#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "Hud.hpp"
#include "ReportPopup.hpp"
#include "Tracker.hpp"

using namespace geode::prelude;
using fw::Tracker;

static PlayLayer* currentPlayLayerFor(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    if (pl && static_cast<GJBaseGameLayer*>(pl) == layer) return pl;
    return nullptr; // editor playtest or something else
}

class $modify(FWBaseGameLayer, GJBaseGameLayer) {
    // called once per physics tick (240 per second in 2.2)
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto pl = currentPlayLayerFor(this);
        if (!pl) {
            GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            return;
        }
        auto& tr = Tracker::get();
        tr.onTickStart(pl);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        tr.onTickEnd(pl);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
        // button 1 = jump. Only player 1 is tracked.
        if (button != 1 || !isPlayer1) return;
        if (auto pl = currentPlayLayerFor(this)) {
            Tracker::get().onButton(pl, down);
        }
    }
};

class $modify(FWPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        Tracker::get().beginLevel(this);
        if (auto hud = fw::FrameHud::create()) {
            this->addChild(hud, 1000);
        }
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        Tracker::get().startRun(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // the game "kills" you with a hidden anti-cheat spike when the level loads; ignore it
        if (object == m_anticheatSpike) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        PlayLayer::destroyPlayer(player, object);
        // noclip mods stop the death, so check that it really happened
        if (player == m_player1 && m_player1 && m_player1->m_isDead) {
            auto& tr = Tracker::get();
            if (tr.hasRunInProgress()) {
                tr.finishRun(this, false);
                if (Mod::get()->getSettingValue<bool>("report-on-death") && !tr.history().empty()) {
                    if (auto popup = fw::ReportPopup::create()) popup->show();
                }
            }
        }
    }

    void levelComplete() {
        Tracker::get().finishRun(this, true);
        PlayLayer::levelComplete();
    }

    void onQuit() {
        Tracker::get().endLevel(this);
        PlayLayer::onQuit();
    }
};

class $modify(FWPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto spr = CircleButtonSprite::createWithSpriteFrameName(
            "GJ_statsBtn_001.png", 0.9f, CircleBaseColor::Green, CircleBaseSize::Small
        );
        if (!spr) return;
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
            if (auto popup = fw::ReportPopup::create()) popup->show();
        });
        btn->setID("frame-window-report-btn"_spr);

        if (auto menu = this->getChildByID("right-button-menu")) {
            menu->addChild(btn);
            menu->updateLayout();
        }
        else {
            // fallback if node IDs are missing
            auto win = CCDirector::get()->getWinSize();
            auto menu2 = CCMenu::create();
            menu2->setPosition({win.width - 30.f, win.height * 0.5f});
            menu2->addChild(btn);
            this->addChild(menu2, 10);
        }
    }
};
