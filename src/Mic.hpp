#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace FMOD { class Sound; class Channel; }

namespace fw {

// Records the chosen microphone while you play (mono, 16-bit), so replays and
// exported clips can include your click / keyboard sounds.
class Mic {
public:
    static Mic& get();
    static std::vector<std::string> deviceNames();

    void start();               // start recording if enabled in settings
    void stop();
    void update();              // pull new samples (call often)
    bool running() const { return m_driver >= 0; }

    uint64_t position() const { return m_total; } // total samples recorded so far
    int rate() const { return m_rate; }
    float level() const; // loudness of the last ~0.1 s, 0..1
    std::string deviceName() const { return m_deviceName; }
    // copy samples [from, to) that are still in the rolling buffer
    std::shared_ptr<std::vector<int16_t>> extract(uint64_t from, uint64_t to) const;

private:
    FMOD::Sound* m_sound = nullptr;
    int m_driver = -1;
    int m_rate = 48000;
    unsigned int m_bufferSamples = 0;
    unsigned int m_lastPos = 0;
    uint64_t m_total = 0;
    std::deque<int16_t> m_rolling;
    uint64_t m_rollingStart = 0; // sample index of m_rolling.front()
    std::string m_deviceName;
};

// Plays a recorded mic clip during a replay.
class MicPlayer {
public:
    void play(std::shared_ptr<std::vector<int16_t>> samples, int rate, double startSeconds, float volume);
    void seek(double seconds);
    void setPaused(bool paused);
    void stop();

private:
    FMOD::Sound* m_sound = nullptr;
    FMOD::Channel* m_channel = nullptr;
    std::shared_ptr<std::vector<int16_t>> m_samples;
};

} // namespace fw
