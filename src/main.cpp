#include <Geode/Geode.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "Hud.hpp"
#include "ReportPopup.hpp"
#include "Tracker.hpp"

using namespace geode::prelude;
using fw::Tracker;

static PlayLayer* playLayerFor(GJBaseGameLayer* layer) {
    auto pl = PlayLayer::get();
    if (pl && static_cast<GJBaseGameLayer*>(pl) == layer) return pl;
    return nullptr; // editor playtest or something else
}

static bool isRealPlayer1(PlayerObject* p) {
    auto pl = PlayLayer::get();
    return pl && p == pl->m_player1 && !Tracker::get().isSimulating();
}

// ---------------------------------------------------------------------------
// physics tick + collision filtering for ghosts

class $modify(FWBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::collisionCheckObjects", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::canBeActivatedByPlayer", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::playerTouchedRing", Priority::First);
        (void)self.setHookPriority("GJBaseGameLayer::flipGravity", Priority::First);
    }

    // called once per physics tick (240 per second in 2.2)
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto pl = playLayerFor(this);
        if (!pl) {
            GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
            return;
        }
        auto& tr = Tracker::get();
        tr.onTickStart(pl);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        tr.onTickEnd(pl);
    }

    // ghosts only collide with blocks, hazards and slopes: no orbs, pads, portals,
    // triggers or coins, so they can never change the real level
    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int objectCount, float dt) {
        if (!Tracker::get().isSimulating()) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
            return;
        }
        static gd::vector<GameObject*> filtered;
        filtered.clear();
        for (int i = 0; i < objectCount; ++i) {
            auto obj = objects->at(i);
            if (!obj) continue;
            auto t = obj->m_objectType;
            if (t == GameObjectType::Solid || t == GameObjectType::Hazard ||
                t == GameObjectType::AnimatedHazard || t == GameObjectType::Slope) {
                filtered.push_back(obj);
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, &filtered, static_cast<int>(filtered.size()), dt);
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        if (Tracker::get().isSimulating()) return false;
        return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (Tracker::get().isSimulating()) return;
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        auto& tr = Tracker::get();
        if (tr.isSimulating()) {
            // flip only the test player itself (never the real players, no effects)
            static bool s_inFlip = false;
            if (!s_inFlip && tr.isGhost(player)) {
                s_inFlip = true;
                player->flipGravity(flip, true);
                s_inFlip = false;
            }
            return;
        }
        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }
};

// ---------------------------------------------------------------------------
// player: catch every kind of input (mouse, any key, controller, touch)

class $modify(FWPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::ringJump", Priority::First);
        (void)self.setHookPriority("PlayerObject::incrementJumps", Priority::First);
        (void)self.setHookPriority("PlayerObject::playSpiderDashEffect", Priority::First);
    }

    bool pushButton(PlayerButton button) {
        bool ret = PlayerObject::pushButton(button);
        if (button == PlayerButton::Jump && isRealPlayer1(this)) {
            Tracker::get().onRealButton(PlayLayer::get(), true);
        }
        return ret;
    }

    bool releaseButton(PlayerButton button) {
        bool ret = PlayerObject::releaseButton(button);
        if (button == PlayerButton::Jump && isRealPlayer1(this)) {
            Tracker::get().onRealButton(PlayLayer::get(), false);
        }
        return ret;
    }

    void update(float dt) {
        PlayerObject::update(dt);
        if (isRealPlayer1(this)) Tracker::get().onRealUpdate(dt);
    }

    void ringJump(RingObject* ring, bool skipCheck) {
        if (Tracker::get().isSimulating()) return;
        if (!isRealPlayer1(this)) {
            PlayerObject::ringJump(ring, skipCheck);
            return;
        }
        double vy = m_yVelocity;
        bool up = m_isUpsideDown;
        bool dash = m_isDashing;
        PlayerObject::ringJump(ring, skipCheck);
        Tracker::get().onRealRing(ring, this, vy, up, dash);
    }

    void bumpPlayer(float bumpMod, int objectType, bool noEffects, GameObject* object) {
        PlayerObject::bumpPlayer(bumpMod, objectType, noEffects, object);
        if (isRealPlayer1(this)) Tracker::get().onRealPad(object, this);
    }

    void playSpiderDashEffect(cocos2d::CCPoint from, cocos2d::CCPoint to) {
        if (Tracker::get().isSimulating()) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }

    void incrementJumps() {
        if (Tracker::get().isSimulating()) return; // don't add ghost jumps to your stats
        PlayerObject::incrementJumps();
    }
};

class $modify(FWHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint point) {
        if (Tracker::get().isSimulating()) return; // no wave trail for ghosts
        HardStreak::addPoint(point);
    }
};

class $modify(FWEffectGameObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int uniqueID, gd::vector<int> const* remapKeys) {
        if (Tracker::get().isSimulating()) return;
        EffectGameObject::triggerObject(layer, uniqueID, remapKeys);
    }
};

class $modify(FWGameObject, GameObject) {
    void playShineEffect() {
        if (Tracker::get().isSimulating()) return;
        GameObject::playShineEffect();
    }
};

// ---------------------------------------------------------------------------
// level lifecycle

class $modify(FWPlayLayer, PlayLayer) {
    // run before MegaHack / Eclipse / other mods so a test player's "death" never
    // reaches noclip, hitboxes-on-death, death effects or attempt counters
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", Priority::First);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        Tracker::get().beginLevel(this);
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
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
        auto& tr = Tracker::get();
        // a test player hit something: that timing fails, nothing happens in the real game
        if (tr.isGhost(player)) {
            tr.onGhostDeath(player, object);
            return;
        }
        // the hidden anti-cheat spike at level start
        if (object == m_anticheatSpike) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        PlayLayer::destroyPlayer(player, object);
        // noclip mods stop the death, so check that it really happened
        if (player == m_player1 && m_player1 && m_player1->m_isDead && tr.hasRunInProgress()) {
            tr.finishRun(this, false);
            if (Mod::get()->getSettingValue<bool>("report-on-death") && !tr.history().empty()) {
                if (auto popup = fw::ReportPopup::create()) popup->show();
            }
        }
    }

    void playEndAnimationToPos(cocos2d::CCPoint position) {
        if (Tracker::get().isSimulating()) return;
        PlayLayer::playEndAnimationToPos(position);
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

// ---------------------------------------------------------------------------
// pause menu button

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
            auto win = CCDirector::get()->getWinSize();
            auto menu2 = CCMenu::create();
            menu2->setPosition({win.width - 30.f, win.height * 0.5f});
            menu2->addChild(btn);
            this->addChild(menu2, 10);
        }
    }
};
