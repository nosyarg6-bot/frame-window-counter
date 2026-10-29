#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

class Slider;

namespace fw {

// Overlay on/off, and (when on) its position, size and background opacity.
// Below that: frame windows shown in the level, and hiding test player hitboxes.
class OverlayPopup : public geode::Popup {
public:
    static OverlayPopup* create();

protected:
    bool init();
    void updateVisibility();
    void onX(cocos2d::CCObject* sender);
    void onY(cocos2d::CCObject* sender);
    void onSize(cocos2d::CCObject* sender);
    void onOpacity(cocos2d::CCObject* sender);
    void onLabelMax(cocos2d::CCObject* sender);
    void syncSliders();
    void applyHud();

    cocos2d::CCNode* m_overlayOptions = nullptr;
    Slider* m_x = nullptr;
    Slider* m_y = nullptr;
    Slider* m_sizeSlider = nullptr;
    Slider* m_opacity = nullptr;
    cocos2d::CCLabelBMFont* m_sizeValue = nullptr;
    cocos2d::CCLabelBMFont* m_opacityValue = nullptr;
    cocos2d::CCNode* m_labelOptions = nullptr;
    Slider* m_labelMax = nullptr;
    cocos2d::CCLabelBMFont* m_labelMaxValue = nullptr;
};

} // namespace fw
