#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <string>
#include <vector>

class PlayLayer;
class PlayerObject;
class GameObject;

namespace fw {

enum class EvalState {
    Pending,   // ghosts are still flying through the obstacle
    Done,      // window measured
    NoEffect,  // the input did not change anything (e.g. releasing as the cube)
    Unknown,   // could not be measured
};

struct InputRecord {
    int number = 0;            // 1-based index within the run
    bool press = true;         // true = click, false = release
    int tick = 0;              // physics tick the input happened on
    float percent = 0.f;       // level percent at the input
    float passedPercent = 0.f; // level percent when the measurement finished (obstacle passed)
    EvalState state = EvalState::Pending;

    float earlyFrames = 0.f;   // how many 240fps frames earlier it still works
    float lateFrames = 0.f;    // how many 240fps frames later it still works
    float windowFrames = 0.f;  // total window
    bool cappedEarly = false;  // hit the max window limit
    bool cappedLate = false;
    bool truncated = false;    // you died before the measurement finished
    bool approx = false;       // the simulation did not perfectly reproduce your real run

    bool isFree() const { return cappedEarly && cappedLate; }
};

struct RunRecord {
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

    // called around every physics tick
    void onTickStart(PlayLayer* pl);
    void onTickEnd(PlayLayer* pl);

    // real player 1 events
    void onRealButton(PlayLayer* pl, bool down);
    void onRealUpdate(float dt) { if (dt > 0.f) m_dt = dt; }
    void onRealRing(GameObject* ring, PlayerObject* p, double vyBefore, bool upBefore, bool dashBefore);
    void onRealPad(GameObject* pad, PlayerObject* p);

    // ghost simulation state (used by the side-effect hooks)
    bool isSimulating() const { return m_simulating; }
    bool isGhost(PlayerObject* p) const;
    void onGhostDeath(PlayerObject* p);

    // data for the UI
    RunRecord const& current() const { return m_current; }
    std::deque<RunRecord> const& history() const { return m_history; }
    bool hasRunInProgress() const { return m_running; }
    int revision() const { return m_revision; }
    bool showingLastRun() const { return m_showLastRun; }

private:
    struct Boost {
        GameObject* obj = nullptr;
        double vy = 0.0;
        bool upsideDown = false;
        int tick = 0;
        bool approx = false;     // dash / spider / teleport style objects
    };

    struct Event {
        double time = 0.0;       // absolute tick time (fractional = between ticks)
        bool press = true;
        bool own = false;        // the shifted input this group is measuring
    };

    enum class GStatus { Alive, Dead, Converged, Pruned, Bounded };

    struct Ghost {
        PlayerObject* obj = nullptr;
        double offset = 0.0;     // ticks, negative = earlier
        bool control = false;    // never performs the input (detects "no effect")
        bool baseline = false;   // offset 0 (should reproduce the real run)
        bool fired = false;
        bool holding = false;
        bool ringPending = false;
        GStatus status = GStatus::Alive;
        int deathTick = 0;
        float deathX = 0.f;
        int convergedTicks = 0;
        std::vector<Event> events;
        std::vector<GameObject*> usedBoosts;
        bool approxBoost = false;
    };

    struct Group {
        size_t inputIdx = 0;
        int tick = 0;
        bool press = true;
        double res = 0.0;        // offset step in ticks
        int maxK = 0;
        int horizon = 0;
        bool baselineMismatch = false;
        int baselineDeathTick = -1;
        std::vector<Ghost> ghosts;
    };

    struct RealState {
        float x = 0.f, y = 0.f;
        double vy = 0.0;
        bool onGround = false;
        bool holding = false;
        int modeKey = 0;
        float speed = 0.f;
        float size = 1.f;
    };

    struct Snapshot {
        PlayerObject* obj = nullptr;
        int tick = -1;
        bool holding = false;
        bool jumpBuffered = false;
    };

    struct PendingInput {
        bool press;
        bool relevant;
        size_t inputIdx;
    };

    PlayerObject* acquireGhost(PlayLayer* pl);
    void releaseGhost(Ghost& g);
    void copyState(PlayerObject* dst, PlayerObject* src, bool holding, bool jumpBuffered);
    void hidePlayer(PlayerObject* p);
    void destroyPool();

    void processTick(PlayLayer* pl, int t);
    void createGroup(PlayLayer* pl, PendingInput const& in, int t);
    void stepGhost(PlayLayer* pl, Ghost& g, int tick);
    void physStep(PlayLayer* pl, Ghost& g, float dt, int tick);
    void applyEvent(PlayLayer* pl, Ghost& g, Event const& ev);
    void tryRing(Ghost& g);
    void tryPad(Ghost& g);
    void prune(Group& grp);
    void finishGroup(PlayLayer* pl, Group& grp, bool truncated);
    void finishAllGroups(PlayLayer* pl, bool truncated);
    RealState readState(PlayerObject* p, bool holding) const;
    int liveGhostCount() const;
    void bump() { ++m_revision; }

    RunRecord m_current;
    std::deque<RunRecord> m_history;

    std::vector<PlayerObject*> m_pool;      // free ghost players
    std::vector<PlayerObject*> m_allGhosts; // every ghost player we created
    std::vector<Snapshot> m_snaps;
    std::vector<Group> m_groups;
    std::vector<PendingInput> m_pending;
    std::vector<Boost> m_rings;
    std::vector<Boost> m_pads;

    RealState m_prevReal;
    bool m_hasPrevReal = false;
    bool m_prevUpsideDown = false;
    int m_tick = 0;
    float m_dt = 0.f;
    bool m_running = false;
    bool m_realHolding = false;
    bool m_boostThisTick = false;
    int m_stepTick = 0;
    bool m_simulating = false;
    bool m_showLastRun = false;
    Ghost* m_curGhost = nullptr;
    int m_lastInputTick = -1000000;
    int m_revision = 0;
};

std::string windowLabel(InputRecord const& r);
cocos2d::ccColor3B windowColor(InputRecord const& r);

} // namespace fw
