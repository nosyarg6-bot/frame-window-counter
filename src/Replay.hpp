#pragma once

#include <Geode/Geode.hpp>
#include "Tracker.hpp"

class PlayLayer;
class Slider;

namespace fw {

class ReplayBar;

// Plays back one of your recorded runs: restarts the level, presses the
// buttons exactly when you did, keeps the player on your recorded path, and
// pops up the frame window of every click and release as it happens.
// Left / right arrow keys (or the bar at the bottom) jump 5 seconds, the
// scroll bar seeks anywhere, and at the end it stays on the last frame.
class Replay {
public:
    static Replay& get();

    // from the report popup; the replay starts on the next restart
    void request(RunRecord const& run);
    bool pending() const { return m_pending; }
    bool active() const { return m_active; }
    bool injecting() const { return m_injecting; }
    bool frozen() const { return m_active && m_frozen; }
    bool restarting() const { return m_restarting; }

    void begin(PlayLayer* pl);   // level reset while pending
    void onRestarted();          // level reset caused by seeking backwards
    void stop();
    void exitReplay();           // stop and give control back with a fresh attempt

    void onTickStart(PlayLayer* pl);
    void onTickEnd(PlayLayer* pl);

    void seekTo(int tick);
    void seekBySeconds(float seconds);

    RunRecord const& run() const { return m_run; }
    int shownInputs() const { return static_cast<int>(m_nextInput); }
    int revision() const { return m_revision; }
    int tick() const { return m_tick; }
    int totalTicks() const;

private:
    void popLabel(PlayLayer* pl, InputRecord const& in);
    void syncMusic(PlayLayer* pl);

    RunRecord m_run;
    bool m_pending = false;
    bool m_active = false;
    bool m_injecting = false;
    bool m_frozen = false;
    bool m_fastForward = false;
    bool m_restarting = false;
    int m_tick = 0;
    int m_ffTarget = 0;
    size_t m_nextInput = 0;
    int m_revision = 0;
    ReplayBar* m_bar = nullptr;
};

// Controls shown at the bottom of the screen during a replay.
class ReplayBar : public cocos2d::CCNode {
public:
    static ReplayBar* create();
    void refresh();

protected:
    bool init() override;
    void update(float dt) override;
    void onSlider(cocos2d::CCObject* sender);

    Slider* m_slider = nullptr;
    cocos2d::CCLabelBMFont* m_time = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    float m_pendingValue = -1.f;
    float m_sinceChange = 0.f;
};

std::string labelText(InputRecord const& in);
cocos2d::CCLabelBMFont* makeWorldLabel(InputRecord const& in);

} // namespace fw
