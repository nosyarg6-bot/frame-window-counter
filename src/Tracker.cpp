#include "Tracker.hpp"
#include "PlayerFields.hpp"
#include "Replay.hpp"

#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/HardStreak.hpp>
#include <Geode/binding/OBB2D.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

/*
 * HOW THE WINDOW IS MEASURED
 * --------------------------
 * Like testing a macro: take one input, move it earlier / later, keep every
 * other input exactly where it was, and see if you still make it.
 *
 * Every physics step we save a full copy of player 1's physics state. When
 * you click or release, invisible test players are spawned from those copies.
 * Each one does that input shifted by some fraction of a 240 FPS frame and
 * then runs on Geometry Dash's own player physics and collision code, in
 * lockstep with you, receiving all of your later inputs at their real times.
 *
 * A test player's result is final when
 *   - it dies          -> that timing fails (we remember what killed it)
 *   - it ends up in exactly the same state as you -> that timing works
 *   - the check-ahead time runs out while it's still alive -> works
 *
 * Shifting an input past your previous / next input is not allowed (a macro
 * can't reorder inputs), so very close inputs limit each other ("click
 * spacing"). A cube's release moves together with its click.
 *
 * Everything is measured in 240 FPS frames of real time, whatever FPS, TPS,
 * time warp or Click Between Frames setting you play with.
 */

namespace fw {

static constexpr float kConvergeX = 0.5f;
static constexpr float kConvergeY = 0.05f;
static constexpr double kConvergeVy = 0.05;

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
// settings

static double maxFrames() { return std::clamp(Mod::get()->getSettingValue<double>("max-window"), 2.0, 30.0); }
static double precision() { return std::clamp(Mod::get()->getSettingValue<double>("resolution"), 0.125, 1.0); }
static double horizonSeconds() { return std::clamp(Mod::get()->getSettingValue<double>("check-ahead"), 0.25, 3.0); }
static int ghostCap() {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("max-ghosts"), 30, 2000));
}

// Shifts to test, in 240fps frames. Fine steps where it matters (hard clicks),
// coarser steps for big windows to keep the number of test players down.
std::vector<double> Tracker::buildOffsets() const {
    double p = precision();
    double mf = maxFrames();
    std::vector<double> out;
    double x = 0.0;
    while (true) {
        double step = x < 2.0 ? p : x < 4.0 ? std::max(p, 0.5) : x < 8.0 ? std::max(p, 1.0) : std::max(p, 2.0);
        x += step;
        if (x > mf + 1e-9) break;
        out.push_back(x);
    }
    if (out.empty() || out.back() < mf - 1e-9) out.push_back(mf);
    return out;
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

static void resetCollisionLog(PlayerObject* p) {
    if (p->m_collisionLogTop) p->m_collisionLogTop->removeAllObjects();
    if (p->m_collisionLogBottom) p->m_collisionLogBottom->removeAllObjects();
    if (p->m_collisionLogLeft) p->m_collisionLogLeft->removeAllObjects();
    if (p->m_collisionLogRight) p->m_collisionLogRight->removeAllObjects();
}

static std::string killerName(GameObject* obj) {
    if (!obj) return "ground";
    switch (obj->m_objectType) {
        case GameObjectType::Hazard: return obj->m_objectRadius > 0.f ? "saw" : "spike";
        case GameObjectType::AnimatedHazard: return "saw";
        case GameObjectType::Solid: return "block";
        case GameObjectType::Slope: return "slope";
        default: return "object";
    }
}

// ---------------------------------------------------------------------------
// hit detection (same shapes the game uses: boxes, rotated boxes, circles)

static constexpr int kHitGrace = 6; // steps to wait before a hit counts as a death

static OBB2D* scratchOBB() {
    static OBB2D* box = nullptr;
    if (!box) {
        box = OBB2D::create({0.f, 0.f}, 1.f, 1.f, 0.f);
        if (box) box->retain();
    }
    return box;
}

// how far two boxes overlap (0 = not touching)
static float rectDepth(CCRect const& a, CCRect const& b) {
    float ox = std::min(a.getMaxX(), b.getMaxX()) - std::max(a.getMinX(), b.getMinX());
    float oy = std::min(a.getMaxY(), b.getMaxY()) - std::max(a.getMinY(), b.getMinY());
    if (ox <= 0.f || oy <= 0.f) return 0.f;
    return std::min(ox, oy);
}

static float circleDepth(CCRect const& r, CCPoint c, float radius) {
    float nx = std::clamp(c.x, r.getMinX(), r.getMaxX());
    float ny = std::clamp(c.y, r.getMinY(), r.getMaxY());
    float d = std::sqrt((c.x - nx) * (c.x - nx) + (c.y - ny) * (c.y - ny));
    return std::max(0.f, radius - d);
}

static float objectDepth(CCRect const& pr, GameObject* obj) {
    auto const& orect = obj->getObjectRect();
    if (orect.size.width <= 0.f || orect.size.height <= 0.f) return 0.f;
    float box = rectDepth(pr, orect);
    if (box <= 0.f) return 0.f; // bounding boxes don't even touch
    if (obj->m_objectRadius > 0.f) {
        float r = std::min(orect.size.width, orect.size.height) * 0.5f;
        return circleDepth(pr, {orect.getMidX(), orect.getMidY()}, r);
    }
    float rot = std::fmod(std::abs(obj->getRotation()), 90.f);
    if (rot > 0.01f && rot < 89.99f) {
        auto ob = obj->getOrientedBox();
        auto pb = scratchOBB();
        if (ob && pb) {
            pb->calculateWithCenter({pr.getMidX(), pr.getMidY()}, pr.size.width, pr.size.height, 0.f);
            if (!(pb->overlaps1Way(ob) && ob->overlaps1Way(pb))) return 0.f;
        }
    }
    return box;
}

// How deep the player is inside something deadly (0 = safe). Same shapes the
// game uses: the full hitbox for spikes / saws, the small inner hitbox for blocks.
float Tracker::hitDepth(PlayerObject* p, GameObject* obj) {
    if (!obj || obj->m_isNoTouch || obj->m_isPassable || obj->m_isDisabled || obj->m_isGroupDisabled) return 0.f;
    auto t = obj->m_objectType;
    if (t == GameObjectType::Hazard || t == GameObjectType::AnimatedHazard) {
        return objectDepth(p->getObjectRect(), obj);
    }
    if (t == GameObjectType::Solid) {
        return rectDepth(p->getObjectRect(0.3f, 0.3f), obj->getObjectRect());
    }
    return 0.f;
}

// A hit is a real death unless your own run went just as deep into the same
// object and survived. Going clearly deeper than you did still counts, so a
// spike you barely grazed still kills timings that go further into it.
static constexpr float kDepthTolerance = 0.75f;

bool Tracker::isGenuine(Ghost::Hit const& h, Group const& grp) const {
    float allowed = -1.f;
    if (auto it = m_realDepth.find(h.obj); it != m_realDepth.end()) allowed = std::max(allowed, it->second);
    if (auto it = grp.baselineDepth.find(h.obj); it != grp.baselineDepth.end()) allowed = std::max(allowed, it->second);
    if (allowed < 0.f) return true; // you never touched it
    return h.depth > allowed + kDepthTolerance;
}

void Tracker::onCollisionList(PlayerObject* p, gd::vector<GameObject*>* objs, int count) {
    if (!m_running || !p || !objs) return;
    if (m_simulating) {
        if (!m_curGhost || m_curGhost->obj != p) return;
        for (int i = 0; i < count; ++i) {
            auto o = objs->at(i);
            float d = hitDepth(p, o);
            if (d > 0.f) onGhostDeath(p, o, d);
        }
        return;
    }
    // your real player: anything the check flags while you live is a false alarm,
    // so test players ignore it too
    auto pl = PlayLayer::get();
    if (!pl || p != pl->m_player1 || p->m_isDead) return;
    for (int i = 0; i < count; ++i) {
        auto o = objs->at(i);
        float d = o ? hitDepth(p, o) : 0.f;
        if (d > 0.f) {
            auto& cur = m_realDepth[o];
            cur = std::max(cur, d);
        }
    }
}

// ---------------------------------------------------------------------------
// test player objects

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


PlayerObject* Tracker::acquireObj(PlayLayer* pl) {
    if (!m_pool.empty()) {
        auto p = m_pool.back();
        m_pool.pop_back();
        return p;
    }
    return newObj(pl);
}

PlayerObject* Tracker::newObj(PlayLayer* pl) {
    // test players live in an invisible layer outside the level, so hitbox
    // viewers (MegaHack, practice hitboxes...) don't draw them near obstacles
    bool hide = true;
    if (hide && !m_ghostRoot) {
        m_ghostRoot = CCLayer::create();
        m_ghostRoot->retain();
        m_ghostRoot->setVisible(false);
        m_ghostRoot->setID("test-players"_spr);
        pl->addChild(m_ghostRoot, -1000);
    }
    CCLayer* home = hide ? m_ghostRoot : static_cast<CCLayer*>(pl);
    auto p = PlayerObject::create(1, 1, pl, home, true);
    if (!p) return nullptr;
    p->retain();
    p->setID("frame-window-ghost"_spr);
    if (hide) m_ghostRoot->addChild(p);
    else if (pl->m_objectLayer) pl->m_objectLayer->addChild(p);
    hidePlayer(p);
    m_allObjs.push_back(p);
    m_ghostSet.insert(p);
    return p;
}

void Tracker::releaseObj(Ghost& g) {
    if (g.obj) {
        m_pool.push_back(g.obj);
        g.obj = nullptr;
    }
}

void Tracker::destroyPool() {
    m_groups.clear();
    for (auto p : m_allObjs) {
        p->removeFromParentAndCleanup(true);
        p->release();
    }
    m_allObjs.clear();
    m_ghostSet.clear();
    m_pool.clear();
    m_snaps.clear();
    if (m_drawNode) {
        m_drawNode->removeFromParentAndCleanup(true);
        m_drawNode->release();
        m_drawNode = nullptr;
    }
    if (m_ghostRoot) {
        m_ghostRoot->removeFromParentAndCleanup(true);
        m_ghostRoot->release();
        m_ghostRoot = nullptr;
    }
    clearLiveLabels();
}

// ---------------------------------------------------------------------------
// frame windows shown in the level (optional)

void Tracker::spawnLiveLabel(PlayLayer* pl, size_t idx, float x, float y) {
    // remember where you clicked; the label pops up there once the window is known
    if (!pl->m_objectLayer) return;
    m_clickPos[idx] = CCPoint(x, y);
}

void Tracker::finishLiveLabel(size_t idx) {
    auto it = m_clickPos.find(idx);
    if (it == m_clickPos.end()) return;
    CCPoint pos = it->second;
    m_clickPos.erase(it);
    auto pl = PlayLayer::get();
    if (!pl || !pl->m_objectLayer || !Mod::get()->getSettingValue<bool>("show-level-labels")) return;
    auto const& in = m_current.inputs[idx];
    if (in.state != EvalState::Done || in.isFree()) return;
    double maxShown = Mod::get()->getSettingValue<double>("level-labels-max");
    if (in.windowFrames > maxShown) return;

    auto label = makeWorldLabel(in);
    label->setPosition({pos.x, pos.y + 32.f});
    pl->m_objectLayer->addChild(label);
    label->runAction(CCSequence::create(
        CCDelayTime::create(1.5f), CCFadeOut::create(0.5f), CCRemoveSelf::create(), nullptr
    ));
}

void Tracker::clearLiveLabels() {
    m_clickPos.clear();
}

void Tracker::copyState(PlayerObject* dst, PlayerObject* src, bool holding) {
    bool wasSim = m_simulating;
    m_simulating = true;
    dst->copyAttributes(src);
#define FW_COPY(name) dst->name = src->name;
    FW_PLAYER_FIELDS(FW_COPY)
#undef FW_COPY
    dst->setPosition(src->getPosition());
    // the game decides landing vs. crashing from where you were last step
    dst->m_lastPosition = src->m_lastPosition;
    dst->setRotation(src->getRotation());
    dst->m_isDead = false;
    dst->m_ignoreDamage = false; // never inherit noclip
    dst->m_holdingButtons[1] = holding;
    m_simulating = wasSim;
}

Tracker::Snapshot* Tracker::snapshotAt(int tick) {
    if (m_snaps.empty() || tick < 0) return nullptr;
    auto& s = m_snaps[tick % m_snaps.size()];
    return s.tick == tick ? &s : nullptr;
}

int Tracker::liveGhostCount() const {
    int n = 0;
    for (auto const& grp : m_groups) {
        auto count = [&](Ghost const& g) { if (g.obj) ++n; };
        count(grp.control);
        count(grp.baseline);
        for (auto const& g : grp.early) count(g);
        for (auto const& g : grp.late) count(g);
    }
    return n;
}

// A test player touched something deadly. It keeps going (like noclip) and the
// hit only becomes a death if your own run didn't touch the same thing.
void Tracker::onGhostDeath(PlayerObject* p, GameObject* killer, float depth) {
    if (!m_curGhost || m_curGhost->obj != p || m_curGhost->status != GStatus::Alive) return;
    if (auto pl = PlayLayer::get(); pl && killer && killer == pl->m_anticheatSpike) return;
    if (depth < 0.f) {
        // reported by the game itself: always at least a small hit
        depth = std::max(killer ? hitDepth(p, killer) : 1.f, 0.05f);
    }
    for (auto& h : m_curGhost->hits) {
        if (h.obj == killer) {
            h.depth = std::max(h.depth, depth);
            return;
        }
    }
    m_curGhost->hits.push_back({killer, m_stepTick, depth});
}

void Tracker::resolveHits(Group& grp, int t) {
    // the copy of your exact run never dies; whatever it "hit", you survived
    for (auto const& h : grp.baseline.hits) {
        auto& d = grp.baselineDepth[h.obj];
        d = std::max(d, h.depth);
        grp.baselineMismatch = true;
        auto name = fmt::format("{}#{}({:.2f})", killerName(h.obj), h.obj ? h.obj->m_objectID : 0, h.depth);
        if (grp.baseHitNames.find(name) == std::string::npos && grp.baseHitNames.size() < 120) {
            grp.baseHitNames += name + " ";
        }
    }
    grp.baseline.hits.clear();

    forEachGhost(grp, [&](Ghost& g) {
        if (&g == &grp.baseline || g.status != GStatus::Alive) return;
        // wait a few steps (the copy of your run may touch the same thing a bit later)
        for (auto const& h : g.hits) {
            if (t - h.tick < kHitGrace || !isGenuine(h, grp)) continue;
            g.status = GStatus::Dead;
            g.killer = h.obj;
            g.deathTick = h.tick;
            releaseObj(g);
            return;
        }
    });
}

Tracker::RealState Tracker::readState(PlayerObject* p) const {
    RealState s;
    // the node position is the real one; PlayerObject::m_position is only synced by the game layer
    s.x = p->getPositionX();
    s.y = p->getPositionY();
    s.vy = p->m_yVelocity;
    s.onGround = p->m_isOnGround;
    s.upsideDown = p->m_isUpsideDown;
    s.holding = m_realHolding;
    s.modeKey = modeKeyOf(p);
    s.speed = p->m_playerSpeed;
    s.size = p->m_vehicleSize;
    return s;
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
    finishAllGroups(pl, true, true);

    clearLiveLabels();
    m_current = RunRecord{};
    m_current.replay = std::make_shared<std::vector<ReplayFrame>>();
    m_current.replay->reserve(240 * 30);
    if (pl->m_player1) m_current.startX = pl->m_player1->getPositionX();
    m_realDepth.clear();
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
    m_accDt = 0.f;
    m_running = !pl->m_isPlatformer;
    for (auto& s : m_snaps) s.tick = -1;

    // create test players now (at load / respawn) so the first click doesn't stutter
    int warm = std::min(ghostCap(), 2 * static_cast<int>(buildOffsets().size()) + 4);
    while (static_cast<int>(m_pool.size()) < warm && static_cast<int>(m_allObjs.size()) < ghostCap() + 64) {
        auto p = newObj(pl);
        if (!p) break;
        m_pool.push_back(p);
    }
    bump();
}

void Tracker::stopForReplay(PlayLayer* pl) {
    if (m_running) finishRun(pl, false);
    clearLiveLabels();
    finishAllGroups(pl, true, true);
    m_running = false;
    m_pending.clear();
    if (m_drawNode) m_drawNode->clear();
    bump();
}

void Tracker::finishRun(PlayLayer* pl, bool completed) {
    if (!m_running) return;
    finishAllGroups(pl, !completed, true);
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
        for (size_t i = 5; i < m_history.size(); ++i) m_history[i].replay.reset();
        m_showLastRun = true;
    }
    if (m_drawNode) m_drawNode->clear();
    bump();
}

// ---------------------------------------------------------------------------
// real player events

void Tracker::onRealButton(PlayLayer* pl, bool down) {
    if (!m_running || !pl || !pl->m_player1 || pl->m_player1->m_isDead) return;
    if (down == m_realHolding) return;
    m_realHolding = down;

    auto p = pl->m_player1;
    // releases only matter in modes where holding changes your movement;
    // other releases (cube, ball, ufo...) move together with their click
    bool relevant = down || p->m_isShip || p->m_isDart || p->m_isRobot;

    InputRecord rec;
    rec.number = static_cast<int>(m_current.inputs.size()) + 1;
    rec.press = down;
    rec.tick = m_tick;
    rec.time = m_tick;
    rec.percent = pl->getCurrentPercent();
    if (!relevant) rec.state = EvalState::NoEffect;
    m_current.inputs.push_back(rec);
    m_pending.push_back({m_current.inputs.size() - 1, relevant, p->getPositionX()});
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
    Boost b{ring, p->m_yVelocity, p->m_isUpsideDown, isApproxBoost(ring) || p->m_isDashing};
    auto it = std::find_if(m_rings.begin(), m_rings.end(), [&](Boost const& o) { return o.obj == ring; });
    if (it != m_rings.end()) *it = b;
    else m_rings.push_back(b);
    m_boostThisTick = true;
}

void Tracker::onRealPad(GameObject* pad, PlayerObject* p) {
    if (!m_running || !pad) return;
    Boost b{pad, p->m_yVelocity, p->m_isUpsideDown, isApproxBoost(pad)};
    auto it = std::find_if(m_pads.begin(), m_pads.end(), [&](Boost const& o) { return o.obj == pad; });
    if (it != m_pads.end()) *it = b;
    else m_pads.push_back(b);
    m_boostThisTick = true;
}

// ---------------------------------------------------------------------------
// test player physics

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

// Mirrors the game's step: player update, then collision. A step that is split
// by an input works like Click Between Frames does it.
void Tracker::subStep(PlayLayer* pl, Ghost& g, float dt, bool endOfStep) {
    auto obj = g.obj;
    m_simulating = true;
    m_curGhost = &g;
    if (dt > 0.f) obj->update(dt);
    if (endOfStep) {
        // ignoreDamage: the game never "kills" a test player (so MegaHack's hitboxes
        // on death, noclip etc. never react); deaths come from our own hit check
        pl->checkCollisions(obj, m_tickDt, true);
    }
    else {
        pl->checkCollisions(obj, (obj->m_isOnSlope && !obj->m_isDart) ? m_tickDt : 0.f, true);
        resetCollisionLog(obj);
    }
    m_curGhost = nullptr;
    m_simulating = false;
}

void Tracker::applyEvent(Ghost& g, Event const& ev) {
    auto obj = g.obj;
    if (!obj) return;
    m_simulating = true;
    m_curGhost = &g;
    if (ev.press) {
        obj->pushButton(PlayerButton::Jump);
        g.holding = true;
        // holding into an orb can activate it, except as ship / wave
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
    m_stepTick = tick;
    resetCollisionLog(g.obj);
    float dt = m_tickDt;
    double pos = 0.0;
    CCPoint stepStart = g.obj->getPosition();
    bool split = false;
    while (!g.events.empty() && g.events.front().time < tick + 1 - 1e-9) {
        Event ev = g.events.front();
        g.events.erase(g.events.begin());
        double f = std::clamp(ev.time - tick, 0.0, 1.0);
        if (f > pos + 1e-6) {
            subStep(pl, g, static_cast<float>((f - pos) * dt), false);
            pos = f;
            split = true;
            if (g.status != GStatus::Alive) return;
        }
        applyEvent(g, ev);
    }
    subStep(pl, g, static_cast<float>(std::max(0.0, 1.0 - pos) * dt), true);
    if (g.status != GStatus::Alive || !g.obj) return;
    // the game ends every step with the rotation update
    m_simulating = true;
    m_curGhost = &g;
    g.obj->updateRotation(dt);
    if (split) g.obj->m_lastPosition = stepStart;
    m_curGhost = nullptr;
    m_simulating = false;
    tryPad(g);
    if (g.holding && g.ringPending) tryRing(g);
    if (g.obj->m_isOnGround) g.ringPending = false;
}

void Tracker::insertEvent(Ghost& g, Event const& ev) {
    auto it = std::upper_bound(g.events.begin(), g.events.end(), ev,
        [](Event const& a, Event const& b) { return a.time < b.time; });
    g.events.insert(it, ev);
}

// real inputs (other than the measured one) that happened in [from, to)
void Tracker::addPastInputs(Ghost& g, size_t beforeIdx, double from, double to) {
    for (size_t i = beforeIdx; i-- > 0;) {
        auto const& in = m_current.inputs[i];
        if (in.time < from - 1e-9) break;
        if (in.time < to - 1e-9) insertEvent(g, {in.time, in.press, false});
    }
}

bool Tracker::spawnFromSnapshot(PlayLayer* pl, Ghost& g, int tick) {
    auto snap = snapshotAt(tick);
    if (!snap || !snap->obj) return false;
    auto obj = acquireObj(pl);
    if (!obj) return false;
    copyState(obj, snap->obj, snap->holding);
    g.obj = obj;
    g.holding = snap->holding;
    g.status = GStatus::Alive;
    return true;
}

bool Tracker::spawnFromGhost(PlayLayer* pl, Ghost& g, Ghost& src) {
    if (!src.obj) return false;
    auto obj = acquireObj(pl);
    if (!obj) return false;
    copyState(obj, src.obj, src.holding);
    g.obj = obj;
    g.holding = src.holding;
    g.ringPending = src.ringPending;
    g.usedBoosts = src.usedBoosts;
    g.approxBoost = src.approxBoost;
    g.status = GStatus::Alive;
    return true;
}

// ---------------------------------------------------------------------------
// measurement groups (one per input)

void Tracker::createGroup(PlayLayer* pl, size_t idx, int t) {
    auto& rec = m_current.inputs[idx];
    if (m_tickDt <= 0.f || !snapshotAt(t)) {
        rec.state = EvalState::Unknown;
        return;
    }

    auto offs = buildOffsets();
    // keep CPU use bounded: test fewer shifts if a lot of test players are running
    int available = ghostCap() - liveGhostCount();
    while (static_cast<int>(offs.size()) * 2 + 2 > available && offs.size() > 4) {
        std::vector<double> thin;
        for (size_t i = 0; i < offs.size(); ++i) {
            if (i % 2 == 1 || i + 1 == offs.size()) thin.push_back(offs[i]);
        }
        offs.swap(thin);
    }

    m_groups.emplace_back();
    Group& grp = m_groups.back();
    grp.inputIdx = idx;
    grp.T0 = rec.time;
    grp.press = rec.press;
    grp.fpt = m_fpt;
    grp.offFrames = offs;
    grp.prevTime = idx > 0 ? m_current.inputs[idx - 1].time : -1e18;
    grp.startTick = t;
    grp.horizonTick = t + static_cast<int>(std::ceil(horizonSeconds() * 240.0 / m_fpt));
    grp.early.resize(offs.size());
    grp.late.resize(offs.size());

    // control: the input never happens
    grp.control.isControl = true;
    if (spawnFromSnapshot(pl, grp.control, t)) addPastInputs(grp.control, idx, t, grp.T0);

    // baseline: the input exactly when you did it
    grp.baseline.fireTime = grp.T0;
    if (spawnFromSnapshot(pl, grp.baseline, t)) {
        addPastInputs(grp.baseline, idx, t, grp.T0);
        insertEvent(grp.baseline, {grp.T0, grp.press, true});
    }

    // earlier
    bool earlyDead = false;
    for (size_t k = 0; k < offs.size(); ++k) {
        auto& g = grp.early[k];
        g.offTicks = -offs[k] / grp.fpt;
        g.fireTime = grp.T0 + g.offTicks;
        if (earlyDead) {
            g.status = GStatus::Pruned;
            continue;
        }
        int s = static_cast<int>(std::floor(g.fireTime + 1e-9));
        if (g.fireTime <= grp.prevTime + 1e-9 || !snapshotAt(s) || s > t) {
            g.status = GStatus::Bounded;
            earlyDead = true; // nothing further out is valid either
            continue;
        }
        if (!spawnFromSnapshot(pl, g, s)) {
            g.status = GStatus::Bounded;
            earlyDead = true;
            continue;
        }
        addPastInputs(g, idx, s, g.fireTime);
        insertEvent(g, {g.fireTime, grp.press, true});
        for (int k2 = s; k2 < t && g.status == GStatus::Alive; ++k2) stepGhost(pl, g, k2);
        // you already went through this stretch, so anything you touched there is safe
        for (auto const& h : g.hits) {
            if (t - h.tick < kHitGrace || !isGenuine(h, grp)) continue;
            g.status = GStatus::Dead;
            g.killer = h.obj;
            g.deathTick = h.tick;
            releaseObj(g);
            earlyDead = true;
            break;
        }
    }

    // later (spawned when their time comes, from the control)
    for (size_t k = 0; k < offs.size(); ++k) {
        auto& g = grp.late[k];
        g.offTicks = offs[k] / grp.fpt;
        g.fireTime = grp.T0 + g.offTicks;
        g.status = GStatus::NotSpawned;
    }
}

void Tracker::spawnLateGhosts(PlayLayer* pl, Group& grp, int t) {
    bool sideDead = false;
    for (auto& g : grp.late) {
        if (g.status == GStatus::Dead || g.status == GStatus::Bounded) sideDead = true;
        if (g.status != GStatus::NotSpawned) continue;
        if (sideDead) {
            g.status = GStatus::Pruned;
            continue;
        }
        int s = static_cast<int>(std::floor(g.fireTime + 1e-9));
        if (s > t) break;
        if (grp.sawNextRelevant && g.fireTime >= grp.nextRelevant - 1e-9) {
            g.status = GStatus::Bounded;
            sideDead = true;
            continue;
        }
        bool ok = false;
        if (s == static_cast<int>(std::floor(grp.T0 + 1e-9)) && snapshotAt(s)) {
            // same step as your input: start from the saved state
            ok = spawnFromSnapshot(pl, g, s);
            if (ok) addPastInputs(g, grp.inputIdx, s, grp.T0);
        }
        else if (grp.control.status == GStatus::Alive) {
            // until it does its input, it's the same as the control
            ok = spawnFromGhost(pl, g, grp.control);
            if (ok) {
                for (auto const& ev : grp.control.events) insertEvent(g, ev);
                g.hits = grp.control.hits;
            }
        }
        else if (grp.control.status == GStatus::Dead) {
            // without the input you die before this shifted time: fails on the same obstacle
            g.status = GStatus::Dead;
            g.killer = grp.control.killer;
            g.deathTick = grp.control.deathTick;
            sideDead = true;
            continue;
        }
        if (!ok) {
            g.status = GStatus::Bounded;
            sideDead = true;
            continue;
        }
        insertEvent(g, {g.fireTime, grp.press, true});
        for (auto const& a : grp.attached) insertEvent(g, {a.time + g.offTicks, a.press, false});
    }
}

// a later real input reaches the running measurements
void Tracker::feedInput(PlayLayer*, size_t idx, bool relevant, int t) {
    auto const& rec = m_current.inputs[idx];
    for (auto& grp : m_groups) {
        if (grp.done || grp.inputIdx >= idx) continue;
        bool attached = !grp.sawNextRelevant && !relevant;
        if (!grp.sawNextRelevant && relevant) {
            grp.sawNextRelevant = true;
            grp.nextRelevant = rec.time;
            // shifting an input past the next one would reorder them
            for (auto& g : grp.late) {
                if (g.fireTime < rec.time - 1e-9) continue;
                if (g.status == GStatus::NotSpawned || (g.status == GStatus::Alive && !g.fired)) {
                    g.status = GStatus::Bounded;
                    releaseObj(g);
                }
            }
        }
        if (attached) grp.attached.push_back({rec.time, rec.press, false});

        // look ahead: a shifted input must also get you past the obstacles around
        // your next 2 inputs (with them kept where you did them), then the
        // measurement can end - no need to drift on through open space
        if (relevant && grp.inputsAhead < 2) {
            ++grp.inputsAhead;
            if (grp.inputsAhead == 2) {
                int ticksPerSec = static_cast<int>(240.0 / grp.fpt);
                int end = std::max(grp.startTick + ticksPerSec / 4, t + ticksPerSec / 4);
                grp.horizonTick = std::min(grp.horizonTick, end);
            }
        }

        forEachGhost(grp, [&](Ghost& g) {
            if (g.status != GStatus::Alive) return;
            if (attached && g.isControl) return; // control never did this click, so not its release either
            double time = attached ? rec.time + g.offTicks : rec.time;
            insertEvent(g, {std::max<double>(time, t), rec.press, false});
        });
    }
}

void Tracker::evaluateGroup(PlayLayer* pl, Group& grp, RealState const& cur, int t) {
    auto real = pl->m_player1;

    forEachGhost(grp, [&](Ghost& g) {
        if (g.status == GStatus::Dead && g.obj) releaseObj(g);
        if (g.status != GStatus::Alive) return;
        auto o = g.obj;
        auto opos = o->getPosition();
        bool same = std::abs(opos.x - cur.x) < kConvergeX &&
                    std::abs(opos.y - cur.y) < kConvergeY &&
                    std::abs(o->m_yVelocity - cur.vy) < kConvergeVy &&
                    o->m_isOnGround == cur.onGround &&
                    o->m_isUpsideDown == cur.upsideDown &&
                    g.holding == cur.holding &&
                    modeKeyOf(o) == cur.modeKey;
        if (&g == &grp.baseline) {
            float dx = std::abs(opos.x - cur.x), dy = std::abs(opos.y - cur.y);
            grp.baseMaxDx = std::max(grp.baseMaxDx, dx);
            grp.baseMaxDy = std::max(grp.baseMaxDy, dy);
            if (!same && (dy > 2.f || dx > 2.f)) grp.baselineMismatch = true;
        }
        bool pendingHit = false;
        for (auto const& h : g.hits) if (isGenuine(h, grp)) pendingHit = true;
        if ((g.fired || g.isControl) && same && !pendingHit) {
            if (++g.converged >= 2) {
                g.status = GStatus::Converged;
                releaseObj(g);
            }
        }
        else {
            g.converged = 0;
        }
    });
    (void)real;

    // a shift that fails makes every bigger shift on that side fail too
    for (auto* side : {&grp.early, &grp.late}) {
        bool dead = false;
        for (auto& g : *side) {
            if (dead && (g.status == GStatus::Alive || g.status == GStatus::NotSpawned)) {
                g.status = GStatus::Pruned;
                releaseObj(g);
            }
            if (g.status == GStatus::Dead || g.status == GStatus::Bounded) dead = true;
        }
    }

    bool noEffect = grp.control.status == GStatus::Converged;
    bool baselineDead = false;
    bool allDone = grp.baseline.status != GStatus::Alive;
    for (auto* side : {&grp.early, &grp.late}) {
        for (auto& g : *side) {
            if (g.status == GStatus::Alive || g.status == GStatus::NotSpawned) allDone = false;
        }
    }
    if (noEffect || baselineDead || allDone || t >= grp.horizonTick) {
        finishGroup(pl, grp, false, false);
    }
}

void Tracker::finishGroup(PlayLayer* pl, Group& grp, bool truncated, bool interrupted) {
    if (grp.done) return;
    bool truncatedByDeath = truncated;
    grp.done = true;
    auto& rec = m_current.inputs[grp.inputIdx];
    rec.passedPercent = pl ? pl->getCurrentPercent() : rec.percent;

    if (grp.control.status == GStatus::Converged) {
        rec.state = EvalState::NoEffect;
    }
    else {
        struct SideResult {
            double edge = 0.0;
            bool capped = false;
            bool spacing = false;
            GameObject* killer = nullptr;
            bool died = false;
            bool untested = false;
        };
        auto scan = [&](std::vector<Ghost>& side, bool isLate) {
            SideResult r;
            int lastOk = -1;
            for (size_t k = 0; k < side.size(); ++k) {
                auto& g = side[k];
                bool genuineHit = false;
                for (auto const& h : g.hits) {
                    if (isGenuine(h, grp)) {
                        genuineHit = true;
                        if (!g.killer) g.killer = h.obj;
                    }
                }
                if (genuineHit && g.status == GStatus::Alive) g.status = GStatus::Dead;
                bool ok = g.status == GStatus::Alive || g.status == GStatus::Converged;
                if (ok) {
                    lastOk = static_cast<int>(k);
                    if (k + 1 == side.size()) r.capped = true;
                    continue;
                }
                double inner = lastOk >= 0 ? grp.offFrames[lastOk] : 0.0;
                if (g.status == GStatus::Dead) {
                    r.died = true;
                    r.killer = g.killer;
                    r.edge = (inner + grp.offFrames[k]) * 0.5;
                }
                else if (g.status == GStatus::Bounded) {
                    r.spacing = true;
                    double gap = isLate ? (grp.nextRelevant - grp.T0) : (grp.T0 - grp.prevTime);
                    double gapFrames = gap * grp.fpt;
                    r.edge = std::clamp(gapFrames, inner, grp.offFrames[k]);
                }
                else {
                    // not tested (run ended / portal): only count what was confirmed
                    r.edge = inner;
                    r.untested = true;
                    truncated = true;
                }
                return r;
            }
            r.edge = grp.offFrames.empty() ? 0.0 : grp.offFrames.back();
            return r;
        };
        auto e = scan(grp.early, false);
        auto l = scan(grp.late, true);

        rec.earlyFrames = static_cast<float>(e.edge);
        rec.lateFrames = static_cast<float>(l.edge);
        rec.windowFrames = static_cast<float>(e.edge + l.edge);
        // no obstacle on either side (only your neighbouring clicks): that's open space
        bool eOpen = e.capped || (e.spacing && !e.died);
        bool lOpen = l.capped || (l.spacing && !l.died);
        rec.cappedEarly = eOpen;
        rec.cappedLate = lOpen;
        if (eOpen != lOpen) {
            // one side open: count it as the max so the window shows the other side's limit
            if (eOpen) rec.earlyFrames = static_cast<float>(std::max(e.edge, grp.offFrames.back()));
            if (lOpen) rec.lateFrames = static_cast<float>(std::max(l.edge, grp.offFrames.back()));
            rec.cappedEarly = e.capped;
            rec.cappedLate = l.capped;
        }
        rec.windowFrames = rec.earlyFrames + rec.lateFrames;
        rec.truncated = truncated;
        rec.approx = grp.baselineMismatch;
        forEachGhost(grp, [&](Ghost& g) { if (g.approxBoost && g.status != GStatus::Dead) rec.approx = true; });

        // what limited the window: the tighter side
        SideResult const* tight = nullptr;
        if (!eOpen && !lOpen) tight = e.edge <= l.edge ? &e : &l;
        else if (!eOpen) tight = &e;
        else if (!lOpen) tight = &l;
        if (tight) {
            if (tight->died) {
                rec.obstacle = killerName(tight->killer);
                if (tight->killer && pl && pl->m_levelLength > 0.f) {
                    rec.obstaclePercent = std::clamp(tight->killer->getPositionX() / pl->m_levelLength * 100.f, 0.f, 100.f);
                }
            }
            else if (tight->spacing) {
                rec.obstacle = "click spacing";
            }
            else if (tight->untested) {
                rec.obstacle = interrupted && !truncatedByDeath ? "portal (cut short)" : "cut short";
            }
        }
        rec.state = EvalState::Done;
        log::info("#{} {} at {:.1f}%: window {:.2f} (early {:.2f}{}{}, late {:.2f}{}{}) obstacle '{}' approx={} drift dx={:.2f} dy={:.2f} basehits=[{}] mode={} fpt={:.3f} shifts={}",
            rec.number, rec.press ? "click" : "release", rec.percent, rec.windowFrames,
            e.edge, e.capped ? " max" : "", e.spacing ? " spacing" : "", l.edge, l.capped ? " max" : "", l.spacing ? " spacing" : "",
            rec.obstacle, rec.approx, grp.baseMaxDx, grp.baseMaxDy, grp.baseHitNames,
            pl && pl->m_player1 ? modeKeyOf(pl->m_player1) : -1, grp.fpt, grp.offFrames.size());
    }

    forEachGhost(grp, [&](Ghost& g) { releaseObj(g); });
    finishLiveLabel(grp.inputIdx);
    bump();
}

void Tracker::finishAllGroups(PlayLayer* pl, bool truncated, bool interrupted) {
    for (auto& grp : m_groups) finishGroup(pl, grp, truncated, interrupted);
    m_groups.clear();
}

// ---------------------------------------------------------------------------
// per physics step

void Tracker::processTick(PlayLayer* pl, int t) {
    auto real = pl->m_player1;
    RealState cur = readState(real);

    // lock everything to 240 FPS frames of real time: GD's step delta is in
    // 1/60 s units and is scaled by time warp
    if (m_accDt > 0.f) m_tickDt = m_accDt;
    float tw = std::max(pl->m_gameState.m_timeWarp, 0.01f);
    if (m_tickDt > 0.f) {
        double secs = m_tickDt > 0.02f ? m_tickDt / 60.0 : m_tickDt;
        m_fpt = std::clamp(240.0 * secs / tw, 0.05, 8.0);
    }

    // where inside the step each input happened (Click Between Frames splits steps)
    for (auto const& p : m_pending) {
        auto& rec = m_current.inputs[p.idx];
        float dx = cur.x - m_tickStartX;
        double f = std::abs(dx) > 1e-3f ? std::clamp<double>((p.x - m_tickStartX) / dx, 0.0, 0.999) : 0.0;
        rec.time = t + f;
    }

    // portals change things the test players can't copy: finish what's running
    bool inputThisTick = !m_pending.empty();
    if (m_hasPrevReal) {
        bool portal = cur.modeKey != m_prevReal.modeKey ||
                      std::abs(cur.speed - m_prevReal.speed) > 0.001f ||
                      std::abs(cur.size - m_prevReal.size) > 0.001f ||
                      std::abs(cur.x - m_prevReal.x) > 60.f;
        bool flipped = cur.modeKey == m_prevReal.modeKey && cur.upsideDown != m_prevReal.upsideDown;
        if (flipped && !inputThisTick && !m_boostThisTick && !real->m_isBall && !real->m_isSwing && !real->m_isSpider) {
            portal = true;
        }
        if (portal) finishAllGroups(pl, false, true);
    }
    m_boostThisTick = false;

    bool liveLabels = Mod::get()->getSettingValue<bool>("show-level-labels");
    for (auto const& p : m_pending) {
        feedInput(pl, p.idx, p.relevant, t);
        if (p.relevant) {
            if (liveLabels) spawnLiveLabel(pl, p.idx, m_tickStartX + (cur.x - m_tickStartX) * static_cast<float>(m_current.inputs[p.idx].time - t), cur.y);
            createGroup(pl, p.idx, t);
            if (m_current.inputs[p.idx].state != EvalState::Pending) finishLiveLabel(p.idx);
        }
    }
    m_pending.clear();

    for (auto& grp : m_groups) {
        if (!grp.done) spawnLateGhosts(pl, grp, t);
    }
    for (auto& grp : m_groups) {
        if (grp.done) continue;
        forEachGhost(grp, [&](Ghost& g) { stepGhost(pl, g, t); });
    }
    for (auto& grp : m_groups) {
        if (grp.done) continue;
        resolveHits(grp, t);
        evaluateGroup(pl, grp, cur, t);
    }
    while (!m_groups.empty() && m_groups.front().done) m_groups.pop_front();
    if (m_groups.size() > 64) {
        std::deque<Group> keep;
        for (auto& g : m_groups) if (!g.done) keep.push_back(std::move(g));
        m_groups.swap(keep);
    }

    drawDebug(pl);

    if (m_current.replay) {
        auto& frames = *m_current.replay;
        if (static_cast<int>(frames.size()) == t) frames.push_back({cur.x, cur.y, cur.vy});
    }

    m_prevReal = cur;
    m_hasPrevReal = true;
}

void Tracker::drawDebug(PlayLayer* pl) {
    bool show = Mod::get()->getSettingValue<bool>("show-test-players");
    if (!show) {
        if (m_drawNode) m_drawNode->clear();
        return;
    }
    if (!m_drawNode) {
        m_drawNode = CCDrawNode::create();
        m_drawNode->retain();
        if (pl->m_objectLayer) pl->m_objectLayer->addChild(m_drawNode, 1000);
    }
    m_drawNode->clear();
    for (auto& grp : m_groups) {
        if (grp.done) continue;
        forEachGhost(grp, [&](Ghost& g) {
            if (g.status != GStatus::Alive || !g.obj) return;
            auto r = g.obj->getObjectRect();
            CCPoint pts[4] = {
                {r.getMinX(), r.getMinY()}, {r.getMinX(), r.getMaxY()},
                {r.getMaxX(), r.getMaxY()}, {r.getMaxX(), r.getMinY()},
            };
            ccColor4F col = g.isControl ? ccColor4F{1.f, 1.f, 1.f, 0.6f}
                          : g.offTicks < 0 ? ccColor4F{0.3f, 0.8f, 1.f, 0.6f}
                          : g.offTicks > 0 ? ccColor4F{1.f, 0.6f, 0.2f, 0.6f}
                                           : ccColor4F{0.3f, 1.f, 0.3f, 0.8f};
            m_drawNode->drawPolygon(pts, 4, ccColor4F{0.f, 0.f, 0.f, 0.f}, 0.4f, col);
        });
    }
}

void Tracker::onTickStart(PlayLayer* pl) {
    if (!m_running || !pl) return;
    auto real = pl->m_player1;
    if (!real || real->m_isDead) return;

    if (m_tick > 0) processTick(pl, m_tick - 1);
    if (!m_running) return;
    m_accDt = 0.f;

    // keep enough saved states to move an input max-window frames earlier
    size_t need = static_cast<size_t>(std::ceil(maxFrames() / m_fpt)) + 4;
    if (m_snaps.size() < need) {
        for (auto& s : m_snaps) s.tick = -1; // indexing changes
        while (m_snaps.size() < need) {
            Snapshot s;
            s.obj = acquireObj(pl);
            if (!s.obj) break;
            m_snaps.push_back(s);
        }
    }

    // save the state at the start of this step
    if (!m_snaps.empty()) {
        auto& snap = m_snaps[m_tick % m_snaps.size()];
        if (snap.obj) {
            copyState(snap.obj, real, m_realHolding);
            snap.tick = m_tick;
            snap.holding = m_realHolding;
        }
    }
    m_tickStartX = real->getPositionX();
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
    if (w < 1.f) return {255, 60, 220};   // 0-1: needs more than 240 fps
    if (w < 2.f) return {255, 70, 70};    // 1-2
    if (w < 3.f) return {255, 150, 50};   // 2-3
    if (w < 4.f) return {255, 230, 60};   // 3-4
    if (w < 6.f) return {150, 255, 80};   // 4-6
    if (w < 10.f) return {80, 220, 255};  // 6-10
    return {200, 200, 200};
}

} // namespace fw
