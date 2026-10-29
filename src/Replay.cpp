#include "Replay.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/binding/LevelSettingsObject.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>

#include <cmath>

using namespace geode::prelude;

namespace fw {

static constexpr float kTicksPerSecond = 240.f;

std::string labelText(InputRecord const& in) {
    std::string text = windowLabel(in);
    if (in.state == EvalState::Done && !in.isFree()) text += "f";
    if (!in.press) text = "R " + text;
    return text;
}

CCLabelBMFont* makeWorldLabel(InputRecord const& in) {
    auto label = CCLabelBMFont::create(labelText(in).c_str(), "bigFont.fnt");
    label->setScale(in.press ? 0.55f : 0.4f);
    label->setColor(windowColor(in));
    label->setZOrder(1000);
    return label;
}

Replay& Replay::get() {
    static Replay inst;
    return inst;
}

int Replay::totalTicks() const {
    return m_run.replay ? static_cast<int>(m_run.replay->size()) : 0;
}

void Replay::request(RunRecord const& run) {
    m_run = run;
    m_pending = run.replay && !run.replay->empty();
}

void Replay::begin(PlayLayer* pl) {
    m_pending = false;
    if (!pl || !pl->m_player1 || !m_run.replay || m_run.replay->empty()) return;
    // a run started from a practice checkpoint can't be replayed from the start
    if (std::abs(pl->m_player1->m_position.x - m_run.startX) > 2.f) {
        FLAlertLayer::create(
            "Frame Window Counter",
            "This run started from a <cy>practice checkpoint</c>, so it can't be replayed from the start of the level.",
            "OK"
        )->show();
        return;
    }
    m_active = true;
    m_frozen = false;
    m_tick = 0;
    m_nextInput = 0;
    if (!m_bar) {
        m_bar = ReplayBar::create();
        if (m_bar) {
            m_bar->retain();
            pl->addChild(m_bar, 1001);
        }
    }
    ++m_revision;
}

void Replay::onRestarted() {
    m_frozen = false;
    m_tick = 0;
    m_nextInput = 0;
    ++m_revision;
}

void Replay::stop() {
    m_active = false;
    m_pending = false;
    m_injecting = false;
    m_frozen = false;
    m_fastForward = false;
    if (m_bar) {
        m_bar->removeFromParentAndCleanup(true);
        m_bar->release();
        m_bar = nullptr;
    }
    ++m_revision;
}

void Replay::exitReplay() {
    stop();
    if (auto pl = PlayLayer::get()) pl->resetLevelFromStart();
}

void Replay::popLabel(PlayLayer* pl, InputRecord const& in) {
    if (!pl->m_objectLayer || !pl->m_player1) return;
    if (in.state == EvalState::NoEffect && !Mod::get()->getSettingValue<bool>("show-ineffective")) return;
    // while fast-forwarding only show the last second before where you jump to
    if (m_fastForward && m_ffTarget - m_tick > static_cast<int>(kTicksPerSecond)) return;

    auto label = makeWorldLabel(in);
    auto pos = pl->m_player1->getPosition();
    label->setPosition({pos.x, pos.y + (in.press ? 32.f : 22.f)});
    pl->m_objectLayer->addChild(label);
    label->runAction(CCSequence::create(
        CCSpawn::create(CCMoveBy::create(1.4f, {0.f, 28.f}),
                        CCSequence::create(CCDelayTime::create(0.8f), CCFadeOut::create(0.6f), nullptr),
                        nullptr),
        CCRemoveSelf::create(),
        nullptr
    ));
}

void Replay::onTickStart(PlayLayer* pl) {
    if (!m_active || !pl || !pl->m_player1) return;
    auto p = pl->m_player1;
    auto const& frames = *m_run.replay;

    // keep the player exactly on the recorded path
    if (m_tick > 0 && m_tick - 1 < static_cast<int>(frames.size())) {
        auto const& f = frames[m_tick - 1];
        if (std::abs(p->m_position.y - f.y) > 0.01f || std::abs(p->m_position.x - f.x) > 0.5f) {
            p->m_position = CCPoint(f.x, f.y);
            p->setPosition({f.x, f.y});
            p->m_yVelocity = f.vy;
        }
    }

    // press / release exactly when you did
    while (m_nextInput < m_run.inputs.size() &&
           static_cast<int>(std::floor(m_run.inputs[m_nextInput].time + 1e-6)) <= m_tick) {
        auto const& in = m_run.inputs[m_nextInput];
        m_injecting = true;
        pl->handleButton(in.press, 1, true);
        m_injecting = false;
        popLabel(pl, in);
        ++m_nextInput;
        ++m_revision;
    }
}

void Replay::onTickEnd(PlayLayer*) {
    if (!m_active) return;
    ++m_tick;
    // stay on the last frame of the run
    if (m_tick >= totalTicks() && !m_frozen) {
        m_frozen = true;
        FMODAudioEngine::sharedEngine()->stopAllMusic(true);
        ++m_revision;
    }
}

void Replay::syncMusic(PlayLayer* pl) {
    auto engine = FMODAudioEngine::sharedEngine();
    engine->stopAllMusic(true);
    if (m_frozen) return;
    pl->startMusic();
    double offset = pl->m_levelSettings ? pl->m_levelSettings->m_songOffset : 0.0;
    double ms = (pl->m_gameState.m_levelTime + offset) * 1000.0;
    if (ms > 0.0) engine->setMusicTimeMS(static_cast<unsigned int>(ms), true, 0);
}

void Replay::seekTo(int target) {
    auto pl = PlayLayer::get();
    if (!m_active || !pl || m_fastForward) return;
    target = std::clamp(target, 0, totalTicks());

    if (target < m_tick || m_frozen) {
        if (target >= m_tick && m_frozen) return; // already at the end
        m_restarting = true;
        pl->resetLevelFromStart();
        m_restarting = false;
        if (!m_active) return;
    }

    // run the game quickly up to the target
    m_fastForward = true;
    m_ffTarget = target;
    int guard = 0;
    while (m_tick < target && !m_frozen && m_active && guard++ < 200000) {
        pl->update(1.f / 60.f);
    }
    m_fastForward = false;
    if (m_active) syncMusic(pl);
    ++m_revision;
}

void Replay::seekBySeconds(float seconds) {
    seekTo(m_tick + static_cast<int>(seconds * kTicksPerSecond));
}

// ---------------------------------------------------------------------------
// replay controls

ReplayBar* ReplayBar::create() {
    auto ret = new ReplayBar();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ReplayBar::init() {
    if (!CCNode::init()) return false;
    this->setID("replay-bar"_spr);
    auto win = CCDirector::get()->getWinSize();
    float h = 34.f;

    auto bg = CCLayerColor::create(ccColor4B{0, 0, 0, 150}, win.width, h);
    bg->setPosition({0.f, 0.f});
    this->addChild(bg);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    this->addChild(menu, 2);

    auto makeBtn = [&](char const* text, float x, auto cb) {
        auto spr = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_04.png", 0.6f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, cb);
        btn->setScale(0.55f);
        btn->m_baseScale = 0.55f;
        btn->setPosition({x, h * 0.5f});
        menu->addChild(btn);
        return btn;
    };
    makeBtn("Exit", 32.f, [](CCMenuItemSpriteExtra*) { Replay::get().exitReplay(); });
    makeBtn("<< 5s", 88.f, [](CCMenuItemSpriteExtra*) { Replay::get().seekBySeconds(-5.f); });
    makeBtn("5s >>", win.width - 120.f, [](CCMenuItemSpriteExtra*) { Replay::get().seekBySeconds(5.f); });

    m_slider = Slider::create(this, menu_selector(ReplayBar::onSlider), 0.75f);
    m_slider->setPosition({(130.f + win.width - 160.f) * 0.5f, h * 0.5f});
    this->addChild(m_slider, 2);

    m_time = CCLabelBMFont::create("0.0s / 0.0s", "bigFont.fnt");
    m_time->setScale(0.3f);
    m_time->setAnchorPoint({1.f, 0.5f});
    m_time->setPosition({win.width - 6.f, h * 0.5f});
    this->addChild(m_time, 2);

    m_status = CCLabelBMFont::create("", "goldFont.fnt");
    m_status->setScale(0.55f);
    m_status->setPosition({win.width * 0.5f, h + 14.f});
    this->addChild(m_status, 2);

    this->scheduleUpdate();
    this->refresh();
    return true;
}

void ReplayBar::onSlider(CCObject* sender) {
    if (auto thumb = typeinfo_cast<SliderThumb*>(sender)) {
        m_pendingValue = thumb->getValue();
        m_sinceChange = 0.f;
    }
}

void ReplayBar::update(float dt) {
    // seek once you stop moving the slider for a moment
    if (m_pendingValue >= 0.f) {
        m_sinceChange += dt;
        if (m_sinceChange > 0.25f) {
            float v = m_pendingValue;
            m_pendingValue = -1.f;
            auto& rp = Replay::get();
            rp.seekTo(static_cast<int>(v * rp.totalTicks()));
        }
    }
    this->refresh();
}

void ReplayBar::refresh() {
    auto& rp = Replay::get();
    int total = std::max(1, rp.totalTicks());
    if (m_pendingValue < 0.f && m_slider) {
        m_slider->setValue(std::clamp(static_cast<float>(rp.tick()) / total, 0.f, 1.f));
    }
    if (m_time) {
        m_time->setString(fmt::format("{:.1f}s / {:.1f}s", rp.tick() / kTicksPerSecond, total / kTicksPerSecond).c_str());
    }
    if (m_status) {
        m_status->setString(rp.frozen()
            ? (rp.run().completed ? "Replay finished - level complete!" : fmt::format("Replay finished - {:.1f}%", rp.run().endPercent).c_str())
            : "");
    }
}

} // namespace fw
