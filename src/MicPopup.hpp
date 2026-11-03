#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

namespace fw {

// Lists the computer's microphones so you can pick the one replays and clips use.
class MicPopup : public geode::Popup {
public:
    static MicPopup* create();

protected:
    bool init();
    void rebuild();
    void update(float dt) override;
    void onClose(cocos2d::CCObject* sender) override;

    cocos2d::CCMenu* m_list = nullptr;
    cocos2d::CCLayerColor* m_meter = nullptr;
    cocos2d::CCLabelBMFont* m_meterLabel = nullptr;
    float m_peakHold = 0.f;
};

} // namespace fw
