#include "Mic.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/fmod/fmod.hpp>

#include <cstring>

using namespace geode::prelude;

namespace fw {

static constexpr double kRollingSeconds = 240.0;
static constexpr double kRecordBufferSeconds = 4.0;

static FMOD::System* fmodSystem() {
    auto engine = FMODAudioEngine::sharedEngine();
    return engine ? engine->m_system : nullptr;
}

Mic& Mic::get() {
    static Mic inst;
    return inst;
}

std::vector<std::string> Mic::deviceNames() {
    std::vector<std::string> out;
    auto sys = fmodSystem();
    if (!sys) return out;
    int count = 0, connected = 0;
    if (sys->getRecordNumDrivers(&count, &connected) != FMOD_OK) return out;
    for (int i = 0; i < count; ++i) {
        char name[256] = {0};
        int rate = 0, channels = 0;
        FMOD_SPEAKERMODE mode;
        FMOD_DRIVER_STATE state = 0;
        if (sys->getRecordDriverInfo(i, name, sizeof(name), nullptr, &rate, &mode, &channels, &state) != FMOD_OK) continue;
        // loopback devices ("Stereo Mix" style) are listed too; skip unplugged ones
        if (!(state & FMOD_DRIVER_STATE_CONNECTED)) continue;
        out.push_back(name);
    }
    return out;
}

void Mic::start() {
    if (m_driver >= 0) return;
    if (!Mod::get()->getSettingValue<bool>("mic-enabled")) return;
    auto sys = fmodSystem();
    if (!sys) return;

    int count = 0, connected = 0;
    if (sys->getRecordNumDrivers(&count, &connected) != FMOD_OK || count <= 0) {
        log::warn("Frame Window Counter: no microphone found");
        return;
    }
    auto wanted = Mod::get()->getSettingValue<std::string>("mic-device");
    int driver = -1;
    int firstConnected = -1;
    for (int i = 0; i < count; ++i) {
        char name[256] = {0};
        int rate = 0, channels = 0;
        FMOD_SPEAKERMODE mode;
        FMOD_DRIVER_STATE state = 0;
        if (sys->getRecordDriverInfo(i, name, sizeof(name), nullptr, &rate, &mode, &channels, &state) != FMOD_OK) continue;
        if (!(state & FMOD_DRIVER_STATE_CONNECTED)) continue;
        if (firstConnected < 0) firstConnected = i;
        if (!wanted.empty() && wanted == name) {
            driver = i;
            m_rate = rate > 0 ? rate : 48000;
        }
    }
    if (driver < 0) {
        driver = firstConnected;
        if (driver < 0) return;
        char name[256] = {0};
        int rate = 0, channels = 0;
        FMOD_SPEAKERMODE mode;
        FMOD_DRIVER_STATE state = 0;
        sys->getRecordDriverInfo(driver, name, sizeof(name), nullptr, &rate, &mode, &channels, &state);
        m_rate = rate > 0 ? rate : 48000;
    }

    FMOD_CREATESOUNDEXINFO ex;
    std::memset(&ex, 0, sizeof(ex));
    ex.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
    ex.numchannels = 1;
    ex.format = FMOD_SOUND_FORMAT_PCM16;
    ex.defaultfrequency = m_rate;
    m_bufferSamples = static_cast<unsigned int>(m_rate * kRecordBufferSeconds);
    ex.length = m_bufferSamples * sizeof(int16_t);
    if (sys->createSound(nullptr, FMOD_LOOP_NORMAL | FMOD_OPENUSER, &ex, &m_sound) != FMOD_OK) {
        m_sound = nullptr;
        return;
    }
    if (sys->recordStart(driver, m_sound, true) != FMOD_OK) {
        m_sound->release();
        m_sound = nullptr;
        return;
    }
    m_driver = driver;
    m_lastPos = 0;
    {
        char name[256] = {0};
        int rate = 0, channels = 0;
        FMOD_SPEAKERMODE mode;
        FMOD_DRIVER_STATE state = 0;
        sys->getRecordDriverInfo(driver, name, sizeof(name), nullptr, &rate, &mode, &channels, &state);
        m_deviceName = name;
    }
    log::info("Frame Window Counter: recording microphone #{} '{}' at {} Hz", driver, m_deviceName, m_rate);
}

void Mic::stop() {
    if (m_driver < 0) return;
    if (auto sys = fmodSystem()) sys->recordStop(m_driver);
    if (m_sound) m_sound->release();
    m_sound = nullptr;
    m_driver = -1;
}

float Mic::level() const {
    size_t n = std::min<size_t>(m_rolling.size(), static_cast<size_t>(m_rate / 10));
    int peak = 0;
    for (size_t i = m_rolling.size() - n; i < m_rolling.size(); ++i) peak = std::max(peak, std::abs(static_cast<int>(m_rolling[i])));
    return peak / 32768.f;
}

void Mic::update() {
    // follow the setting, even if it's changed in the middle of a level
    bool enabled = Mod::get()->getSettingValue<bool>("mic-enabled");
    if (enabled && m_driver < 0) {
        static int s_retry = 0;
        if (++s_retry % 240 == 1) start();
    }
    if (!enabled && m_driver >= 0) stop();
    if (m_driver < 0 || !m_sound) return;
    auto sys = fmodSystem();
    if (!sys) return;
    unsigned int pos = 0;
    if (sys->getRecordPosition(m_driver, &pos) != FMOD_OK) return;
    if (pos == m_lastPos) return;
    unsigned int count = pos >= m_lastPos ? pos - m_lastPos : m_bufferSamples - m_lastPos + pos;

    void* p1 = nullptr;
    void* p2 = nullptr;
    unsigned int l1 = 0, l2 = 0;
    if (m_sound->lock(m_lastPos * sizeof(int16_t), count * sizeof(int16_t), &p1, &p2, &l1, &l2) == FMOD_OK) {
        auto push = [&](void* p, unsigned int bytes) {
            auto s = static_cast<int16_t*>(p);
            for (unsigned int i = 0; i < bytes / sizeof(int16_t); ++i) m_rolling.push_back(s[i]);
        };
        if (p1) push(p1, l1);
        if (p2) push(p2, l2);
        m_sound->unlock(p1, p2, l1, l2);
        m_total += count;
    }
    m_lastPos = pos;

    size_t cap = static_cast<size_t>(m_rate * kRollingSeconds);
    while (m_rolling.size() > cap) {
        m_rolling.pop_front();
        ++m_rollingStart;
    }
}

std::shared_ptr<std::vector<int16_t>> Mic::extract(uint64_t from, uint64_t to) const {
    auto out = std::make_shared<std::vector<int16_t>>();
    if (to <= from) return out;
    uint64_t end = m_rollingStart + m_rolling.size();
    uint64_t a = std::max(from, m_rollingStart);
    uint64_t b = std::min(to, end);
    // keep the timing right if the start was already dropped: pad with silence
    if (a > from) out->resize(static_cast<size_t>(a - from), 0);
    for (uint64_t i = a; i < b; ++i) out->push_back(m_rolling[static_cast<size_t>(i - m_rollingStart)]);
    return out;
}

// ---------------------------------------------------------------------------

void MicPlayer::play(std::shared_ptr<std::vector<int16_t>> samples, int rate, double startSeconds, float volume) {
    stop();
    auto sys = fmodSystem();
    if (!sys || !samples || samples->empty() || volume <= 0.f) return;
    m_samples = std::move(samples);

    FMOD_CREATESOUNDEXINFO ex;
    std::memset(&ex, 0, sizeof(ex));
    ex.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
    ex.numchannels = 1;
    ex.format = FMOD_SOUND_FORMAT_PCM16;
    ex.defaultfrequency = rate;
    ex.length = static_cast<unsigned int>(m_samples->size() * sizeof(int16_t));
    if (sys->createSound(reinterpret_cast<char const*>(m_samples->data()),
                         FMOD_OPENMEMORY_POINT | FMOD_OPENRAW | FMOD_LOOP_OFF | FMOD_CREATESAMPLE,
                         &ex, &m_sound) != FMOD_OK) {
        m_sound = nullptr;
        return;
    }
    if (sys->playSound(m_sound, nullptr, true, &m_channel) != FMOD_OK) {
        m_channel = nullptr;
        return;
    }
    m_channel->setVolume(volume);
    seek(startSeconds);
    m_channel->setPaused(false);
}

void MicPlayer::seek(double seconds) {
    if (!m_channel) return;
    m_channel->setPosition(static_cast<unsigned int>(std::max(0.0, seconds) * 1000.0), FMOD_TIMEUNIT_MS);
}

void MicPlayer::setPaused(bool paused) {
    if (m_channel) m_channel->setPaused(paused);
}

void MicPlayer::stop() {
    if (m_channel) m_channel->stop();
    m_channel = nullptr;
    if (m_sound) m_sound->release();
    m_sound = nullptr;
    m_samples.reset();
}

} // namespace fw
