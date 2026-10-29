#include "Replay.hpp"

#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/FLAlertLayer.hpp>

#include <cmath>

using namespace geode::prelude;

namespace fw {

Replay& Replay::get() {
    static Replay inst;
    return inst;
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
    m_tick = 0;
    m_nextInput = 0;
    ++m_revision;
}

void Replay::stop() {
    m_active = false;
    m_pending = false;
    m_injecting = false;
    ++m_revision;
}

void Replay::popLabel(PlayLayer* pl, InputRecord const& in) {
    if (!pl->m_objectLayer || !pl->m_player1) return;
    if (in.state == EvalState::NoEffect && !Mod::get()->getSettingValue<bool>("show-ineffective")) return;

    std::string text = windowLabel(in);
    if (in.state == EvalState::Done && !in.isFree()) text += "f";
    if (!in.press) text = "R " + text;
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(in.press ? 0.55f : 0.4f);
    label->setColor(windowColor(in));
    auto pos = pl->m_player1->getPosition();
    label->setPosition({pos.x, pos.y + (in.press ? 32.f : 22.f)});
    label->setZOrder(1000);
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
    if (m_tick > 0 && m_tick - 1 < static_cast<int>(frames.size()) && !p->m_isDead) {
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
    // a bit after the recording ends, hand control back
    if (m_tick > static_cast<int>(m_run.replay->size()) + 240) stop();
}

} // namespace fw
