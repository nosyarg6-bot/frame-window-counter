#include "OverlayPopup.hpp"
#include "Hud.hpp"
#include "ReportPopup.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>

using namespace geode::prelude;

namespace fw {

static constexpr float kW = 330.f;
static constexpr float kH = 300.f;
static constexpr float kMinScale = 0.4f;
static constexpr float kMaxScale = 2.0f;
static constexpr float kLabelMaxLo = 1.f;
static constexpr float kLabelMaxHi = 30.f;

static float thumbValue(CCObject* sender) {
    if (auto t = typeinfo_cast<SliderThumb*>(sender)) return std::clamp(t->getValue(), 0.f, 1.f);
    return 0.f;
}

// how far the overlay's top-left corner can move before it leaves the screen
static CCSize moveRange() {
    auto win = CCDirector::get()->getWinSize();
    CCSize panel{176.f, 120.f};
    if (auto hud = FrameHud::get()) panel = hud->panelSize();
    float scale = static_cast<float>(Mod::get()->getSettingValue<double>("hud-scale"));
    return {std::max(1.f, win.width - panel.width * scale), std::max(1.f, win.height - panel.height * scale)};
}

OverlayPopup* OverlayPopup::create() {
    auto ret = new OverlayPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool OverlayPopup::init() {
    if (!Popup::init(kW, kH)) return false;
    this->setTitle("Overlay Settings");

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize(m_size);
    m_mainLayer->addChild(menu, 5);

    auto addToggle = [&](char const* text, char const* key, float y, CCNode* parentLabel, CCMenu* parentMenu,
                         geode::Function<void()> after) {
        auto toggler = CCMenuItemExt::createTogglerWithStandardSprites(0.6f,
            [key = std::string(key), after = std::move(after)](CCMenuItemToggler* t) mutable {
                Mod::get()->setSettingValue<bool>(key, !t->isToggled());
                if (after) after();
            });
        toggler->toggle(Mod::get()->getSettingValue<bool>(key));
        toggler->setPosition({28.f, y});
        parentMenu->addChild(toggler);
        auto l = CCLabelBMFont::create(text, "bigFont.fnt");
        l->setScale(0.38f);
        l->setAnchorPoint({0.f, 0.5f});
        l->setPosition({46.f, y});
        l->limitLabelWidth(kW - 60.f, 0.38f, 0.2f);
        parentLabel->addChild(l);
    };

    float top = kH - 50.f;
    addToggle("Show Overlay", "enabled", top, m_mainLayer, menu, [this]() {
        this->applyHud();
        this->updateVisibility();
    });

    // overlay options, only shown while the overlay is on
    m_overlayOptions = CCNode::create();
    m_mainLayer->addChild(m_overlayOptions, 4);

    auto addSlider = [&](char const* text, float y, SEL_MenuHandler handler, CCLabelBMFont** valueLabel) {
        auto l = CCLabelBMFont::create(text, "bigFont.fnt");
        l->setScale(0.32f);
        l->setAnchorPoint({0.f, 0.5f});
        l->setPosition({46.f, y});
        m_overlayOptions->addChild(l);
        auto slider = Slider::create(this, handler, 0.6f);
        slider->setPosition({215.f, y});
        m_overlayOptions->addChild(slider);
        if (valueLabel) {
            *valueLabel = CCLabelBMFont::create("", "bigFont.fnt");
            (*valueLabel)->setScale(0.25f);
            (*valueLabel)->setPosition({215.f, y - 11.f});
            (*valueLabel)->setColor({200, 200, 200});
            m_overlayOptions->addChild(*valueLabel);
        }
        return slider;
    };
    m_x = addSlider("Position X", top - 30.f, menu_selector(OverlayPopup::onX), nullptr);
    m_y = addSlider("Position Y", top - 56.f, menu_selector(OverlayPopup::onY), nullptr);
    m_sizeSlider = addSlider("Size", top - 82.f, menu_selector(OverlayPopup::onSize), &m_sizeValue);
    m_opacity = addSlider("Background", top - 108.f, menu_selector(OverlayPopup::onOpacity), &m_opacityValue);

    auto optMenu = CCMenu::create();
    optMenu->setPosition({0.f, 0.f});
    m_overlayOptions->addChild(optMenu);
    auto drag = CCMenuItemExt::createSpriteExtra(
        ButtonSprite::create("Drag", "bigFont.fnt", "GJ_button_04.png", 0.6f),
        [this](CCMenuItemSpriteExtra*) {
            MoveHudLayer::open();
            this->onClose(nullptr);
        }
    );
    drag->setScale(0.5f);
    drag->m_baseScale = 0.5f;
    drag->setPosition({kW - 32.f, top - 43.f});
    optMenu->addChild(drag);

    // always available
    float y2 = top - 140.f;
    auto sep = CCLayerColor::create(ccColor4B{255, 255, 255, 40}, kW - 40.f, 1.f);
    sep->setPosition({20.f, y2 + 14.f});
    m_mainLayer->addChild(sep);
    addToggle("Show frame windows in the level", "show-level-labels", y2, m_mainLayer, menu, [this]() {
        this->updateVisibility();
    });
    // only shown while "show frame windows in the level" is on
    m_labelOptions = CCNode::create();
    m_mainLayer->addChild(m_labelOptions, 4);
    auto maxLabel = CCLabelBMFont::create("Only show up to", "bigFont.fnt");
    maxLabel->setScale(0.3f);
    maxLabel->setAnchorPoint({0.f, 0.5f});
    maxLabel->setPosition({46.f, y2 - 24.f});
    m_labelOptions->addChild(maxLabel);
    m_labelMax = Slider::create(this, menu_selector(OverlayPopup::onLabelMax), 0.6f);
    m_labelMax->setPosition({215.f, y2 - 24.f});
    m_labelOptions->addChild(m_labelMax);
    m_labelMaxValue = CCLabelBMFont::create("", "bigFont.fnt");
    m_labelMaxValue->setScale(0.25f);
    m_labelMaxValue->setColor({200, 200, 200});
    m_labelMaxValue->setPosition({215.f, y2 - 35.f});
    m_labelOptions->addChild(m_labelMaxValue);
    y2 -= 26.f;

    addToggle("Show test player hitboxes", "show-test-players", y2 - 26.f, m_mainLayer, menu, nullptr);
    auto note = CCLabelBMFont::create("(the hidden test players that measure each click - leave off)", "chatFont.fnt");
    note->setScale(0.45f);
    note->setColor({170, 170, 170});
    note->setPosition({kW * 0.5f, y2 - 44.f});
    m_mainLayer->addChild(note);

    this->syncSliders();
    this->updateVisibility();
    return true;
}

void OverlayPopup::updateVisibility() {
    m_overlayOptions->setVisible(Mod::get()->getSettingValue<bool>("enabled"));
    if (m_labelOptions) m_labelOptions->setVisible(Mod::get()->getSettingValue<bool>("show-level-labels"));
}

void OverlayPopup::onLabelMax(CCObject* sender) {
    float v = std::round(kLabelMaxLo + thumbValue(sender) * (kLabelMaxHi - kLabelMaxLo));
    Mod::get()->setSettingValue<double>("level-labels-max", v);
    m_labelMaxValue->setString(fmt::format("{:.0f} frames", v).c_str());
}

void OverlayPopup::syncSliders() {
    auto range = moveRange();
    auto off = FrameHud::savedOffset();
    m_x->setValue(std::clamp(off.x / range.width, 0.f, 1.f));
    m_y->setValue(std::clamp(off.y / range.height, 0.f, 1.f));
    float scale = static_cast<float>(Mod::get()->getSettingValue<double>("hud-scale"));
    m_sizeSlider->setValue((scale - kMinScale) / (kMaxScale - kMinScale));
    int64_t opacity = Mod::get()->getSettingValue<int64_t>("hud-opacity");
    m_opacity->setValue(static_cast<float>(opacity) / 255.f);
    m_sizeValue->setString(fmt::format("{:.0f}%", scale * 100.f).c_str());
    m_opacityValue->setString(fmt::format("{:.0f}%", opacity / 2.55f).c_str());
    if (m_labelMax) {
        double lm = Mod::get()->getSettingValue<double>("level-labels-max");
        m_labelMax->setValue(static_cast<float>((lm - kLabelMaxLo) / (kLabelMaxHi - kLabelMaxLo)));
        m_labelMaxValue->setString(fmt::format("{:.0f} frames", lm).c_str());
    }
}

void OverlayPopup::applyHud() {
    if (auto hud = FrameHud::get()) hud->applySettings();
}

void OverlayPopup::onX(CCObject* sender) {
    auto off = FrameHud::savedOffset();
    off.x = thumbValue(sender) * moveRange().width;
    FrameHud::saveOffset(off);
    this->applyHud();
}

void OverlayPopup::onY(CCObject* sender) {
    auto off = FrameHud::savedOffset();
    off.y = thumbValue(sender) * moveRange().height;
    FrameHud::saveOffset(off);
    this->applyHud();
}

void OverlayPopup::onSize(CCObject* sender) {
    float scale = kMinScale + thumbValue(sender) * (kMaxScale - kMinScale);
    scale = std::round(scale * 20.f) / 20.f;
    Mod::get()->setSettingValue<double>("hud-scale", scale);
    m_sizeValue->setString(fmt::format("{:.0f}%", scale * 100.f).c_str());
    this->applyHud();
}

void OverlayPopup::onOpacity(CCObject* sender) {
    int64_t opacity = static_cast<int64_t>(std::round(thumbValue(sender) * 255.f));
    Mod::get()->setSettingValue<int64_t>("hud-opacity", opacity);
    m_opacityValue->setString(fmt::format("{:.0f}%", opacity / 2.55f).c_str());
    this->applyHud();
}

} // namespace fw
