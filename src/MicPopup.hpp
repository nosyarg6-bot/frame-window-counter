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

    cocos2d::CCMenu* m_list = nullptr;
};

} // namespace fw
