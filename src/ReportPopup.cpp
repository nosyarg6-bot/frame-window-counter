#include "ReportPopup.hpp"
#include "Hud.hpp"
#include "Replay.hpp"

#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;

namespace fw {

static constexpr float kPopupW = 440.f;
static constexpr float kPopupH = 290.f;
static constexpr float kListW = 400.f;
static constexpr float kListH = 150.f;
static constexpr float kRowH = 16.f;

static CCLabelBMFont* label(std::string const& text, char const* font, float scale, ccColor3B color = {255, 255, 255}) {
    auto l = CCLabelBMFont::create(text.c_str(), font);
    l->setScale(scale);
    l->setColor(color);
    return l;
}

ReportPopup* ReportPopup::create() {
    auto ret = new ReportPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ReportPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    this->setTitle("Frame Window Report");

    auto& tr = Tracker::get();
    if (tr.hasRunInProgress() && !tr.current().inputs.empty()) {
        m_runs.push_back(tr.current());
        m_runNames.push_back("Current run");
    }
    int i = 1;
    for (auto const& run : tr.history()) {
        m_runs.push_back(run);
        m_runNames.push_back(i == 1 ? "Previous run" : fmt::format("{} runs ago", i));
        ++i;
    }
    // open on the run that just ended when there is one
    m_index = 0;
    if (!m_runs.empty() && m_runNames[0] == "Current run" && m_runs.size() > 1 && tr.showingLastRun()) {
        m_index = 1;
    }

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(m_size);
    m_mainLayer->addChild(menu, 5);

    // run selector
    auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    prevSpr->setScale(0.45f);
    m_prevBtn = CCMenuItemExt::createSpriteExtra(prevSpr, [this](CCMenuItemSpriteExtra*) {
        this->showRun(m_index - 1);
    });
    m_prevBtn->setPosition({kPopupW * 0.5f - 150.f, kPopupH - 48.f});
    menu->addChild(m_prevBtn);

    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    nextSpr->setScale(0.45f);
    nextSpr->setFlipX(true);
    m_nextBtn = CCMenuItemExt::createSpriteExtra(nextSpr, [this](CCMenuItemSpriteExtra*) {
        this->showRun(m_index + 1);
    });
    m_nextBtn->setPosition({kPopupW * 0.5f + 150.f, kPopupH - 48.f});
    menu->addChild(m_nextBtn);

    m_runLabel = label("", "goldFont.fnt", 0.55f);
    m_runLabel->setPosition({kPopupW * 0.5f, kPopupH - 45.f});
    m_mainLayer->addChild(m_runLabel);

    m_infoLabel = label("", "chatFont.fnt", 0.6f, {220, 220, 220});
    m_infoLabel->setPosition({kPopupW * 0.5f, kPopupH - 64.f});
    m_mainLayer->addChild(m_infoLabel);

    m_summary = CCNode::create();
    m_summary->setPosition({0.f, kPopupH - 82.f});
    m_mainLayer->addChild(m_summary);

    // column headers
    float hy = kPopupH - 100.f;
    float lx = (kPopupW - kListW) * 0.5f;
    auto addHeader = [&](char const* text, float x, float anchorX) {
        auto l = label(text, "bigFont.fnt", 0.3f, {255, 220, 120});
        l->setAnchorPoint({anchorX, 0.5f});
        l->setPosition({lx + x, hy});
        m_mainLayer->addChild(l);
    };
    addHeader("#", 8.f, 0.f);
    addHeader("Input", 40.f, 0.f);
    addHeader("At", 88.f, 0.f);
    addHeader("Window", 172.f, 0.5f);
    addHeader("Early / Late", 245.f, 0.5f);
    addHeader("Obstacle", 300.f, 0.f);

    auto listBg = CCLayerColor::create(ccColor4B{0, 0, 0, 90}, kListW, kListH);
    listBg->setPosition({lx, hy - 10.f - kListH});
    m_mainLayer->addChild(listBg);

    m_scroll = ScrollLayer::create(CCSize{kListW, kListH});
    m_scroll->setPosition({lx, hy - 10.f - kListH});
    m_mainLayer->addChild(m_scroll, 2);

    // bottom buttons
    auto moveBtn = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Move Overlay", "bigFont.fnt", "GJ_button_04.png", 0.6f),
        [this](CCMenuItemSpriteExtra*) {
            MoveHudLayer::open();
            this->onClose(nullptr);
        }
    );
    moveBtn->setScale(0.7f);
    moveBtn->m_baseScale = 0.7f;
    moveBtn->setPosition({kPopupW * 0.5f, 20.f});
    menu->addChild(moveBtn);

    // in-game overlay on / off
    auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.55f, [](CCMenuItemToggler* t) {
        bool on = !t->isToggled();
        Mod::get()->setSettingValue<bool>("enabled", on);
        if (auto hud = FrameHud::get()) hud->applySettings();
    });
    toggler->toggle(Mod::get()->getSettingValue<bool>("enabled"));
    toggler->setPosition({30.f, 20.f});
    menu->addChild(toggler);
    auto togLabel = label("Overlay", "bigFont.fnt", 0.32f);
    togLabel->setAnchorPoint({0.f, 0.5f});
    togLabel->setPosition({46.f, 20.f});
    m_mainLayer->addChild(togLabel);

    // replay the selected run
    m_replayBtn = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Watch Replay", "bigFont.fnt", "GJ_button_01.png", 0.6f),
        [this](CCMenuItemSpriteExtra*) {
            if (m_index < 0 || m_index >= static_cast<int>(m_runs.size())) return;
            Replay::get().request(m_runs[m_index]);
            if (!Replay::get().pending()) return;
            this->onClose(nullptr);
            if (auto pause = CCScene::get()->getChildByType<PauseLayer>(0)) {
                pause->onRestartFull(nullptr);
            }
            else if (auto pl = PlayLayer::get()) {
                pl->resetLevelFromStart();
            }
        }
    );
    m_replayBtn->setScale(0.7f);
    m_replayBtn->m_baseScale = 0.7f;
    m_replayBtn->setPosition({kPopupW - 70.f, 20.f});
    menu->addChild(m_replayBtn);

    m_replayNote = label("", "chatFont.fnt", 0.4f, {170, 170, 170});
    m_replayNote->setPosition({kPopupW - 70.f, 6.f});
    m_mainLayer->addChild(m_replayNote);

    auto legend = label("240 FPS frames.  0-1 = needs more than 240 FPS.  ~ = estimate   * = cut short", "chatFont.fnt", 0.45f, {170, 170, 170});
    legend->setPosition({kPopupW * 0.5f, 40.f});
    m_mainLayer->addChild(legend);

    this->showRun(m_index);
    return true;
}

void ReportPopup::showRun(int index) {
    m_summary->removeAllChildren();
    m_scroll->m_contentLayer->removeAllChildren();

    if (m_runs.empty()) {
        m_runLabel->setString("No runs yet");
        m_infoLabel->setString("Play the level - every click and release gets measured.");
        m_prevBtn->setVisible(false);
        m_nextBtn->setVisible(false);
        m_replayBtn->setEnabled(false);
        return;
    }
    m_index = std::clamp(index, 0, static_cast<int>(m_runs.size()) - 1);
    m_prevBtn->setVisible(m_index > 0);
    m_nextBtn->setVisible(m_index < static_cast<int>(m_runs.size()) - 1);

    auto const& run = m_runs[m_index];
    bool canReplay = run.finished && run.replay && !run.replay->empty() && PlayLayer::get();
    m_replayBtn->setEnabled(canReplay);
    if (auto spr = typeinfo_cast<ButtonSprite*>(m_replayBtn->getNormalImage())) {
        spr->setColor(canReplay ? ccColor3B{255, 255, 255} : ccColor3B{110, 110, 110});
        spr->setOpacity(canReplay ? 255 : 150);
    }
    m_replayNote->setString(canReplay ? "" : !run.finished ? "finish the run first" : "only the last 5 runs");
    m_runLabel->setString(fmt::format("{} ({}/{})", m_runNames[m_index], m_index + 1, m_runs.size()).c_str());

    std::string result;
    if (!run.finished) result = "in progress";
    else if (run.completed) result = "completed!";
    else result = fmt::format("ended at {:.1f}%", run.endPercent);
    m_infoLabel->setString(fmt::format(
        "{}  -  attempt {}  -  {}{}", run.levelName.empty() ? "Level" : run.levelName,
        run.attempt, result, run.practice ? "  (practice)" : ""
    ).c_str());
    m_infoLabel->limitLabelWidth(kPopupW - 30.f, 0.6f, 0.3f);

    this->buildSummary(run);
    this->buildList(run);
}

void ReportPopup::buildSummary(RunRecord const& run) {
    struct Bucket { char const* name; float lo, hi; ccColor3B color; int count = 0; };
    std::vector<Bucket> buckets = {
        {"0-1", 0.f, 1.f, {255, 60, 220}},
        {"1-2", 1.f, 2.f, {255, 70, 70}},
        {"2-3", 2.f, 3.f, {255, 150, 50}},
        {"3-4", 3.f, 4.f, {255, 230, 60}},
        {"4-6", 4.f, 6.f, {150, 255, 80}},
        {"6-10", 6.f, 10.f, {80, 220, 255}},
        {"10+", 10.f, 1e9f, {200, 200, 200}},
    };
    int freeCount = 0;
    for (auto const& in : run.inputs) {
        if (in.state != EvalState::Done) continue;
        if (in.isFree()) { ++freeCount; continue; }
        for (auto& b : buckets) {
            if (in.windowFrames >= b.lo && in.windowFrames < b.hi) { ++b.count; break; }
        }
    }
    float slotW = kListW / (buckets.size() + 1);
    float x = (kPopupW - kListW) * 0.5f + slotW * 0.5f;
    for (auto const& b : buckets) {
        auto l = label(fmt::format("{}: {}", b.name, b.count), "bigFont.fnt", 0.28f, b.color);
        l->setPosition({x, 0.f});
        m_summary->addChild(l);
        x += slotW;
    }
    auto f = label(fmt::format("free: {}", freeCount), "bigFont.fnt", 0.28f, {200, 200, 200});
    f->setPosition({x, 0.f});
    m_summary->addChild(f);
}

void ReportPopup::buildList(RunRecord const& run) {
    auto content = m_scroll->m_contentLayer;
    std::vector<InputRecord const*> rows;
    bool showIneffective = Mod::get()->getSettingValue<bool>("show-ineffective");
    for (auto const& in : run.inputs) {
        if (!showIneffective && in.state == EvalState::NoEffect) continue;
        rows.push_back(&in);
    }

    float height = std::max(kListH, rows.size() * kRowH + 4.f);
    content->setContentSize({kListW, height});

    if (rows.empty()) {
        auto l = label("No measured inputs in this run", "chatFont.fnt", 0.6f, {170, 170, 170});
        l->setPosition({kListW * 0.5f, height - 14.f});
        content->addChild(l);
    }

    float y = height - kRowH * 0.5f - 2.f;
    bool shade = false;
    for (auto in : rows) {
        if (shade) {
            auto stripe = CCLayerColor::create(ccColor4B{255, 255, 255, 14}, kListW, kRowH);
            stripe->setPosition({0.f, y - kRowH * 0.5f});
            content->addChild(stripe);
        }
        shade = !shade;

        auto add = [&](std::string const& text, float x, float anchorX, ccColor3B color, float scale = 0.6f) {
            auto l = label(text, "chatFont.fnt", scale, color);
            l->setAnchorPoint({anchorX, 0.5f});
            l->setPosition({x, y});
            content->addChild(l);
        };
        add(std::to_string(in->number), 8.f, 0.f, {200, 200, 200});
        add(in->press ? "Click" : "Release", 40.f, 0.f, in->press ? ccColor3B{255, 255, 255} : ccColor3B{170, 190, 255});
        add(fmt::format("{:.1f}%", in->percent), 88.f, 0.f, {200, 200, 200});

        std::string win = windowLabel(*in);
        if (in->state == EvalState::NoEffect) win = "no effect";
        if (in->state == EvalState::Unknown) win = "can't measure";
        auto wl = label(win, "bigFont.fnt", 0.32f, windowColor(*in));
        wl->setPosition({172.f, y});
        content->addChild(wl);

        if (in->state == EvalState::Done) {
            add(fmt::format("-{} / +{}",
                    in->cappedEarly ? std::string("max") : fmt::format("{:.2f}", in->earlyFrames),
                    in->cappedLate ? std::string("max") : fmt::format("{:.2f}", in->lateFrames)),
                245.f, 0.5f, {180, 180, 180}, 0.5f);
            std::string obs = in->obstacle;
            if (!obs.empty() && in->obstaclePercent >= 0.f) obs += fmt::format(" {:.1f}%", in->obstaclePercent);
            if (obs.empty()) obs = in->isFree() ? "" : "-";
            add(obs, 300.f, 0.f, {230, 200, 160}, 0.5f);
        }
        y -= kRowH;
    }
    m_scroll->scrollToTop();
}

// ---------------------------------------------------------------------------

MoveHudLayer* MoveHudLayer::create() {
    auto ret = new MoveHudLayer();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void MoveHudLayer::open() {
    auto scene = CCDirector::get()->getRunningScene();
    if (!scene) return;
    if (scene->getChildByID("move-hud-layer"_spr)) return;
    if (auto layer = MoveHudLayer::create()) {
        scene->addChild(layer, 10000);
    }
}

bool MoveHudLayer::init() {
    if (!CCLayer::init()) return false;
    this->setID("move-hud-layer"_spr);
    auto win = CCDirector::get()->getWinSize();

    auto dim = CCLayerColor::create(ccColor4B{0, 0, 0, 120});
    this->addChild(dim);

    CCSize size{176.f, 120.f};
    float scale = static_cast<float>(Mod::get()->getSettingValue<double>("hud-scale"));
    if (auto hud = FrameHud::get()) size = hud->panelSize();

    m_box = CCLayerColor::create(ccColor4B{80, 200, 255, 110}, size.width, size.height);
    m_box->ignoreAnchorPointForPosition(false);
    m_box->setAnchorPoint({0.f, 1.f});
    m_box->setScale(scale);
    this->addChild(m_box);

    auto boxLabel = CCLabelBMFont::create("Drag me", "bigFont.fnt");
    boxLabel->setScale(0.4f);
    boxLabel->setPosition({size.width * 0.5f, size.height * 0.5f});
    m_box->addChild(boxLabel);

    auto hint = CCLabelBMFont::create("Drag the box to move the frame window overlay", "chatFont.fnt");
    hint->setScale(0.7f);
    hint->setPosition({win.width * 0.5f, win.height * 0.5f + 40.f});
    this->addChild(hint);

    m_doneBtn = ButtonSprite::create("Done", "goldFont.fnt", "GJ_button_01.png", 0.8f);
    m_doneBtn->setPosition({win.width * 0.5f + 50.f, win.height * 0.5f});
    this->addChild(m_doneBtn);

    m_resetBtn = ButtonSprite::create("Reset", "goldFont.fnt", "GJ_button_06.png", 0.8f);
    m_resetBtn->setPosition({win.width * 0.5f - 50.f, win.height * 0.5f});
    this->addChild(m_resetBtn);

    m_offset = FrameHud::savedOffset();
    this->placeBox();

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    return true;
}

void MoveHudLayer::registerWithTouchDispatcher() {
    CCTouchDispatcher::get()->addTargetedDelegate(this, -1000, true);
}

void MoveHudLayer::placeBox() {
    auto win = CCDirector::get()->getWinSize();
    m_box->setPosition({m_offset.x, win.height - m_offset.y});
}

static bool hit(CCNode* node, CCPoint worldPt) {
    auto local = node->convertToNodeSpace(worldPt);
    auto size = node->getContentSize();
    return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
}

bool MoveHudLayer::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto pt = touch->getLocation();
    if (hit(m_doneBtn, pt)) {
        FrameHud::saveOffset(m_offset);
        this->close();
        return true;
    }
    if (hit(m_resetBtn, pt)) {
        m_offset = CCPoint(5.f, 5.f);
        this->placeBox();
        return true;
    }
    if (hit(m_box, pt)) {
        m_dragging = true;
        m_dragStart = pt;
        m_offsetStart = m_offset;
    }
    return true; // swallow everything while moving the overlay
}

void MoveHudLayer::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_dragging) return;
    auto pt = touch->getLocation();
    auto win = CCDirector::get()->getWinSize();
    auto boxSize = m_box->getContentSize() * m_box->getScale();
    m_offset.x = std::clamp(m_offsetStart.x + (pt.x - m_dragStart.x), 0.f, std::max(0.f, win.width - boxSize.width));
    m_offset.y = std::clamp(m_offsetStart.y - (pt.y - m_dragStart.y), 0.f, std::max(0.f, win.height - boxSize.height));
    this->placeBox();
}

void MoveHudLayer::ccTouchEnded(CCTouch*, CCEvent*) {
    m_dragging = false;
}

void MoveHudLayer::keyBackClicked() {
    FrameHud::saveOffset(m_offset);
    this->close();
}

void MoveHudLayer::close() {
    if (auto hud = FrameHud::get()) hud->applySettings();
    this->setTouchEnabled(false);
    this->setKeypadEnabled(false);
    this->removeFromParent();
}

} // namespace fw
