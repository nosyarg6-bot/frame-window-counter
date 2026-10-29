#pragma once

#include <Geode/Geode.hpp>
#include "Tracker.hpp"

class PlayLayer;

namespace fw {

// Plays back one of your recorded runs: restarts the level, presses the
// buttons exactly when you did, keeps the player on your recorded path, and
// pops up the frame window of every click and release as it happens.
class Replay {
public:
    static Replay& get();

    // from the report popup; the replay starts on the next restart
    void request(RunRecord const& run);
    bool pending() const { return m_pending; }
    bool active() const { return m_active; }
    bool injecting() const { return m_injecting; }

    void begin(PlayLayer* pl);   // called on level reset while pending
    void stop();

    void onTickStart(PlayLayer* pl);
    void onTickEnd(PlayLayer* pl);

    RunRecord const& run() const { return m_run; }
    int shownInputs() const { return static_cast<int>(m_nextInput); }
    int revision() const { return m_revision; }

private:
    void popLabel(PlayLayer* pl, InputRecord const& in);

    RunRecord m_run;
    bool m_pending = false;
    bool m_active = false;
    bool m_injecting = false;
    int m_tick = 0;
    size_t m_nextInput = 0;
    int m_revision = 0;
};

} // namespace fw
