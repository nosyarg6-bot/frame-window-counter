#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class PlayLayer;
class PlayerObject;
class GameObject;

namespace fw {

enum class EvalState {
    Pending,   // test players are still going through the obstacle
    Done,      // window measured
    NoEffect,  // the input did not change anything (e.g. a mid-air click)
    Unknown,   // could not be measured
};

struct InputRecord {
    int number = 0;            // 1-based index within the run
    bool press = true;         // true = click, false = release
    int tick = 0;              // physics step the input happened on
    double time = 0.0;         // step + fraction of the step (Click Between Frames)
    float percent = 0.f;       // level percent at the input
    float passedPercent = 0.f; // level percent when the measurement finished
    EvalState state = EvalState::Pending;

    float earlyFrames = 0.f;   // 240fps frames it could have been earlier
    float lateFrames = 0.f;    // 240fps frames it could have been later
    float windowFrames = 0.f;  // total window in 240fps frames
    bool cappedEarly = false;  // more than the max window
    bool cappedLate = false;
    bool truncated = false;    // you died before it could be fully measured
    bool approx = false;       // relied on orb/pad copying or the simulation drifted

    std::string obstacle;      // what limited the window: "spike", "block", "click spacing"...
    float obstaclePercent = -1.f;

    bool isFree() const { return cappedEarly && cappedLate; }
};

struct ReplayFrame {
    float x = 0.f;
    float y = 0.f;
    double vy = 0.0;
};

struct RunRecord {
    std::shared_ptr<std::vector<ReplayFrame>> replay; // player state at the end of every step
    float startX = 0.f;
    std::string levelName;
    int attempt = 0;
    float endPercent = 0.f;
    bool completed = false;
    bool practice = false;
    bool finished = false;
    std::vector<InputRecord> inputs;

    int effectiveCount() const;
    InputRecord const* hardest() const;
};

class Tracker {
public:
    static Tracker& get();

    // level lifecycle
    void beginLevel(PlayLayer* pl);
    void endLevel(PlayLayer* pl);
    void startRun(PlayLayer* pl);
    void finishRun(PlayLayer* pl, bool completed);

    // called around every physics step
    void onTickStart(PlayLayer* pl);
    void onTickEnd(PlayLayer* pl);

    // real player 1 events
    void onRealButton(PlayLayer* pl, bool down);
    void onRealUpdate(float dt) { if (dt > 0.f) m_accDt += dt; }
    void onRealRing(GameObject* ring, PlayerObject* p, double vyBefore, bool upBefore, bool dashBefore);
    void onRealPad(GameObject* pad, PlayerObject* p);

    // test player simulation (used by the side-effect hooks)
    bool isSimulating() const { return m_simulating; }
    bool isGhost(PlayerObject* p) const { return p && m_ghostSet.count(p); }
    void onGhostDeath(PlayerObject* p, GameObject* killer);
    // own hit detection on the exact object list the game checks (works even if
    // another mod swallows deaths, e.g. noclip)
    void onCollisionList(PlayerObject* p, gd::vector<GameObject*>* objs, int count);
    void stopForReplay(PlayLayer* pl);

    // data for the UI
    RunRecord const& current() const { return m_current; }
    std::deque<RunRecord> const& history() const { return m_history; }
    bool hasRunInProgress() const { return m_running; }
    int revision() const { return m_revision; }
    bool showingLastRun() const { return m_showLastRun; }

    struct Event {
        double time = 0.0;
        bool press = true;
        bool own = false; // the shifted input being measured
    };

private:
    enum class GStatus { NotSpawned, Alive, Dead, Converged, Bounded, Pruned };

    struct Ghost {
        PlayerObject* obj = nullptr;
        double offTicks = 0.0;   // shift in physics steps (negative = earlier)
        double fireTime = 0.0;   // when the shifted input happens
        GStatus status = GStatus::NotSpawned;
        bool isControl = false;  // never does the input (detects "no effect")
        bool fired = false;
        bool holding = false;
        bool ringPending = false;
        bool approxBoost = false;
        int converged = 0;
        int deathTick = -1;
        GameObject* killer = nullptr;
        std::vector<std::pair<GameObject*, int>> hits; // possible deaths, confirmed a few steps later
        std::vector<Event> events;
        std::vector<GameObject*> usedBoosts;
    };

    struct Group {
        size_t inputIdx = 0;
        double T0 = 0.0;
        bool press = true;
        double fpt = 1.0;              // 240fps frames per physics step
        std::vector<double> offFrames; // tested shifts, in frames, ascending
        Ghost control;
        Ghost baseline;
        std::vector<Ghost> early;
        std::vector<Ghost> late;
        double prevTime = -1e18;
        bool sawNextRelevant = false;
        double nextRelevant = 1e18;
        std::vector<Event> attached;   // inputs that move along with this one (e.g. its cube release)
        int horizonTick = 0;
        bool baselineMismatch = false;
        bool done = false;
        std::unordered_set<GameObject*> baselineHits; // things the copy of your exact run "hit"
    };

    struct Snapshot {
        PlayerObject* obj = nullptr;
        int tick = -1;
        bool holding = false;
    };

    struct PendingInput {
        size_t idx;
        bool relevant;
        float x;
    };

    struct Boost {
        GameObject* obj = nullptr;
        double vy = 0.0;
        bool upsideDown = false;
        bool approx = false;
    };

    struct RealState {
        float x = 0.f, y = 0.f;
        double vy = 0.0;
        bool onGround = false;
        bool upsideDown = false;
        bool holding = false;
        int modeKey = 0;
        float speed = 0.f;
        float size = 1.f;
    };

    template <class F> static void forEachGhost(Group& g, F&& f) {
        f(g.control);
        f(g.baseline);
        for (auto& x : g.early) f(x);
        for (auto& x : g.late) f(x);
    }

    PlayerObject* acquireObj(PlayLayer* pl);
    PlayerObject* newObj(PlayLayer* pl);
    void releaseObj(Ghost& g);
    void hidePlayer(PlayerObject* p);
    void destroyPool();
    void copyState(PlayerObject* dst, PlayerObject* src, bool holding);
    Snapshot* snapshotAt(int tick);

    bool spawnFromSnapshot(PlayLayer* pl, Ghost& g, int tick);
    bool spawnFromGhost(PlayLayer* pl, Ghost& g, Ghost& src);
    void addPastInputs(Ghost& g, size_t beforeIdx, double from, double to);
    static void insertEvent(Ghost& g, Event const& ev);

    void stepGhost(PlayLayer* pl, Ghost& g, int tick);
    void subStep(PlayLayer* pl, Ghost& g, float dt, bool endOfStep);
    void applyEvent(Ghost& g, Event const& ev);
    void tryRing(Ghost& g);
    void tryPad(Ghost& g);

    void processTick(PlayLayer* pl, int t);
    void feedInput(PlayLayer* pl, size_t idx, bool relevant, int t);
    void createGroup(PlayLayer* pl, size_t idx, int t);
    void spawnLateGhosts(PlayLayer* pl, Group& grp, int t);
    void evaluateGroup(PlayLayer* pl, Group& grp, RealState const& cur, int t);
    void finishGroup(PlayLayer* pl, Group& grp, bool truncated, bool interrupted);
    void finishAllGroups(PlayLayer* pl, bool truncated, bool interrupted);
    RealState readState(PlayerObject* p) const;
    int liveGhostCount() const;
    std::vector<double> buildOffsets() const;
    void drawDebug(PlayLayer* pl);
    void resolveHits(Group& grp, int t);
    static bool hitsPlayer(PlayerObject* p, GameObject* obj);
    void bump() { ++m_revision; }

    RunRecord m_current;
    std::deque<RunRecord> m_history;

    std::vector<PlayerObject*> m_pool;
    std::vector<PlayerObject*> m_allObjs;
    std::unordered_set<PlayerObject*> m_ghostSet;
    std::vector<Snapshot> m_snaps;
    std::deque<Group> m_groups;
    std::vector<PendingInput> m_pending;
    std::vector<Boost> m_rings;
    std::vector<Boost> m_pads;
    std::unordered_set<GameObject*> m_realFalseHits; // objects the hit check flags but you survived
    cocos2d::CCDrawNode* m_drawNode = nullptr;

    RealState m_prevReal;
    bool m_hasPrevReal = false;
    float m_tickStartX = 0.f;
    int m_tick = 0;
    float m_accDt = 0.f;
    float m_tickDt = 0.f;
    double m_fpt = 1.0;
    bool m_running = false;
    bool m_realHolding = false;
    bool m_boostThisTick = false;
    bool m_simulating = false;
    bool m_showLastRun = false;
    Ghost* m_curGhost = nullptr;
    int m_stepTick = 0;
    int m_revision = 0;
};

std::string windowLabel(InputRecord const& r);
cocos2d::ccColor3B windowColor(InputRecord const& r);

} // namespace fw
