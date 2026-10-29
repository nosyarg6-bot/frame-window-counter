#pragma once

#include <Geode/Geode.hpp>
#include <deque>
#include <string>
#include <vector>

class PlayLayer;
class GameObject;

namespace fw {

// One recorded physics tick of player 1.
struct Sample {
    float x = 0.f;
    float y = 0.f;
    float w = 30.f;   // outer hitbox width
    float h = 30.f;   // outer hitbox height
    bool onGround = false;
    bool sideways = false;
};

enum class EvalState {
    Pending,   // waiting for enough of the future trajectory to be recorded
    Done,      // window calculated
    NoEffect,  // input did not change the trajectory (e.g. releasing as cube)
    Unknown,   // could not be calculated (sideways gameplay, not enough data)
};

struct InputRecord {
    int number = 0;          // 1-based index within the run
    bool press = true;       // true = click, false = release
    int preIdx = 0;          // index of the last sample before the input took effect
    float percent = 0.f;     // level percent when the input happened
    EvalState state = EvalState::Pending;

    float earlyFrames = 0.f; // how many 240fps frames earlier it still works
    float lateFrames = 0.f;  // how many 240fps frames later it still works
    float windowFrames = 0.f;
    bool cappedEarly = false;   // hit the max window limit
    bool cappedLate = false;
    bool truncated = false;     // run ended before the full horizon was recorded

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

struct Collider {
    GameObject* obj = nullptr;
    float minX = 0.f;
    bool hazard = false;
};

class Tracker {
public:
    static Tracker& get();

    void beginLevel(PlayLayer* pl);
    void endLevel(PlayLayer* pl);
    void startRun(PlayLayer* pl);
    void finishRun(PlayLayer* pl, bool completed);

    void onTickStart(PlayLayer* pl);
    void onTickEnd(PlayLayer* pl);
    void onButton(PlayLayer* pl, bool down);

    RunRecord const& current() const { return m_current; }
    std::deque<RunRecord> const& history() const { return m_history; }
    bool hasRunInProgress() const { return m_running; }
    int revision() const { return m_revision; }

    // true while the overlay should display the summary of the run that just ended
    bool showingLastRun() const { return m_showLastRun; }

private:
    struct Ctx {
        int p = 0;          // pre-input sample index
        int n = -1;         // next input's pre index (-1 = none)
        int prev = -1;      // previous input's pre index (-1 = none)
        int H = 0;          // horizon (last sample index checked)
        float y0 = 0.f, v = 0.f, a = 0.f;
        bool grounded = false;
        std::vector<Collider const*> cands;
        std::vector<char> ignore;
    };

    float X(double t) const;
    float Y(double t) const;
    float extrap(Ctx const& c, double j) const;
    bool hitsAt(Ctx& c, float x, float y, float w, float h, bool baseline);
    bool survives(Ctx& c, double d, bool baseline);
    bool tryEvaluate(size_t idx, bool final);
    void evaluatePending(bool final, int budget);
    void bump() { ++m_revision; }

    std::vector<Collider> m_colliders;
    std::vector<Sample> m_samples;
    RunRecord m_current;
    std::deque<RunRecord> m_history;
    size_t m_nextEval = 0;
    bool m_running = false;
    bool m_holding = false;
    bool m_inTick = false;
    bool m_showLastRun = false;
    int m_revision = 0;
    float m_innerScale = 0.3f;
};

std::string windowLabel(InputRecord const& r);
cocos2d::ccColor3B windowColor(InputRecord const& r);

} // namespace fw
