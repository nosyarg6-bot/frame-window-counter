#include "Tracker.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/HardStreak.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

/*
 * HOW THE WINDOW IS MEASURED
 * --------------------------
 * The same way you would test it by hand with a macro bot, but live:
 *
 * Every physics tick we save a copy of player 1. When you click or release,
 * we spawn invisible "ghost" players from those saved copies. Each ghost does
 * your input a little earlier or later (in 1/2 or 1/4 frame steps) and then
 * runs on Geometry Dash's own collision and player physics, in lockstep with
 * you, through the obstacles in front of you.
 *
 * A ghost's result is final when:
 *   - it dies (that timing fails), or
 *   - it ends up in exactly the same state as you (it made it), or
 *   - you make your next input that matters (it got you to your next click), or
 *   - you go through a portal / the horizon runs out.
 *
 * So a window is only shown once you've actually passed the obstacle the
 * input was for. The surviving range of timings around your real one is the
 * frame window, counted in 240 FPS frames.
 *
 * Ghosts don't touch orbs, pads, triggers or coins (that could change the
 * real level). Instead, when a ghost touches an orb / pad that YOU used, it
 * gets the same boost you got.
 */

namespace fw {

static constexpr float kConvergeY = 0.1f;
static constexpr float kConvergeX = 0.5f;
static constexpr double kConvergeVy = 0.1;
static constexpr double kHorizonSeconds = 1.5;

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

// ---------------------------------------------------------------------------
// settings helpers

static int tps() {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("physics-tps"), 60, 2000));
}
static double stepFrames() { return 240.0 / tps(); }
static double maxFrames() { return std::clamp(Mod::get()->getSettingValue<double>("max-window"), 2.0, 60.0); }
static double resFrames() { return std::clamp(Mod::get()->getSettingValue<double>("resolution"), 0.25, 1.0); }
static int ghostCap() {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("max-ghosts"), 20, 1000));
}
static int snapshotCount() {
    return static_cast<int>(std::ceil(maxFrames() / stepFrames())) + 4;
}

static int modeKeyOf(PlayerObject* p) {
    int k = 0;
    k |= p->m_isShip ? 1 << 0 : 0;
    k |= p->m_isBird ? 1 << 1 : 0;
    k |= p->m_isBall ? 1 << 2 : 0;
    k |= p->m_isDart ? 1 << 3 : 0;
    k |= p->m_isRobot ? 1 << 4 : 0;
    k |= p->m_isSpider ? 1 << 5 : 0;
    k |= p->m_isSwing ? 1 << 6 : 0;
    k |= p->m_isSideways ? 1 << 7 : 0;
    k |= p->m_isGoingLeft ? 1 << 8 : 0;
    return k;
}

static bool overlaps(CCRect const& a, CCRect const& b) {
    return a.getMinX() < b.getMaxX() && a.getMaxX() > b.getMinX() &&
           a.getMinY() < b.getMaxY() && a.getMaxY() > b.getMinY();
}

// ---------------------------------------------------------------------------
// ghost pool

void Tracker::hidePlayer(PlayerObject* p) {
    p->setVisible(false);
    for (CCParticleSystemQuad* ps : {
             p->m_playerGroundParticles, p->m_trailingParticles, p->m_shipClickParticles,
             p->m_vehicleGroundParticles, p->m_ufoClickParticles, p->m_robotBurstParticles,
             p->m_dashParticles, p->m_swingBurstParticles1, p->m_swingBurstParticles2,
             p->m_landParticles0, p->m_landParticles1}) {
        if (ps) {
            ps->stopSystem();
            ps->setVisible(false);
        }
    }
    if (p->m_regularTrail) p->m_regularTrail->setVisible(false);
    if (p->m_waveTrail) p->m_waveTrail->setVisible(false);
}

PlayerObject* Tracker::acquireGhost(PlayLayer* pl) {
    if (!m_pool.empty()) {
        auto p = m_pool.back();
        m_pool.pop_back();
        return p;
    }
    auto p = PlayerObject::create(1, 1, pl, pl, true);
    if (!p) return nullptr;
    p->retain();
    p->setID("frame-window-ghost"_spr);
    if (pl->m_objectLayer) pl->m_objectLayer->addChild(p);
    hidePlayer(p);
    m_allGhosts.push_back(p);
    return p;
}

void Tracker::releaseGhost(Ghost& g) {
    if (g.obj) {
        m_pool.push_back(g.obj);
        g.obj = nullptr;
    }
}

void Tracker::destroyPool() {
    for (auto p : m_allGhosts) {
        p->removeFromParentAndCleanup(true);
        p->release();
    }
    m_allGhosts.clear();
    m_pool.clear();
    m_snaps.clear();
    m_groups.clear();
}

bool Tracker::isGhost(PlayerObject* p) const {
    if (!p) return false;
    return std::find(m_allGhosts.begin(), m_allGhosts.end(), p) != m_allGhosts.end();
}

void Tracker::onGhostDeath(PlayerObject* p) {
    if (m_curGhost && m_curGhost->obj == p && m_curGhost->status == GStatus::Alive) {
        m_curGhost->status = GStatus::Dead;
        m_curGhost->deathTick = m_stepTick;
        m_curGhost->deathX = p->m_position.x;
    }
}

void Tracker::copyState(PlayerObject* dst, PlayerObject* src, bool holding, bool jumpBuffered) {
    bool wasSim = m_simulating;
    m_simulating = true;
    dst->copyAttributes(src);
    dst->m_gravityMod = src->m_gravityMod;
    dst->m_isOnGround = src->m_isOnGround;
    dst->m_yVelocity = src->m_yVelocity;
    dst->m_position = src->m_position;
    dst->setPosition(src->getPosition());
    dst->m_isUpsideDown = src->m_isUpsideDown;
    dst->m_isDead = false;
    if (holding) dst->pushButton(PlayerButton::Jump);
    else dst->releaseButton(PlayerButton::Jump);
    dst->m_jumpBuffered = jumpBuffered;
    m_simulating = wasSim;
}

Tracker::RealState Tracker::readState(PlayerObject* p, bool holding) const {
    RealState s;
    s.x = p->m_position.x;
    s.y = p->m_position.y;
    s.vy = p->m_yVelocity;
    s.onGround = p->m_isOnGround;
    s.holding = holding;
    s.modeKey = modeKeyOf(p);
    s.speed = p->m_playerSpeed;
    s.size = p->m_vehicleSize;
    return s;
}

int Tracker::liveGhostCount() const {
    int n = 0;
    for (auto const& grp : m_groups) {
        for (auto const& g : grp.ghosts) {
            if (g.obj) ++n;
        }
    }
    return n;
}

// ---------------------------------------------------------------------------
// level / run lifecycle

void Tracker::beginLevel(PlayLayer*) {
    destroyPool();
    m_current = RunRecord{};
    m_running = false;
    m_showLastRun = false;
    m_pending.clear();
    bump();
}

void Tracker::endLevel(PlayLayer* pl) {
    if (m_running) finishRun(pl, false);
    m_running = false;
    destroyPool();
    bump();
}

void Tracker::startRun(PlayLayer* pl) {
    if (!pl) return;
    if (m_running && !m_current.inputs.empty()) finishRun(pl, false);
    finishAllGroups(pl, true);

    m_current = RunRecord{};
    if (pl->m_level) m_current.levelName = std::string(pl->m_level->m_levelName);
    m_current.attempt = pl->m_attempts;
    m_current.practice = pl->m_isPracticeMode;

    m_tick = 0;
    m_pending.clear();
    m_rings.clear();
    m_pads.clear();
    m_realHolding = false;
    m_hasPrevReal = false;
    m_boostThisTick = false;
    m_running = !pl->m_isPlatformer;

    // snapshot ring buffer (enough ticks to move an input max-window frames early)
    int need = snapshotCount();
    for (auto& s : m_snaps) s.tick = -1;
    while (static_cast<int>(m_snaps.size()) < need) {
        Snapshot s;
        s.obj = acquireGhost(pl);
        if (!s.obj) break;
        m_snaps.push_back(s);
    }
    // create ghost players now (at load / respawn) instead of mid-click, so the first click doesn't stutter
    int perGroup = 2 * static_cast<int>(std::floor(maxFrames() / resFrames() + 1e-6)) + 2;
    int warm = std::min(ghostCap(), perGroup * 2);
    while (static_cast<int>(m_pool.size()) + liveGhostCount() < warm) {
        auto p = PlayerObject::create(1, 1, pl, pl, true);
        if (!p) break;
        p->retain();
        p->setID("frame-window-ghost"_spr);
        if (pl->m_objectLayer) pl->m_objectLayer->addChild(p);
        hidePlayer(p);
        m_allGhosts.push_back(p);
        m_pool.push_back(p);
    }
    bump();
}

void Tracker::finishRun(PlayLayer* pl, bool completed) {
    if (!m_running) return;
    finishAllGroups(pl, !completed);
    for (auto const& p : m_pending) {
        auto& in = m_current.inputs[p.inputIdx];
        if (in.state == EvalState::Pending) in.state = EvalState::Unknown;
    }
    m_pending.clear();
    for (auto& in : m_current.inputs) {
        if (in.state == EvalState::Pending) in.state = EvalState::Unknown;
    }

    m_running = false;
    m_current.finished = true;
    m_current.completed = completed;
    m_current.endPercent = completed ? 100.f : (pl ? pl->getCurrentPercent() : 0.f);
    if (!m_current.inputs.empty()) {
        m_history.push_front(m_current);
        size_t maxRuns = static_cast<size_t>(std::max<int64_t>(1, Mod::get()->getSettingValue<int64_t>("history-size")));
        while (m_history.size() > maxRuns) m_history.pop_back();
        m_showLastRun = true;
    }
    bump();
}

// ---------------------------------------------------------------------------
// real player events

void Tracker::onRealButton(PlayLayer* pl, bool down) {
    if (!m_running || !pl || !pl->m_player1 || pl->m_player1->m_isDead) return;
    if (down == m_realHolding) return;
    m_realHolding = down;

    auto p = pl->m_player1;
    // releases only matter for modes where holding changes your movement
    bool relevant = down || p->m_isShip || p->m_isDart || p->m_isRobot;

    InputRecord rec;
    rec.number = static_cast<int>(m_current.inputs.size()) + 1;
    rec.press = down;
    rec.tick = m_tick;
    rec.percent = pl->getCurrentPercent();
    if (!relevant) {
        rec.state = EvalState::NoEffect;
        rec.passedPercent = rec.percent;
    }
    m_current.inputs.push_back(rec);
    m_pending.push_back({down, relevant, m_current.inputs.size() - 1});
    m_showLastRun = false;
    bump();
}

static bool isApproxBoost(GameObject* obj) {
    auto t = obj->m_objectType;
    return t == GameObjectType::DashRing || t == GameObjectType::GravityDashRing ||
           t == GameObjectType::SpiderOrb || t == GameObjectType::SpiderPad ||
           t == GameObjectType::TeleportOrb || t == GameObjectType::CustomRing;
}

void Tracker::onRealRing(GameObject* ring, PlayerObject* p, double vyBefore, bool upBefore, bool dashBefore) {
    if (!m_running || !ring) return;
    if (p->m_yVelocity == vyBefore && p->m_isUpsideDown == upBefore && p->m_isDashing == dashBefore) return;
    Boost b{ring, p->m_yVelocity, p->m_isUpsideDown, m_tick, isApproxBoost(ring) || p->m_isDashing};
    auto it = std::find_if(m_rings.begin(), m_rings.end(), [&](Boost const& o) { return o.obj == ring; });
    if (it != m_rings.end()) *it = b;
    else m_rings.push_back(b);
    m_boostThisTick = true;
}

void Tracker::onRealPad(GameObject* pad, PlayerObject* p) {
    if (!m_running || !pad) return;
    Boost b{pad, p->m_yVelocity, p->m_isUpsideDown, m_tick, isApproxBoost(pad)};
    auto it = std::find_if(m_pads.begin(), m_pads.end(), [&](Boost const& o) { return o.obj == pad; });
    if (it != m_pads.end()) *it = b;
    else m_pads.push_back(b);
    m_boostThisTick = true;
}

// ---------------------------------------------------------------------------
// ghost physics

void Tracker::tryRing(Ghost& g) {
    if (m_rings.empty() || !g.obj) return;
    auto const& r = g.obj->getObjectRect();
    for (auto const& b : m_rings) {
        if (std::find(g.usedBoosts.begin(), g.usedBoosts.end(), b.obj) != g.usedBoosts.end()) continue;
        if (!overlaps(r, b.obj->getObjectRect())) continue;
        g.obj->m_yVelocity = b.vy;
        g.obj->m_isUpsideDown = b.upsideDown;
        g.obj->m_isOnGround = false;
        g.usedBoosts.push_back(b.obj);
        g.ringPending = false;
        if (b.approx) g.approxBoost = true;
        return;
    }
}

void Tracker::tryPad(Ghost& g) {
    if (m_pads.empty() || !g.obj) return;
    auto const& r = g.obj->getObjectRect();
    for (auto const& b : m_pads) {
        if (std::find(g.usedBoosts.begin(), g.usedBoosts.end(), b.obj) != g.usedBoosts.end()) continue;
        if (!overlaps(r, b.obj->getObjectRect())) continue;
        g.obj->m_yVelocity = b.vy;
        g.obj->m_isUpsideDown = b.upsideDown;
        g.obj->m_isOnGround = false;
        g.usedBoosts.push_back(b.obj);
        if (b.approx) g.approxBoost = true;
    }
}

void Tracker::physStep(PlayLayer* pl, Ghost& g, float dt, int tick) {
    auto obj = g.obj;
    if (!obj || g.status != GStatus::Alive || dt <= 0.f) return;
    if (obj->m_collisionLogTop) obj->m_collisionLogTop->removeAllObjects();
    if (obj->m_collisionLogBottom) obj->m_collisionLogBottom->removeAllObjects();
    if (obj->m_collisionLogLeft) obj->m_collisionLogLeft->removeAllObjects();
    if (obj->m_collisionLogRight) obj->m_collisionLogRight->removeAllObjects();

    m_simulating = true;
    m_curGhost = &g;
    m_stepTick = tick;
    pl->checkCollisions(obj, dt, false);
    if (g.status == GStatus::Alive) obj->update(dt);
    m_curGhost = nullptr;
    m_simulating = false;

    if (g.status != GStatus::Alive) return;
    tryPad(g);
    if (g.holding && g.ringPending) tryRing(g);
    if (obj->m_isOnGround) g.ringPending = false;
}

void Tracker::applyEvent(PlayLayer*, Ghost& g, Event const& ev) {
    auto obj = g.obj;
    if (!obj) return;
    m_simulating = true;
    m_curGhost = &g;
    if (ev.press) {
        obj->pushButton(PlayerButton::Jump);
        g.holding = true;
        // orbs can be buffered by holding into them, except as ship / wave
        g.ringPending = !obj->m_isOnGround && !obj->m_isShip && !obj->m_isDart;
    }
    else {
        obj->releaseButton(PlayerButton::Jump);
        g.holding = false;
        g.ringPending = false;
    }
    m_curGhost = nullptr;
    m_simulating = false;
    if (ev.press) tryRing(g);
    if (ev.own) g.fired = true;
}

void Tracker::stepGhost(PlayLayer* pl, Ghost& g, int tick) {
    if (g.status != GStatus::Alive || !g.obj) return;
    float dt = m_dt;
    double pos = 0.0; // fraction of this tick already simulated
    while (!g.events.empty() && g.events.front().time < tick + 1 - 1e-9) {
        Event ev = g.events.front();
        g.events.erase(g.events.begin());
        double f = std::max(0.0, ev.time - tick);
        if (f > pos + 1e-6) {
            physStep(pl, g, static_cast<float>((f - pos) * dt), tick);
            pos = f;
            if (g.status != GStatus::Alive) return;
        }
        applyEvent(pl, g, ev);
    }
    if (1.0 - pos > 1e-6) physStep(pl, g, static_cast<float>((1.0 - pos) * dt), tick);
}

// ---------------------------------------------------------------------------
// groups

void Tracker::createGroup(PlayLayer* pl, PendingInput const& in, int t) {
    auto& rec = m_current.inputs[in.inputIdx];
    int SN = static_cast<int>(m_snaps.size());
    if (SN == 0 || m_dt <= 0.f) {
        rec.state = EvalState::Unknown;
        return;
    }
    auto& base = m_snaps[t % SN];
    if (base.tick != t) {
        rec.state = EvalState::Unknown;
        return;
    }

    double sf = stepFrames();
    double rf = resFrames();
    double mf = maxFrames();
    int maxK = static_cast<int>(std::floor(mf / rf + 1e-6));
    // keep CPU use bounded: coarser steps if too many ghosts are already flying
    int available = ghostCap() - liveGhostCount();
    while (2 * maxK + 2 > available && rf < 1.0) {
        rf *= 2.0;
        maxK = static_cast<int>(std::floor(mf / rf + 1e-6));
    }
    if (2 * maxK + 2 > available) maxK = (available - 2) / 2;
    if (maxK < 1) {
        rec.state = EvalState::Unknown;
        return;
    }

    Group grp;
    grp.inputIdx = in.inputIdx;
    grp.tick = t;
    grp.press = in.press;
    grp.res = rf / sf;
    grp.maxK = maxK;
    grp.horizon = t + static_cast<int>(kHorizonSeconds * tps());

    // earliest allowed shifted time: after the previous input
    int prevTick = -1000000;
    if (in.inputIdx > 0) prevTick = m_current.inputs[in.inputIdx - 1].tick;

    auto spawn = [&](double offset, bool control) -> bool {
        double T = t + offset;
        int s = static_cast<int>(std::floor(T + 1e-9));
        if (offset < 0) {
            if (T < prevTick + 1) return false;
            if (s < 0 || t - s >= SN) return false;
            if (m_snaps[s % SN].tick != s) return false;
        }
        else {
            s = t;
        }
        auto obj = acquireGhost(pl);
        if (!obj) return false;
        auto const& snap = m_snaps[s % SN];
        copyState(obj, snap.obj, snap.holding, snap.jumpBuffered);

        Ghost g;
        g.obj = obj;
        g.offset = offset;
        g.control = control;
        g.baseline = !control && offset == 0.0;
        g.holding = snap.holding;
        if (!control) g.events.push_back({T, in.press, true});
        grp.ghosts.push_back(std::move(g));
        return true;
    };

    spawn(0.0, false);   // baseline
    spawn(0.0, true);    // control (input never happens)
    for (int k = 1; k <= maxK; ++k) {
        if (!spawn(-k * grp.res, false)) break;
    }
    for (int k = 1; k <= maxK; ++k) {
        spawn(k * grp.res, false);
    }

    // catch the early ghosts up to the present, then everyone runs tick t
    for (auto& g : grp.ghosts) {
        int s = g.offset < 0 ? static_cast<int>(std::floor(t + g.offset + 1e-9)) : t;
        for (int k = s; k <= t && g.status == GStatus::Alive; ++k) {
            stepGhost(pl, g, k);
        }
    }
    m_groups.push_back(std::move(grp));
}

void Tracker::prune(Group& grp) {
    // a timing that fails makes every timing further out on that side fail too
    for (int side : {-1, 1}) {
        int firstDead = -1;
        for (int k = 1; k <= grp.maxK; ++k) {
            double off = side * k * grp.res;
            for (auto& g : grp.ghosts) {
                if (g.control || std::abs(g.offset - off) > 1e-9) continue;
                if (g.status == GStatus::Dead) firstDead = k;
                break;
            }
            if (firstDead >= 0) break;
        }
        if (firstDead < 0) continue;
        for (auto& g : grp.ghosts) {
            if (g.control || g.status != GStatus::Alive) continue;
            if (g.offset * side > firstDead * grp.res + 1e-9) {
                g.status = GStatus::Pruned;
                releaseGhost(g);
            }
        }
    }
}

void Tracker::finishGroup(PlayLayer* pl, Group& grp, bool truncated) {
    auto& rec = m_current.inputs[grp.inputIdx];

    Ghost* control = nullptr;
    for (auto& g : grp.ghosts) if (g.control) control = &g;

    auto survived = [&](Ghost const& g) {
        if (g.status == GStatus::Alive || g.status == GStatus::Converged || g.status == GStatus::Bounded) return true;
        if (g.status == GStatus::Dead && grp.baselineDeathTick >= 0 && g.deathTick >= grp.baselineDeathTick) return true;
        return false;
    };
    auto find = [&](double off) -> Ghost* {
        for (auto& g : grp.ghosts) {
            if (!g.control && std::abs(g.offset - off) < 1e-9) return &g;
        }
        return nullptr;
    };

    if (control && control->status == GStatus::Converged) {
        rec.state = EvalState::NoEffect;
    }
    else {
        bool approx = grp.baselineMismatch || grp.baselineDeathTick >= 0;
        int early = 0, late = 0;
        bool capE = false, capL = false;
        for (int k = 1; k <= grp.maxK; ++k) {
            auto g = find(-k * grp.res);
            if (!g || !survived(*g)) break;
            if (g->approxBoost) approx = true;
            early = k;
            if (k == grp.maxK) capE = true;
        }
        for (int k = 1; k <= grp.maxK; ++k) {
            auto g = find(k * grp.res);
            if (!g || !survived(*g)) break;
            if (g->approxBoost) approx = true;
            late = k;
            if (k == grp.maxK) capL = true;
        }
        double sf = stepFrames();
        rec.earlyFrames = static_cast<float>(early * grp.res * sf);
        rec.lateFrames = static_cast<float>(late * grp.res * sf);
        rec.windowFrames = static_cast<float>((early + late + 1) * grp.res * sf);
        rec.cappedEarly = capE;
        rec.cappedLate = capL;
        rec.approx = approx;
        rec.truncated = truncated;
        rec.state = EvalState::Done;
    }
    rec.passedPercent = pl ? pl->getCurrentPercent() : rec.percent;

    for (auto& g : grp.ghosts) releaseGhost(g);
    grp.ghosts.clear();
    bump();
}

void Tracker::finishAllGroups(PlayLayer* pl, bool truncated) {
    for (auto& grp : m_groups) finishGroup(pl, grp, truncated);
    m_groups.clear();
}

// ---------------------------------------------------------------------------
// per tick

void Tracker::processTick(PlayLayer* pl, int t) {
    auto real = pl->m_player1;
    RealState cur = readState(real, m_realHolding);

    bool inputThisTick = !m_pending.empty();

    // did the player go through a portal (mode / speed / size / teleport / gravity portal)?
    bool portal = false;
    if (m_hasPrevReal) {
        portal = cur.modeKey != m_prevReal.modeKey ||
                 std::abs(cur.speed - m_prevReal.speed) > 0.001f ||
                 std::abs(cur.size - m_prevReal.size) > 0.001f ||
                 std::abs(cur.x - m_prevReal.x) > 60.f;
        bool flipped = cur.modeKey == m_prevReal.modeKey && real->m_isUpsideDown != m_prevUpsideDown;
        if (flipped && !inputThisTick && !m_boostThisTick && !real->m_isBall && !real->m_isSwing && !real->m_isSpider) {
            portal = true;
        }
    }
    m_boostThisTick = false;

    // inputs that happened during tick t
    for (auto const& in : m_pending) {
        if (in.relevant) {
            // your next real input: every running measurement got you this far
            for (auto& grp : m_groups) {
                for (auto& g : grp.ghosts) {
                    if (g.status == GStatus::Alive && !g.control && !g.fired) {
                        g.status = GStatus::Bounded; // its timing would be after this input
                        releaseGhost(g);
                    }
                }
            }
            finishAllGroups(pl, false);
        }
        else {
            // e.g. releasing as the cube: replay it into running ghosts so holding stays in sync
            for (auto& grp : m_groups) {
                for (auto& g : grp.ghosts) {
                    if (g.status != GStatus::Alive) continue;
                    double time = t;
                    if (!g.control && !g.fired) {
                        // ghost hasn't done its own input yet: keep the same spacing after it
                        time = grp.tick + g.offset + (t - grp.tick);
                    }
                    Event ev{time, in.press, false};
                    auto it = std::upper_bound(g.events.begin(), g.events.end(), ev,
                        [](Event const& a, Event const& b) { return a.time < b.time; });
                    g.events.insert(it, ev);
                }
            }
        }
    }

    if (portal) finishAllGroups(pl, false);

    // existing ghosts run tick t
    for (auto& grp : m_groups) {
        for (auto& g : grp.ghosts) stepGhost(pl, g, t);
    }

    // new measurements for inputs of tick t
    for (auto const& in : m_pending) {
        if (in.relevant) createGroup(pl, in, t);
    }
    m_pending.clear();

    // check every group against where you really are now
    for (auto& grp : m_groups) {
        bool allDone = true;
        bool finishNow = false;
        for (auto& g : grp.ghosts) {
            if (g.status == GStatus::Dead && g.obj) releaseGhost(g);
            if (g.status != GStatus::Alive) continue;

            auto o = g.obj;
            bool same = std::abs(o->m_position.x - cur.x) < kConvergeX &&
                        std::abs(o->m_position.y - cur.y) < kConvergeY &&
                        std::abs(o->m_yVelocity - cur.vy) < kConvergeVy &&
                        o->m_isOnGround == cur.onGround &&
                        g.holding == cur.holding &&
                        modeKeyOf(o) == cur.modeKey &&
                        o->m_isUpsideDown == real->m_isUpsideDown;

            if (g.baseline && g.fired && !same &&
                (std::abs(o->m_position.y - cur.y) > 3.f || std::abs(o->m_position.x - cur.x) > 3.f)) {
                grp.baselineMismatch = true;
            }
            if ((g.fired || g.control) && same) {
                if (++g.convergedTicks >= 2) {
                    g.status = GStatus::Converged;
                    releaseGhost(g);
                    continue;
                }
            }
            else {
                g.convergedTicks = 0;
            }
            allDone = false;
        }
        for (auto& g : grp.ghosts) {
            if (g.baseline && g.status == GStatus::Dead) {
                // the simulation didn't match your real run here; anything that
                // survived until this point counts as surviving
                grp.baselineDeathTick = g.deathTick;
                finishNow = true;
            }
            if (g.control && g.status == GStatus::Converged) finishNow = true;
        }
        if (!finishNow) prune(grp);
        if (finishNow || allDone || t >= grp.horizon) {
            finishGroup(pl, grp, false);
        }
    }
    m_groups.erase(
        std::remove_if(m_groups.begin(), m_groups.end(), [](Group const& g) { return g.ghosts.empty(); }),
        m_groups.end()
    );

    m_prevReal = cur;
    m_prevUpsideDown = real->m_isUpsideDown;
    m_hasPrevReal = true;
}

void Tracker::onTickStart(PlayLayer* pl) {
    if (!m_running || !pl) return;
    auto real = pl->m_player1;
    if (!real || real->m_isDead) return;

    if (m_tick > 0) processTick(pl, m_tick - 1);
    if (!m_running) return;

    // save the state at the start of this tick
    if (!m_snaps.empty()) {
        auto& snap = m_snaps[m_tick % m_snaps.size()];
        if (snap.obj) {
            bool wasSim = m_simulating;
            m_simulating = true;
            snap.obj->copyAttributes(real);
            snap.obj->m_gravityMod = real->m_gravityMod;
            snap.obj->m_isOnGround = real->m_isOnGround;
            snap.obj->m_yVelocity = real->m_yVelocity;
            snap.obj->m_position = real->m_position;
            snap.obj->setPosition(real->getPosition());
            snap.obj->m_isUpsideDown = real->m_isUpsideDown;
            m_simulating = wasSim;
            snap.tick = m_tick;
            snap.holding = m_realHolding;
            snap.jumpBuffered = real->m_jumpBuffered;
        }
    }
}

void Tracker::onTickEnd(PlayLayer*) {
    if (!m_running) return;
    ++m_tick;
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
    int f = static_cast<int>(std::floor(r.windowFrames + 1e-4));
    auto s = fmt::format("{}-{}", f, f + 1);
    if (r.approx) s = "~" + s;
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
