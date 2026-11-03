#include "MicPopup.hpp"
#include "Mic.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>

using namespace geode::prelude;

namespace fw {

static constexpr float kW = 340.f;
static constexpr float kH = 260.f;

MicPopup* MicPopup::create() {
    auto ret = new MicPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool MicPopup::init() {
    if (!Popup::init(kW, kH)) return false;
    this->setTitle("Microphone");

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(m_size);
    m_mainLayer->addChild(menu, 5);

    auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f, [](CCMenuItemToggler* t) {
        Mod::get()->setSettingValue<bool>("mic-enabled", !t->isToggled());
        Mic::get().stop();
        Mic::get().start();
    });
    toggler->toggle(Mod::get()->getSettingValue<bool>("mic-enabled"));
    toggler->setPosition({30.f, kH - 50.f});
    menu->addChild(toggler);
    auto l = CCLabelBMFont::create("Record microphone while playing", "bigFont.fnt");
    l->setScale(0.36f);
    l->setAnchorPoint({0.f, 0.5f});
    l->setPosition({48.f, kH - 50.f});
    m_mainLayer->addChild(l);

    m_list = CCMenu::create();
    m_list->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_list, 5);
    this->rebuild();
    return true;
}

void MicPopup::rebuild() {
    m_list->removeAllChildren();
    auto names = Mic::deviceNames();
    auto current = Mod::get()->getSettingValue<std::string>("mic-device");
    float y = kH - 82.f;
    if (names.empty()) {
        auto l = CCLabelBMFont::create("No microphones found", "chatFont.fnt");
        l->setScale(0.7f);
        l->setPosition({kW * 0.5f, y});
        m_list->addChild(l);
        return;
    }
    bool anySelected = false;
    for (auto const& n : names) if (n == current) anySelected = true;
    int i = 0;
    for (auto const& n : names) {
        if (i++ >= 7) break;
        bool selected = n == current || (!anySelected && i == 1);
        auto text = n.size() > 34 ? n.substr(0, 33) + "..." : n;
        auto spr = ButtonSprite::create(text.c_str(), 250, true, "bigFont.fnt",
                                        selected ? "GJ_button_01.png" : "GJ_button_04.png", 26.f, 0.45f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, n](CCMenuItemSpriteExtra*) {
            Mod::get()->setSettingValue<std::string>("mic-device", n);
            Mic::get().stop();
            Mic::get().start();
            this->rebuild();
        });
        btn->setPosition({kW * 0.5f, y});
        m_list->addChild(btn);
        y -= 25.f;
    }
}

} // namespace fw
