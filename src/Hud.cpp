#include "Hud.hpp"
#include "Tracker.hpp"
#include "Replay.hpp"

using namespace geode::prelude;

namespace fw {

static constexpr float kWidth = 176.f;
static constexpr float kRowH = 11.f;
static constexpr float kHeaderH = 15.f;
static constexpr float kFooterH = 12.f;
static constexpr float kPad = 4.f;

static FrameHud* s_instance = nullptr;

FrameHud* FrameHud::create() {
    auto ret = new FrameHud();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

FrameHud* FrameHud::get() {
    return s_instance;
}

CCPoint FrameHud::savedOffset() {
    return {
        static_cast<float>(Mod::get()->getSavedValue<double>("hud-x", 5.0)),
        static_cast<float>(Mod::get()->getSavedValue<double>("hud-y", 5.0)),
    };
}

void FrameHud::saveOffset(CCPoint offset) {
    Mod::get()->setSavedValue<double>("hud-x", offset.x);
    Mod::get()->setSavedValue<double>("hud-y", offset.y);
}

CCSize FrameHud::panelSize() const {
    return {kWidth, kHeaderH + kRowH * m_rows + kFooterH + kPad};
}

bool FrameHud::init() {
    if (!CCNode::init()) return false;
    s_instance = this;
    this->setID("frame-window-hud"_spr);
    this->setAnchorPoint({0.f, 1.f});

    m_bg = CCLayerColor::create(ccColor4B{0, 0, 0, 150});
    m_bg->setAnchorPoint({0.f, 0.f});
    m_bg->setPosition({0.f, 0.f});
    this->addChild(m_bg, -1);

    m_content = CCNode::create();
    this->addChild(m_content);

    this->applySettings();
    this->scheduleUpdate();
    return true;
}

void FrameHud::onExit() {
    if (s_instance == this) s_instance = nullptr;
    CCNode::onExit();
}

void FrameHud::applySettings() {
    auto mod = Mod::get();
    m_rows = static_cast<int>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("hud-rows"), 3, 20));
    m_showOffsets = mod->getSettingValue<bool>("show-offsets");
    m_showIneffective = mod->getSettingValue<bool>("show-ineffective");
    float scale = static_cast<float>(mod->getSettingValue<double>("hud-scale"));
    int opacity = static_cast<int>(std::clamp<int64_t>(mod->getSettingValue<int64_t>("hud-opacity"), 0, 255));

    auto size = this->panelSize();
    this->setContentSize(size);
    m_bg->setContentSize(size);
    m_bg->setOpacity(static_cast<GLubyte>(opacity));
    this->setScale(scale);

    auto win = CCDirector::get()->getWinSize();
    auto off = savedOffset();
    this->setPosition({off.x, win.height - off.y});
    this->setVisible(mod->getSettingValue<bool>("enabled"));

    m_seenRevision = -1;
}

void FrameHud::update(float) {
    static int s_frame = 0;
    if (++s_frame % 30 == 0) {
        // pick up changes made in the settings menu while in a level
        bool vis = Mod::get()->getSettingValue<bool>("enabled");
        int rows = static_cast<int>(Mod::get()->getSettingValue<int64_t>("hud-rows"));
        float scale = static_cast<float>(Mod::get()->getSettingValue<double>("hud-scale"));
        if (vis != this->isVisible() || rows != m_rows || scale != this->getScale()) {
            this->applySettings();
        }
    }
    if (!this->isVisible()) return;
    int rev = Tracker::get().revision() * 7919 + Replay::get().revision();
    if (rev != m_seenRevision) {
        m_seenRevision = rev;
        this->rebuild();
    }
}

static CCLabelBMFont* makeLabel(std::string const& text, float scale, ccColor3B color, CCPoint anchor) {
    auto lbl = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    lbl->setScale(scale);
    lbl->setColor(color);
    lbl->setAnchorPoint(anchor);
    return lbl;
}

static std::string offsetsText(InputRecord const& r) {
    if (r.state != EvalState::Done || r.isFree()) return "";
    auto fmtSide = [](float v, bool capped) {
        if (capped) return std::string("max");
        return fmt::format("{:.2f}", v);
    };
    return fmt::format("-{} / +{}", fmtSide(r.earlyFrames, r.cappedEarly), fmtSide(r.lateFrames, r.cappedLate));
}

void FrameHud::rebuild() {
    m_content->removeAllChildren();
    auto& tr = Tracker::get();
    auto size = this->panelSize();

    auto& rp = Replay::get();
    bool replaying = rp.active();
    bool lastRun = !replaying && tr.showingLastRun() && !tr.history().empty();
    RunRecord const& run = replaying ? rp.run() : lastRun ? tr.history().front() : tr.current();
    size_t visibleInputs = replaying ? std::min<size_t>(rp.shownInputs(), run.inputs.size()) : run.inputs.size();

    // header
    std::string title;
    if (replaying) {
        title = fmt::format("Replay - attempt {}", run.attempt);
    }
    else if (lastRun) {
        title = run.completed ? "Last run - complete!" : fmt::format("Last run - {}%", static_cast<int>(run.endPercent));
    }
    else {
        title = fmt::format("Frame Windows  (att {})", run.attempt);
    }
    auto header = makeLabel(title, 0.32f, replaying ? ccColor3B{120, 220, 255} : lastRun ? ccColor3B{255, 200, 80} : ccColor3B{255, 255, 255}, {0.f, 0.5f});
    header->setPosition({kPad, size.height - kHeaderH * 0.5f - 1.f});
    header->limitLabelWidth(kWidth - kPad * 2, 0.32f, 0.1f);
    m_content->addChild(header);

    // rows, newest first
    float y = size.height - kHeaderH - kRowH * 0.5f;
    int shown = 0;
    for (size_t i = visibleInputs; i-- > 0 && shown < m_rows;) {
        auto const& in = run.inputs[i];
        if (!m_showIneffective && in.state == EvalState::NoEffect) continue;

        auto name = makeLabel(
            fmt::format("#{} {}", in.number, in.press ? "Click" : "Release"),
            0.26f, in.press ? ccColor3B{230, 230, 230} : ccColor3B{170, 190, 255}, {0.f, 0.5f}
        );
        name->setPosition({kPad, y});
        m_content->addChild(name);

        auto win = makeLabel(windowLabel(in), 0.3f, windowColor(in), {1.f, 0.5f});
        if (m_showOffsets) {
            win->setPosition({kWidth * 0.62f, y});
            auto off = makeLabel(offsetsText(in), 0.2f, {160, 160, 160}, {1.f, 0.5f});
            off->setPosition({kWidth - kPad, y});
            m_content->addChild(off);
        }
        else {
            win->setPosition({kWidth - kPad, y});
        }
        m_content->addChild(win);

        y -= kRowH;
        ++shown;
    }
    if (shown == 0) {
        auto empty = makeLabel(
            replaying ? "replay starting..." : tr.hasRunInProgress() || lastRun ? "click to start..." : "waiting for level...",
            0.24f, {150, 150, 150}, {0.f, 0.5f}
        );
        empty->setPosition({kPad, y});
        m_content->addChild(empty);
    }

    // footer
    std::string footer;
    if (auto h = run.hardest()) {
        footer = fmt::format("hardest: #{} {}f   counted: {}", h->number, windowLabel(*h), run.effectiveCount());
    }
    else {
        footer = fmt::format("counted: {}", run.effectiveCount());
    }
    if (lastRun) footer += "   (pause > FW for report)";
    if (replaying) footer = "replay - your inputs are paused";
    auto foot = makeLabel(footer, 0.22f, {200, 200, 200}, {0.f, 0.5f});
    foot->setPosition({kPad, kFooterH * 0.5f + 1.f});
    foot->limitLabelWidth(kWidth - kPad * 2, 0.22f, 0.1f);
    m_content->addChild(foot);
}

} // namespace fw
