#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include "Tracker.hpp"

namespace fw {

// Full report: every input of the previous runs with its frame window.
class ReportPopup : public geode::Popup {
public:
    static ReportPopup* create();

protected:
    bool init();
    void showRun(int index);
    void buildList(RunRecord const& run);
    void buildSummary(RunRecord const& run);

    std::vector<RunRecord> m_runs;
    std::vector<std::string> m_runNames;
    int m_index = 0;

    cocos2d::CCLabelBMFont* m_runLabel = nullptr;
    cocos2d::CCLabelBMFont* m_infoLabel = nullptr;
    cocos2d::CCNode* m_summary = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    CCMenuItemSpriteExtra* m_prevBtn = nullptr;
    CCMenuItemSpriteExtra* m_nextBtn = nullptr;
    CCMenuItemSpriteExtra* m_replayBtn = nullptr;
    cocos2d::CCLabelBMFont* m_replayNote = nullptr;
};

// Full-screen overlay that lets you drag the overlay to a new position.
class MoveHudLayer : public cocos2d::CCLayer {
public:
    static MoveHudLayer* create();
    static void open();

protected:
    bool init() override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void keyBackClicked() override;
    void close();
    void placeBox();

    cocos2d::CCLayerColor* m_box = nullptr;
    cocos2d::CCNode* m_doneBtn = nullptr;
    cocos2d::CCNode* m_resetBtn = nullptr;
    cocos2d::CCPoint m_offset;        // top-left offset from the screen's top-left
    cocos2d::CCPoint m_dragStart;
    cocos2d::CCPoint m_offsetStart;
    bool m_dragging = false;
};

} // namespace fw
