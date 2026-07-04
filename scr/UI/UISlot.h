#pragma once
#include "UIManager.h"
#include "UINumber.h"

// ── 单个物品格（自包含容器）──────────────────────────────────────
// 一个格子内部管理自己的：图标、数量角标、耐久条。父容器（UIHotbar / UIInventory）
// 只负责把格子摆到正确位置（setPosition）并配置缩放（configure），内容用 setContent 刷新。
// 子组件坐标相对格子原点（左下角）；UIContainer 会加上格子自身的位置平移。
class UISlot : public UIContainer {
public:
    UISlot(const std::string& id);

    // 配置像素度量：iconPx = 图标边长（屏幕像素），guiScale 用于角标/耐久条缩放
    void configure(float iconPx, float guiScale);

    // 刷新内容：iconName 空 / count<=0 视为空格；durRatio<0 不显示耐久条。
    // iconTexOverride != 0 时优先用该 GL 纹理（方块物品的等距立方体图标）。
    void setContent(const std::string& iconName, int count, float durRatio,
                    GLuint iconTexOverride = 0);
    void clear();

    // 拖拽用：当前图标 GL 纹理（空格为 0）
    GLuint iconTextureID() const { return m_icon ? m_icon->textureID : 0; }
    bool   hasItem() const { return m_icon && m_icon->visible && m_icon->textureID != 0; }
    float  iconPx() const { return m_iconPx; }

private:
    std::shared_ptr<UIImage>  m_icon;
    std::shared_ptr<UIRect>   m_durBg, m_durFill;
    std::shared_ptr<UINumber> m_count;
    float m_iconPx = 16.0f;
    float m_scale  = 1.0f;

    void relayout();
};
