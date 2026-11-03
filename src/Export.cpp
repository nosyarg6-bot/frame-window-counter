#include "Export.hpp"
#include "Replay.hpp"
#include "Mp4Fix.hpp"

#if __has_include(<eclipse.ffmpeg-api/include/events.hpp>)
#include <eclipse.ffmpeg-api/include/events.hpp>
#else
// copy of the FFmpeg API mod's header-only event interface
#include "ffmpeg/events.hpp"
#endif

#include <Geode/binding/EndLevelLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/fmod/fmod.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/file.hpp>

#include <chrono>
#include <cmath>
#include <cstring>

using namespace geode::prelude;

namespace fw {

static constexpr int kAudioRate = 44100;

struct Exporter::State {
    std::vector<ClipSegment> segments;
    size_t index = 0;
    std::string name;
    std::filesystem::path videoTmp;
    std::filesystem::path output;
    ffmpeg::events::Recorder recorder;
    CCRenderTexture* texture = nullptr;
    std::vector<uint8_t> pixels;
    int width = 1280;
    int height = 720;
    int fps = 60;
    int totalFrames = 0;
    int estimatedFrames = 1;
    int holdLeft = -1;
    CCLabelBMFont* progress = nullptr;
    CCNode* driver = nullptr;
};

// runs Exporter::onFrame once per frame
class ExportDriver : public CCNode {
public:
    static ExportDriver* create() {
        auto ret = new ExportDriver();
        ret->init();
        ret->autorelease();
        ret->scheduleUpdate();
        return ret;
    }
    void update(float) override { Exporter::get().onFrame(); }
};

Exporter& Exporter::get() {
    static Exporter inst;
    return inst;
}

std::filesystem::path Exporter::clipsFolder() {
    auto dir = Mod::get()->getSettingValue<std::filesystem::path>("clips-folder");
    if (dir.empty()) dir = Mod::get()->getSaveDir() / "clips";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static std::string sanitize(std::string s) {
    for (auto& c : s) {
        if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos || static_cast<unsigned char>(c) < 32) c = '_';
    }
    if (s.empty()) s = "level";
    return s;
}

static void notify(std::string const& text, NotificationIcon icon, float time = 3.f) {
    Notification::create(text, icon, time)->show();
}

void Exporter::saveLastAttempt() {
    auto const& hist = Tracker::get().history();
    for (auto const& run : hist) {
        if (run.replay && !run.replay->empty()) {
            exportRun(run);
            return;
        }
        break;
    }
    notify("No recorded attempt to save yet", NotificationIcon::Warning);
}

void Exporter::saveLastTwoAttempts() {
    auto const& hist = Tracker::get().history();
    if (hist.size() < 2 || !hist[0].replay || !hist[1].replay || hist[0].replay->empty() || hist[1].replay->empty()) {
        if (!hist.empty()) {
            saveLastAttempt();
            return;
        }
        notify("Need two recorded attempts", NotificationIcon::Warning);
        return;
    }
    double tail = std::clamp(Mod::get()->getSettingValue<double>("prev-tail"), 0.25, 30.0);
    std::vector<ClipSegment> segs;
    ClipSegment prev;
    prev.run = hist[1];
    prev.to = static_cast<int>(hist[1].replay->size());
    prev.from = std::max(0, prev.to - static_cast<int>(tail / hist[1].secondsPerTick));
    segs.push_back(prev);
    ClipSegment last;
    last.run = hist[0];
    last.to = static_cast<int>(hist[0].replay->size());
    segs.push_back(last);
    start(std::move(segs), fmt::format("{} attempts {}-{}", hist[0].levelName, hist[1].attempt, hist[0].attempt));
}

void Exporter::exportRun(RunRecord const& run) {
    if (!run.replay || run.replay->empty()) {
        notify("This run has no replay data", NotificationIcon::Warning);
        return;
    }
    ClipSegment seg;
    seg.run = run;
    seg.to = static_cast<int>(run.replay->size());
    start({seg}, fmt::format("{} attempt {}", run.levelName, run.attempt));
}

static std::string pickCodec() {
    auto wanted = Mod::get()->getSettingValue<std::string>("clip-codec");
    auto codecs = ffmpeg::events::Recorder::getAvailableCodecs();
    auto has = [&](std::string const& c) { return std::find(codecs.begin(), codecs.end(), c) != codecs.end(); };
    if (!wanted.empty() && has(wanted)) return wanted;
    for (char const* c : {"libx264", "h264_nvenc", "h264_amf", "h264_qsv", "h264_mf", "libopenh264", "h264", "mpeg4"}) {
        if (has(c)) return c;
    }
    return codecs.empty() ? "libx264" : codecs.front();
}

void Exporter::start(std::vector<ClipSegment> segments, std::string name) {
    auto pl = PlayLayer::get();
    if (m_active || !pl || segments.empty()) return;
    // leave the level complete screen first so it isn't in the video
    EndLevelLayer* endLayer = CCScene::get() ? CCScene::get()->getChildByType<EndLevelLayer>(0) : nullptr;
    if (!endLayer) endLayer = pl->getChildByType<EndLevelLayer>(0);
    if (endLayer) endLayer->onReplay(nullptr);
    if (Replay::get().active()) Replay::get().stop();

    auto st = std::make_unique<State>();
    if (!st->recorder.isValid()) {
        FLAlertLayer::create(
            "Frame Window Counter",
            "Saving clips needs the <cy>FFmpeg API</c> mod. Install it from the Geode mod browser, restart, and try again.",
            "OK"
        )->show();
        return;
    }

    auto win = CCDirector::get()->getWinSize();
    st->height = static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("clip-height"), 240, 2160)) & ~1;
    st->width = static_cast<int>(std::round(st->height * win.width / win.height)) & ~1;
    int fps = static_cast<int>(Mod::get()->getSettingValue<int64_t>("clip-fps"));
    st->fps = fps >= 200 ? 240 : fps >= 90 ? 120 : fps >= 45 ? 60 : 30; // must divide 240 steps per second
    st->segments = std::move(segments);
    st->name = sanitize(name);

    auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H-%M-%S", std::localtime(&stamp));
    st->output = clipsFolder() / fmt::format("{} {}.mp4", st->name, buf);
    st->videoTmp = clipsFolder() / "rendering.tmp.mp4";

    ffmpeg::RenderSettings rs;
    rs.m_pixelFormat = ffmpeg::PixelFormat::RGBA;
    rs.m_codec = pickCodec();
    rs.m_bitrate = static_cast<int64_t>(st->width) * st->height * st->fps / 6; // ~ good quality
    rs.m_width = st->width;
    rs.m_height = st->height;
    rs.m_fps = static_cast<uint16_t>(st->fps);
    rs.m_doVerticalFlip = true;
    rs.m_outputFile = st->videoTmp;
    if (auto res = st->recorder.init(rs); res.isErr()) {
        notify("Couldn't start the video encoder: " + res.unwrapErr(), NotificationIcon::Error, 5.f);
        return;
    }
    log::info("Frame Window Counter: exporting {}x{} @{} with {}", st->width, st->height, st->fps, rs.m_codec);

    st->texture = CCRenderTexture::create(st->width, st->height, kCCTexture2DPixelFormat_RGBA8888);
    st->texture->retain();
    st->pixels.resize(static_cast<size_t>(st->width) * st->height * 4);
    int ticks = 0;
    for (auto const& s : st->segments) ticks += s.to - s.from;
    st->estimatedFrames = std::max(1, ticks * st->fps / 240);

    st->progress = CCLabelBMFont::create("Saving clip...", "bigFont.fnt");
    st->progress->setScale(0.5f);
    st->progress->setPosition({win.width * 0.5f, win.height - 20.f});
    st->progress->retain();
    CCScene::get()->addChild(st->progress, 100000);
    st->driver = ExportDriver::create();
    st->driver->retain();
    CCScene::get()->addChild(st->driver);

    m_state = std::move(st);
    m_active = true;
    Tracker::get().stopForReplay(pl);
    FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
    startSegment();
}

void Exporter::startSegment() {
    auto pl = PlayLayer::get();
    auto& st = *m_state;
    if (!pl) {
        finish(false, "left the level");
        return;
    }
    auto& seg = st.segments[st.index];
    auto& rp = Replay::get();
    rp.setExporting(true);
    rp.request(seg.run);
    m_driving = true;
    pl->resetLevelFromStart();
    if (rp.active() && seg.from > 0) rp.seekTo(seg.from);
    m_driving = false;
    FMODAudioEngine::sharedEngine()->pauseAllMusic(true);
    if (!rp.active()) {
        finish(false, "this run can't be replayed");
        return;
    }
    st.holdLeft = -1;
}

void Exporter::capture() {
    auto& st = *m_state;
    auto scene = CCScene::get();
    if (!scene) return;
    st.progress->setVisible(false);
    st.texture->beginWithClear(0.f, 0.f, 0.f, 1.f);
    // CCRenderTexture draws at screen-pixel scale (so a smaller video only shows
    // a corner of the screen): map the whole screen onto the video instead
    auto win = CCDirector::get()->getWinSize();
    kmGLMatrixMode(KM_GL_PROJECTION);
    kmGLLoadIdentity();
    kmMat4 ortho;
    kmMat4OrthographicProjection(&ortho, 0.f, win.width, 0.f, win.height, -1024.f, 1024.f);
    kmGLMultMatrix(&ortho);
    kmGLMatrixMode(KM_GL_MODELVIEW);
    kmGLLoadIdentity();
    glViewport(0, 0, st.width, st.height);
    scene->visit();
    glReadPixels(0, 0, st.width, st.height, GL_RGBA, GL_UNSIGNED_BYTE, st.pixels.data());
    st.texture->end();
    st.progress->setVisible(true);
    if (auto res = st.recorder.writeFrame(st.pixels); res.isErr()) {
        finish(false, "encoder error: " + res.unwrapErr());
        return;
    }
    ++st.totalFrames;
}

void Exporter::onFrame() {
    if (!m_active || !m_state) return;
    auto pl = PlayLayer::get();
    if (!pl) {
        finish(false, "left the level");
        return;
    }
    auto& st = *m_state;
    auto& rp = Replay::get();
    int ticksPerFrame = 240 / st.fps;

    // a few video frames per screen frame keeps the game responsive
    for (int n = 0; n < 3 && m_active; ++n) {
        auto& seg = st.segments[st.index];
        if (st.holdLeft < 0) {
            if (rp.tick() >= seg.to || rp.frozen() || !rp.active()) {
                // show where the run ended for a moment
                st.holdLeft = seg.run.completed ? 0 : st.fps / 2;
                seg.holdFrames = st.holdLeft;
                continue;
            }
            m_driving = true;
            pl->update(static_cast<float>(ticksPerFrame) / 240.f);
            m_driving = false;
            capture();
            ++seg.renderedFrames;
        }
        else if (st.holdLeft > 0) {
            capture();
            --st.holdLeft;
        }
        else {
            if (++st.index >= st.segments.size()) {
                finish(true);
                return;
            }
            startSegment();
        }
    }
    if (m_active && st.progress) {
        int pct = std::min(99, st.totalFrames * 100 / st.estimatedFrames);
        st.progress->setString(fmt::format("Saving clip... {}%", pct).c_str());
    }
}

// ---------------------------------------------------------------------------
// audio

static std::string songPath(GJGameLevel* level) {
    if (!level) return "";
    std::string name = level->getAudioFileName();
    if (name.empty()) return "";
    std::error_code ec;
    if (std::filesystem::exists(name, ec)) return name;
    return std::string(CCFileUtils::sharedFileUtils()->fullPathForFilename(name.c_str(), false));
}

// level music from `startSec` for `durSec`, as 44.1 kHz stereo floats
static std::vector<float> decodeMusic(std::string const& path, double startSec, double durSec) {
    std::vector<float> out(static_cast<size_t>(durSec * kAudioRate) * 2, 0.f);
    auto engine = FMODAudioEngine::sharedEngine();
    if (path.empty() || !engine || !engine->m_system || durSec <= 0.0) return out;

    FMOD::Sound* sound = nullptr;
    if (engine->m_system->createSound(path.c_str(), FMOD_OPENONLY | FMOD_ACCURATETIME | FMOD_CREATESTREAM, nullptr, &sound) != FMOD_OK || !sound) {
        log::warn("Frame Window Counter: couldn't open song {}", path);
        return out;
    }
    FMOD_SOUND_TYPE type;
    FMOD_SOUND_FORMAT format;
    int channels = 2, bits = 16;
    float freq = 44100.f;
    int prio = 0;
    sound->getFormat(&type, &format, &channels, &bits);
    sound->getDefaults(&freq, &prio);
    channels = std::max(1, channels);
    if (startSec < 0.0) startSec = 0.0;

    unsigned int startPcm = static_cast<unsigned int>(startSec * freq);
    size_t needFrames = static_cast<size_t>(durSec * freq) + 4;
    std::vector<float> src;
    src.reserve(needFrames * 2);
    if (sound->seekData(startPcm) == FMOD_OK) {
        int bytesPerSample = std::max(1, bits / 8);
        std::vector<uint8_t> chunk(16384 * bytesPerSample * channels);
        while (src.size() / 2 < needFrames) {
            unsigned int read = 0;
            auto r = sound->readData(chunk.data(), static_cast<unsigned int>(chunk.size()), &read);
            if (read == 0) break;
            size_t samples = read / bytesPerSample;
            for (size_t i = 0; i + channels <= samples; i += channels) {
                float ch[2] = {0.f, 0.f};
                for (int c = 0; c < std::min(channels, 2); ++c) {
                    uint8_t const* p = chunk.data() + (i + c) * bytesPerSample;
                    float v = 0.f;
                    switch (format) {
                        case FMOD_SOUND_FORMAT_PCM8: v = (static_cast<int>(*p) - 128) / 128.f; break;
                        case FMOD_SOUND_FORMAT_PCM16: { int16_t x; std::memcpy(&x, p, 2); v = x / 32768.f; } break;
                        case FMOD_SOUND_FORMAT_PCM24: { int32_t x = (p[0] << 8) | (p[1] << 16) | (p[2] << 24); v = (x >> 8) / 8388608.f; } break;
                        case FMOD_SOUND_FORMAT_PCM32: { int32_t x; std::memcpy(&x, p, 4); v = x / 2147483648.f; } break;
                        case FMOD_SOUND_FORMAT_PCMFLOAT: std::memcpy(&v, p, 4); break;
                        default: break;
                    }
                    ch[c] = v;
                }
                if (channels == 1) ch[1] = ch[0];
                src.push_back(ch[0]);
                src.push_back(ch[1]);
            }
            if (r != FMOD_OK) break;
        }
    }
    sound->release();

    // resample to 44.1 kHz
    size_t srcFrames = src.size() / 2;
    size_t outFrames = out.size() / 2;
    double ratio = freq / kAudioRate;
    for (size_t i = 0; i < outFrames; ++i) {
        double pos = i * ratio;
        size_t a = static_cast<size_t>(pos);
        if (a + 1 >= srcFrames) break;
        float f = static_cast<float>(pos - a);
        out[i * 2] = src[a * 2] + (src[(a + 1) * 2] - src[a * 2]) * f;
        out[i * 2 + 1] = src[a * 2 + 1] + (src[(a + 1) * 2 + 1] - src[a * 2 + 1]) * f;
    }
    return out;
}

// 16-bit stereo 44.1 kHz WAV
static void writeWav(std::filesystem::path const& path, std::vector<float> const& samples) {
    std::vector<uint8_t> data;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) data.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { data.push_back(static_cast<uint8_t>(v)); data.push_back(static_cast<uint8_t>(v >> 8)); };
    uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
    data.insert(data.end(), {'R', 'I', 'F', 'F'}); u32(36 + bytes);
    data.insert(data.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '}); u32(16); u16(1); u16(2);
    u32(kAudioRate); u32(kAudioRate * 4); u16(4); u16(16);
    data.insert(data.end(), {'d', 'a', 't', 'a'}); u32(bytes);
    for (float f : samples) {
        auto v = static_cast<int16_t>(std::clamp(f, -1.f, 1.f) * 32767.f);
        u16(static_cast<uint16_t>(v));
    }
    (void)utils::file::writeBinary(path, data);
}

std::vector<float> Exporter::buildAudio() {
    auto& st = *m_state;
    auto pl = PlayLayer::get();
    float gameVol = static_cast<float>(Mod::get()->getSettingValue<double>("clip-game-volume"));
    float micVol = static_cast<float>(Mod::get()->getSettingValue<double>("clip-mic-volume"));
    size_t totalSamples = static_cast<size_t>(static_cast<double>(st.totalFrames) / st.fps * kAudioRate);
    std::vector<float> out(totalSamples * 2, 0.f);

    std::string path = pl ? songPath(pl->m_level) : "";
    double songOffset = (pl && pl->m_levelSettings) ? pl->m_levelSettings->m_songOffset : 0.0;
    size_t pos = 0;
    for (auto const& seg : st.segments) {
        double startSec = seg.from * seg.run.secondsPerTick;
        double durSec = static_cast<double>(seg.renderedFrames) / st.fps;
        size_t segSamples = static_cast<size_t>(durSec * kAudioRate);

        if (gameVol > 0.f) {
            double songPos = seg.run.musicStartSec >= 0.0 ? seg.run.musicStartSec + startSec : songOffset + startSec;
            auto music = decodeMusic(path, songPos, durSec);
            for (size_t i = 0; i < segSamples && pos + i < totalSamples && i * 2 + 1 < music.size(); ++i) {
                out[(pos + i) * 2] += music[i * 2] * gameVol;
                out[(pos + i) * 2 + 1] += music[i * 2 + 1] * gameVol;
            }
        }
        if (micVol > 0.f && seg.run.mic && !seg.run.mic->empty()) {
            auto const& mic = *seg.run.mic;
            double ratio = static_cast<double>(seg.run.micRate) / kAudioRate;
            double base = startSec * seg.run.micRate;
            for (size_t i = 0; i < segSamples && pos + i < totalSamples; ++i) {
                double p = base + i * ratio;
                size_t a = static_cast<size_t>(p);
                if (a + 1 >= mic.size()) break;
                float f = static_cast<float>(p - a);
                float v = (mic[a] + (mic[a + 1] - mic[a]) * f) / 32768.f * micVol;
                out[(pos + i) * 2] += v;
                out[(pos + i) * 2 + 1] += v;
            }
        }
        pos += segSamples + static_cast<size_t>(static_cast<double>(seg.holdFrames) / st.fps * kAudioRate);
    }
    for (auto& v : out) v = std::clamp(v, -1.f, 1.f);
    return out;
}

void Exporter::finish(bool ok, std::string const& error) {
    if (!m_state) return;
    auto& st = *m_state;
    st.recorder.stop();
    m_active = false;

    std::string message;
    if (ok && st.totalFrames > 0) {
        auto audio = buildAudio();
        if (Mod::get()->getSettingValue<bool>("clip-save-wav")) {
            auto wavPath = st.output;
            wavPath.replace_extension(".wav");
            writeWav(wavPath, audio);
        }
        std::error_code ec;
        auto res = ffmpeg::events::AudioMixer::mixVideoRaw(st.videoTmp, audio, st.output);
        if (res.isErr()) {
            // keep the video without sound rather than nothing
            std::filesystem::rename(st.videoTmp, st.output, ec);
            message = "Clip saved (without audio): " + res.unwrapErr();
        }
        else {
            message = "Clip saved: " + st.output.filename().string();
            if (Mod::get()->getSettingValue<bool>("mic-enabled")) {
                int peak = 0;
                bool any = false;
                for (auto const& seg : st.segments) {
                    if (!seg.run.mic) continue;
                    any = true;
                    for (auto v : *seg.run.mic) peak = std::max(peak, std::abs(static_cast<int>(v)));
                }
                if (!any) message += " (no mic audio was recorded for this attempt)";
                else if (peak < 100) message += " (the mic was silent - check Choose Microphone)";
            }
            // add the audio decoder config the encoder leaves out, so every player has sound
            if (auto data = utils::file::readBinary(st.output); data.isOk()) {
                auto bytes = data.unwrap();
                if (mp4::fixAacEsds(bytes, kAudioRate, 2)) {
                    if (auto w = utils::file::writeBinary(st.output, bytes); w.isErr()) {
                        log::warn("Frame Window Counter: couldn't fix clip audio: {}", w.unwrapErr());
                    }
                }
            }
        }
        std::filesystem::remove(st.videoTmp, ec);
        log::info("Frame Window Counter: saved {}", st.output.string());
    }
    else {
        std::error_code ec;
        std::filesystem::remove(st.videoTmp, ec);
        message = "Couldn't save the clip" + (error.empty() ? std::string() : ": " + error);
    }

    if (st.progress) {
        st.progress->removeFromParent();
        st.progress->release();
    }
    if (st.driver) {
        st.driver->removeFromParent();
        st.driver->release();
    }
    if (st.texture) st.texture->release();
    m_state.reset();

    auto& rp = Replay::get();
    rp.setExporting(false);
    rp.stop();
    FMODAudioEngine::sharedEngine()->resumeAllMusic();
    if (auto pl = PlayLayer::get()) pl->resetLevelFromStart();
    notify(message, ok ? NotificationIcon::Success : NotificationIcon::Error, 4.f);
}

} // namespace fw
