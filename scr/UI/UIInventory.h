#pragma once
#include "UIManager.h"
#include "UISlot.h"
#include "../item/ItemStack.h"
#include <array>
#include <vector>

// ── 背包界面（精简版：主背包 27 + hotbar 9 = 36 格）──────────────
// 背景用原版 inventory.png 纹理（256x256，GUI 内容在左上 176x166）。格子按原版
// 像素坐标定位：主背包 (8,84) 起、hotbar (8,142) 起、格距 18、图标 16x16。
// 只用主背包 27 + hotbar 9 这 36 格，纹理里的盔甲/合成格保留显示但不承载物品。
// 支持拖拽：按下拿起一格，释放落到目标格（移动 / 合并 / 交换由 Player::moveStack 决定）。
//
// 坐标：子组件用面板局部坐标（UIContainer 会加上容器原点平移）。slotAt 收全局 UI 坐标，
// 内部减去容器原点转局部再命中测试。
class UIInventory : public UIContainer {
public:
    static constexpr int SLOT_COUNT = 36;

    UIInventory(const std::string& id);
    ~UIInventory();

    // 按屏幕尺寸计算整数 GUI 缩放并居中布局
    void setScreenSize(int screenWidth, int screenHeight);

    // 用玩家背包刷新全部 36 格图标 / 数量 / 耐久
    void syncFrom(const std::vector<ItemStack>& inventory);

    // 命中测试：全局 UI 坐标 → 槽位下标，未命中返回 -1
    int slotAt(const glm::vec2& uiPoint) const;

    // ── 光标携带栈显示（鼠标本身作为一个物品格）────────────────────
    // 用光标栈内容刷新跟随鼠标的图标 + 数量角标；空栈则隐藏。
    void setCursorStack(const ItemStack& st);
    // 让光标图标中心跟随鼠标（全局 UI 坐标）
    void updateCursorPos(const glm::vec2& uiPoint);
    // 全局 UI 坐标是否落在背包面板矩形内（拖出面板释放 = 丢弃）
    bool panelContains(const glm::vec2& uiPoint) const;

private:
    int m_guiScale = 3;
    int m_screenW = 0, m_screenH = 0;

    std::shared_ptr<UIImage> m_panel;   // 原版 inventory.png 背景
    std::vector<std::shared_ptr<UISlot>> m_slots;   // 36 个物品格（自包含图标/数量/耐久）
    std::shared_ptr<UISlot> m_cursorSlot;           // 光标携带栈（图标+数量，最上层）

    std::array<glm::vec4, SLOT_COUNT> m_slotRects; // 局部坐标 (x,y,w,h)

    float m_iconPx  = 48.0f;

    void initChildren();
    void layout();
};
