#pragma once

#include <Geode/Geode.hpp>

namespace fw {

// The small overlay drawn in the top-left corner (position is draggable).
class FrameHud : public cocos2d::CCNode {
public:
    static FrameHud* create();
    static FrameHud* get();            // the overlay in the current PlayLayer, if any

    // saved position: offset of the overlay's top-left corner from the screen's top-left
    static cocos2d::CCPoint savedOffset();
    static void saveOffset(cocos2d::CCPoint offset);

    void applySettings();
    cocos2d::CCSize panelSize() const;

protected:
    bool init() override;
    void update(float dt) override;
    void rebuild();
    void onExit() override;

    cocos2d::CCLayerColor* m_bg = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    int m_seenRevision = -1;
    int m_rows = 8;
    bool m_showOffsets = true;
    bool m_showIneffective = false;
};

} // namespace fw
