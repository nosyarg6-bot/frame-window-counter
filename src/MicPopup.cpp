#include "MicPopup.hpp"
#include "Mic.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/PlayLayer.hpp>

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

    // live input level, so you can check the right mic is picked
    auto meterBg = CCLayerColor::create(ccColor4B{0, 0, 0, 120}, kW - 60.f, 10.f);
    meterBg->setPosition({30.f, 18.f});
    m_mainLayer->addChild(meterBg);
    m_meter = CCLayerColor::create(ccColor4B{90, 255, 120, 255}, 0.f, 10.f);
    m_meter->setPosition({30.f, 18.f});
    m_mainLayer->addChild(m_meter);
    m_meterLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_meterLabel->setScale(0.55f);
    m_meterLabel->setPosition({kW * 0.5f, 38.f});
    m_mainLayer->addChild(m_meterLabel);
    this->scheduleUpdate();

    m_list = CCMenu::create();
    m_list->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_list, 5);
    this->rebuild();
    return true;
}

void MicPopup::update(float dt) {
    auto& mic = Mic::get();
    if (Mod::get()->getSettingValue<bool>("mic-enabled") && !mic.running()) mic.start();
    mic.update();
    float lvl = mic.running() ? mic.level() : 0.f;
    m_peakHold = std::max(lvl, m_peakHold - dt * 0.6f);
    m_meter->setContentSize({(kW - 60.f) * std::clamp(m_peakHold * 2.f, 0.f, 1.f), 10.f});
    if (!Mod::get()->getSettingValue<bool>("mic-enabled")) m_meterLabel->setString("Recording is off");
    else if (!mic.running()) m_meterLabel->setString("Couldn't open a microphone");
    else m_meterLabel->setString(fmt::format("Listening: {}  -  click to test", mic.deviceName()).c_str());
}

void MicPopup::onClose(CCObject* sender) {
    // outside a level there's nothing to record
    if (!PlayLayer::get()) Mic::get().stop();
    Popup::onClose(sender);
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
