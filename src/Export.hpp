#pragma once

#include <Geode/Geode.hpp>
#include "Tracker.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace fw {

// One piece of a clip: a recorded run from tick `from` to tick `to`.
struct ClipSegment {
    RunRecord run;
    int from = 0;
    int to = 0;
    // filled while rendering
    int renderedFrames = 0;
    int holdFrames = 0;
};

// Renders replays to an MP4 file (needs the "FFmpeg API" Geode mod).
class Exporter {
public:
    static Exporter& get();

    bool active() const { return m_active; }
    bool driving() const { return m_driving; }

    // the newest finished attempt
    void saveLastAttempt();
    // the newest attempt plus the end of the one before it
    void saveLastTwoAttempts();
    void exportRun(RunRecord const& run);

    void onFrame(); // called every frame while exporting
    static std::filesystem::path clipsFolder();

private:
    void start(std::vector<ClipSegment> segments, std::string name);
    void startSegment();
    void capture();
    void finish(bool ok, std::string const& error = "");
    std::vector<float> buildAudio();

    struct State;
    std::unique_ptr<State> m_state;
    bool m_active = false;
    bool m_driving = false;
};

} // namespace fw
