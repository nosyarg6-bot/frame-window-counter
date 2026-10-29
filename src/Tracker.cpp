#include "Tracker.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <climits>
#include <cmath>

using namespace geode::prelude;

/*
 * HOW THE WINDOW IS CALCULATED
 * ----------------------------
 * Every physics tick (240 per second in vanilla 2.2) we record player 1's
 * position and hitbox. Every click / release is stamped with the tick it
 * happened on.
 *
 * To find out how much leeway an input had, we "move" it d ticks earlier or
 * later and rebuild the trajectory that would have followed:
 *
 *   - Horizontal position is taken from the real recording (in GD the x
 *     movement does not depend on your inputs).
 *   - Before the moved input, the player keeps doing what it was doing
 *     (standing on the ground, falling, flying straight...). This is a
 *     constant-acceleration extrapolation from the last ticks before the
 *     input.
 *   - After the moved input, the player follows the exact same vertical
 *     curve the real input produced, just starting from where the moved
 *     input happened.
 *   - When the next real input arrives, the vertical difference is carried
 *     along (ship / wave / etc.) until the player touches the ground.
 *
 * That path is checked against every spike / hazard (outer hitbox) and block
 * (inner hitbox) nearby. Objects the real run touched safely (the floor you
 * were sliding on, etc.) are ignored. We walk outwards from the real timing
 * in quarter-frame steps until the moved path dies; the surviving range is
 * the frame window.
 *
 * This is a model, not a full re-simulation of the game, so orbs, pads,
 * portals and moving objects are only approximated.
 */

namespace fw {

static constexpr double kStep = 1.0;          // trajectory check resolution (ticks)
static constexpr double kResFrames = 0.25;    // offset resolution (240fps frames)
static constexpr double kHorizonSeconds = 0.75;

int RunRecord::effectiveCount() const {
    int n = 0;
    for (auto const& i : inputs) {
        if (i.state == EvalState::Done) ++n;
    }
    return n;
}

InputRecord const* RunRecord::hardest() const {
    InputRecord const* best = nullptr;
    for (auto const& i : inputs) {
        if (i.state != EvalState::Done || i.isFree()) continue;
        if (!best || i.windowFrames < best->windowFrames) best = &i;
    }
    return best;
}

Tracker& Tracker::get() {
    static Tracker inst;
    return inst;
}

static int tps() {
    return static_cast<int>(std::clamp<int64_t>(
        Mod::get()->getSettingValue<int64_t>("physics-tps"), 60, 2000
    ));
}

static double stepFrames() {
    // how many 240fps frames one physics tick is worth
    return 240.0 / tps();
}

void Tracker::beginLevel(PlayLayer*) {
    m_colliders.clear();
    m_samples.clear();
    m_current = RunRecord{};
    m_running = false;
    m_showLastRun = false;
    m_nextEval = 0;
    bump();
}

void Tracker::endLevel(PlayLayer* pl) {
    if (m_running && !m_current.inputs.empty()) {
        finishRun(pl, false);
    }
    m_running = false;
    m_colliders.clear();
    m_samples.clear();
    m_samples.shrink_to_fit();
    bump();
}

static void buildColliders(PlayLayer* pl, std::vector<Collider>& out) {
    out.clear();
    if (!pl || !pl->m_objects) return;
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj) continue;
        auto type = obj->m_objectType;
        bool hazard = type == GameObjectType::Hazard || type == GameObjectType::AnimatedHazard;
        bool solid = type == GameObjectType::Solid;
        if (!hazard && !solid) continue;
        if (obj->m_isNoTouch || obj->m_isPassable) continue;
        auto const& r = obj->getObjectRect();
        if (r.size.width <= 0.f || r.size.height <= 0.f) continue;
        out.push_back({obj, r.getMinX(), hazard});
    }
    std::sort(out.begin(), out.end(), [](Collider const& a, Collider const& b) {
        return a.minX < b.minX;
    });
    log::info("Frame Window Counter: tracking {} hazards/blocks", out.size());
}

void Tracker::startRun(PlayLayer* pl) {
    if (!pl) return;
    // restarting without dying still saves the run
    if (m_running && !m_current.inputs.empty()) {
        finishRun(pl, false);
    }
    if (m_colliders.empty()) buildColliders(pl, m_colliders);

    m_samples.clear();
    m_samples.reserve(240 * 60);
    m_current = RunRecord{};
    if (pl->m_level) m_current.levelName = std::string(pl->m_level->m_levelName);
    m_current.attempt = pl->m_attempts;
    m_current.practice = pl->m_isPracticeMode;
    m_nextEval = 0;
    m_holding = false;
    m_running = !pl->m_isPlatformer;
    bump();
}

void Tracker::finishRun(PlayLayer* pl, bool completed) {
    if (!m_running) return;
    evaluatePending(true, INT_MAX);
    m_running = false;
    m_current.finished = true;
    m_current.completed = completed;
    m_current.endPercent = completed ? 100.f : (pl ? pl->getCurrentPercent() : 0.f);
    if (!m_current.inputs.empty()) {
        m_history.push_front(m_current);
        size_t maxRuns = static_cast<size_t>(std::max<int64_t>(
            1, Mod::get()->getSettingValue<int64_t>("history-size")
        ));
        while (m_history.size() > maxRuns) m_history.pop_back();
        m_showLastRun = true;
    }
    bump();
}

void Tracker::onTickStart(PlayLayer* pl) {
    m_inTick = true;
    if (!m_running || !pl) return;
    auto p = pl->m_player1;
    if (!p || p->m_isDead) return;
    Sample s;
    s.x = p->m_position.x;
    s.y = p->m_position.y;
    auto const& r = p->getObjectRect();
    s.w = r.size.width > 0.f ? r.size.width : 30.f;
    s.h = r.size.height > 0.f ? r.size.height : 30.f;
    s.onGround = p->m_isOnGround;
    s.sideways = p->m_isSideways;
    m_samples.push_back(s);
}

void Tracker::onTickEnd(PlayLayer*) {
    m_inTick = false;
    if (!m_running) return;
    evaluatePending(false, 1);
}

void Tracker::onButton(PlayLayer* pl, bool down) {
    if (!m_running || !pl) return;
    if (!pl->m_player1 || pl->m_player1->m_isDead) return;
    if (down == m_holding) return;
    m_holding = down;

    InputRecord rec;
    rec.number = static_cast<int>(m_current.inputs.size()) + 1;
    rec.press = down;
    // inside a tick the sample for this tick was already taken
    rec.preIdx = m_inTick ? static_cast<int>(m_samples.size()) - 1
                          : static_cast<int>(m_samples.size());
    rec.preIdx = std::max(rec.preIdx, 0);
    rec.percent = pl->getCurrentPercent();
    m_current.inputs.push_back(rec);
    m_showLastRun = false;
    bump();
}

// ---------------------------------------------------------------------------

float Tracker::X(double t) const {
    int last = static_cast<int>(m_samples.size()) - 1;
    if (t <= 0) return m_samples.front().x;
    int i = static_cast<int>(std::floor(t));
    if (i >= last) return m_samples[last].x;
    float f = static_cast<float>(t - i);
    return m_samples[i].x + (m_samples[i + 1].x - m_samples[i].x) * f;
}

float Tracker::Y(double t) const {
    int last = static_cast<int>(m_samples.size()) - 1;
    if (t <= 0) return m_samples.front().y;
    int i = static_cast<int>(std::floor(t));
    if (i >= last) return m_samples[last].y;
    float f = static_cast<float>(t - i);
    return m_samples[i].y + (m_samples[i + 1].y - m_samples[i].y) * f;
}

float Tracker::extrap(Ctx const& c, double j) const {
    if (c.grounded) return c.y0;
    return static_cast<float>(c.y0 + c.v * j + 0.5 * c.a * j * (j + 1.0));
}

static bool overlaps(float l1, float b1, float r1, float t1, CCRect const& r) {
    return l1 < r.getMaxX() && r1 > r.getMinX() && b1 < r.getMaxY() && t1 > r.getMinY();
}

bool Tracker::hitsAt(Ctx& c, float x, float y, float w, float h, bool baseline) {
    float hw = w * 0.5f, hh = h * 0.5f;
    float iw = hw * m_innerScale, ih = hh * m_innerScale;
    for (size_t i = 0; i < c.cands.size(); ++i) {
        if (!baseline && c.ignore[i]) continue;
        auto const* col = c.cands[i];
        auto const& r = col->obj->getObjectRect();
        if (baseline) {
            if (overlaps(x - hw, y - hh, x + hw, y + hh, r)) c.ignore[i] = 1;
            continue;
        }
        if (col->hazard) {
            if (overlaps(x - hw, y - hh, x + hw, y + hh, r)) return true;
        }
        else {
            if (overlaps(x - iw, y - ih, x + iw, y + ih, r)) return true;
        }
    }
    return false;
}

bool Tracker::survives(Ctx& c, double d, bool baseline) {
    int last = static_cast<int>(m_samples.size()) - 1;
    double T = c.p + d;                       // when the moved input happens
    double start = (d < 0) ? T : c.p;         // before this the path is unchanged
    double Yb = (d <= 0) ? Y(T) : extrap(c, d);
    double Yp = m_samples[c.p].y;
    double carry = 0.0;
    bool afterNext = false;

    // align the first check to the grid so every offset is checked at comparable points
    for (double t = start; t <= c.H + 1e-6; t += kStep) {
        double y;
        if (t < T) {
            y = extrap(c, t - c.p);
        }
        else if (c.n < 0 || t < c.n) {
            double src = t - d;
            if (src > last) break;            // no more data: assume it lives
            y = Yb + Y(src) - Yp;
        }
        else {
            if (!afterNext) {
                double src = std::min<double>(c.n - d, last);
                double yAtN = (c.n < T) ? extrap(c, c.n - c.p) : Yb + Y(src) - Yp;
                carry = yAtN - m_samples[c.n].y;
                afterNext = true;
            }
            int ti = static_cast<int>(t);
            if (ti > last) break;
            if (m_samples[ti].onGround) carry = 0.0;
            y = Y(t) + carry;
        }
        int si = std::clamp(static_cast<int>(t), 0, last);
        auto const& s = m_samples[si];
        if (hitsAt(c, X(t), static_cast<float>(y), s.w, s.h, baseline)) return false;
    }
    return true;
}

bool Tracker::tryEvaluate(size_t idx, bool final) {
    auto& in = m_current.inputs[idx];
    if (in.state != EvalState::Pending) return true;

    int last = static_cast<int>(m_samples.size()) - 1;
    int p = in.preIdx;
    double sf = stepFrames();
    double maxFrames = Mod::get()->getSettingValue<double>("max-window");
    int maxSteps = static_cast<int>(std::ceil(maxFrames / sf)) + 1;
    int horizon = static_cast<int>(kHorizonSeconds * tps());

    auto preOf = [&](size_t k) { return m_current.inputs[k].preIdx; };
    int n = idx + 1 < m_current.inputs.size() ? preOf(idx + 1) : -1;
    int nn = idx + 2 < m_current.inputs.size() ? preOf(idx + 2) : -1;
    int prev = idx > 0 ? preOf(idx - 1) : -1;

    int H = p + horizon;
    if (nn >= 0) H = std::min(H, nn);

    if (!final && last < H + maxSteps + 1) return false;   // wait for more data
    if (p < 3 || p > last) {
        in.state = EvalState::Unknown;
        return true;
    }
    if (H > last) {
        H = last;
        in.truncated = true;
    }
    if (n > last) n = -1;
    if (m_samples[p].sideways) {
        in.state = EvalState::Unknown;
        return true;
    }

    m_innerScale = static_cast<float>(Mod::get()->getSettingValue<double>("inner-hitbox"));

    Ctx c;
    c.p = p;
    c.n = n;
    c.prev = prev;
    c.H = H;
    c.y0 = m_samples[p].y;
    c.v = m_samples[p].y - m_samples[p - 1].y;
    c.a = (m_samples[p].y - m_samples[p - 1].y) - (m_samples[p - 1].y - m_samples[p - 2].y);
    c.grounded = m_samples[p].onGround && std::abs(c.v) < 0.01f;

    // did this input change anything?
    {
        int checks = std::min(8, last - p);
        if (checks < 2) {
            in.state = final ? EvalState::Unknown : EvalState::Pending;
            return final;
        }
        float maxDiff = 0.f;
        for (int j = 1; j <= checks; ++j) {
            maxDiff = std::max(maxDiff, std::abs(m_samples[p + j].y - extrap(c, j)));
        }
        if (maxDiff < 0.35f) {
            in.state = EvalState::NoEffect;
            return true;
        }
    }

    // collect nearby objects
    {
        float lo = 1e30f, hi = -1e30f;
        for (int i = std::max(0, p - maxSteps); i <= H; ++i) {
            lo = std::min(lo, m_samples[i].x);
            hi = std::max(hi, m_samples[i].x);
        }
        lo -= 90.f;
        hi += 90.f;
        auto it = std::lower_bound(
            m_colliders.begin(), m_colliders.end(), lo - 240.f,
            [](Collider const& col, float v) { return col.minX < v; }
        );
        for (; it != m_colliders.end() && it->minX <= hi; ++it) {
            auto const& r = it->obj->getObjectRect();
            if (r.getMaxX() < lo || r.getMinX() > hi) continue;
            c.cands.push_back(&*it);
        }
        c.ignore.assign(c.cands.size(), 0);
    }

    // anything the real run touched is assumed safe (floors, blocks you slid on...)
    survives(c, 0.0, true);

    double res = kResFrames / sf;     // offset resolution in ticks
    int maxK = static_cast<int>(std::floor(maxFrames / kResFrames));

    double late = 0.0;
    for (int k = 1; k <= maxK; ++k) {
        double d = k * res;
        if (n >= 0 && p + d >= n - 1e-6) break;          // can't pass the next input
        if (!survives(c, d, false)) break;
        late = d;
        if (k == maxK) in.cappedLate = true;
    }
    double early = 0.0;
    for (int k = 1; k <= maxK; ++k) {
        double d = -k * res;
        if (p + d < 2.0) break;
        if (prev >= 0 && p + d <= prev + 1e-6) break;     // can't pass the previous input
        if (!survives(c, d, false)) break;
        early = d;
        if (k == maxK) in.cappedEarly = true;
    }

    in.earlyFrames = static_cast<float>(-early * sf);
    in.lateFrames = static_cast<float>(late * sf);
    in.windowFrames = static_cast<float>((late - early) * sf + kResFrames);
    in.state = EvalState::Done;
    return true;
}

void Tracker::evaluatePending(bool final, int budget) {
    bool changed = false;
    while (m_nextEval < m_current.inputs.size() && budget > 0) {
        if (!tryEvaluate(m_nextEval, final)) break;
        ++m_nextEval;
        --budget;
        changed = true;
    }
    if (final) {
        for (size_t i = m_nextEval; i < m_current.inputs.size(); ++i) {
            auto& in = m_current.inputs[i];
            if (in.state == EvalState::Pending) in.state = EvalState::Unknown;
        }
        m_nextEval = m_current.inputs.size();
        changed = true;
    }
    if (changed) bump();
}

// ---------------------------------------------------------------------------

std::string windowLabel(InputRecord const& r) {
    switch (r.state) {
        case EvalState::Pending: return "...";
        case EvalState::NoEffect: return "-";
        case EvalState::Unknown: return "?";
        case EvalState::Done: break;
    }
    if (r.isFree()) return "free";
    int f = static_cast<int>(std::floor(r.windowFrames));
    auto s = fmt::format("{}-{}", f, f + 1);
    if (r.truncated) s += "*";
    return s;
}

ccColor3B windowColor(InputRecord const& r) {
    if (r.state != EvalState::Done) return {150, 150, 150};
    if (r.isFree()) return {200, 200, 200};
    float w = r.windowFrames;
    if (w < 1.f) return {255, 60, 220};   // sub-frame: magenta
    if (w < 2.f) return {255, 70, 70};    // 1-2: red
    if (w < 3.f) return {255, 150, 50};   // 2-3: orange
    if (w < 4.f) return {255, 230, 60};   // 3-4: yellow
    if (w < 6.f) return {150, 255, 80};   // 4-6: lime
    if (w < 10.f) return {80, 220, 255};  // 6-10: cyan
    return {200, 200, 200};
}

} // namespace fw
