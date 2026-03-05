#pragma once
#include "UIManager.h"

class UIHotbar : public UIContainer {
public:
    // 布局参数结构体，用于简化物品栏尺寸和位置配置
    struct LayoutParams {
        float baseSlotSize = 40.0f;          // 基础槽位尺寸（参考值）
        float borderScaleNormal = 1.0f;      // 未选中边框缩放比例（相对于基础槽位尺寸）
        float borderScaleSelected = 1.0f;    // 选中边框缩放比例（相对于基础槽位尺寸）
        float iconScale = 0.8f;              // 图标缩放比例（相对于边框内部尺寸）
        float iconOffsetX = 0.0f;            // 图标水平偏移量（用于覆盖左边空白）
        float iconOffsetY = 0.0f;            // 图标垂直偏移量
        float slotSpacing = 0.0f;            // 槽位间距（正数为间隔，负数为重叠）
        float overallScale = 1.0f;           // 整体缩放系数（用于适应屏幕分辨率）

        // 计算实际槽位大小（考虑整体缩放）
        float getScaledSlotSize() const { return baseSlotSize * overallScale; }
        // 计算实际间距
        float getScaledSpacing() const { return slotSpacing * overallScale; }
    };

    UIHotbar(const std::string& id, int slotCount = 10);
    ~UIHotbar();

    // 设置选中槽位
    void setSelectedSlot(int slot);
    int getSelectedSlot() const { return m_selectedSlot; }

    // 设置槽位物品图标
    void setSlotItem(int slot, const std::string& textureName);
    void clearSlotItem(int slot);

    // 处理滚轮滚动
    void scroll(float yoffset);

    // 更新和渲染
    void update(float deltaTime) override;
    void render(const glm::mat4& projection) override;

    // 设置位置和尺寸（重写以触发布局更新）
    void setPosition(float x, float y) ;
    void setSize(float width, float height);

    // 布局参数设置
    void setLayoutParams(const LayoutParams& params);
    const LayoutParams& getLayoutParams() const { return m_layoutParams; }

    // 根据当前容器尺寸更新布局参数（窗口大小变化时调用）
    void updateLayoutFromContainerSize(float containerWidth, float containerHeight);

private:
    int m_slotCount;
    int m_selectedSlot; // 0-indexed
    LayoutParams m_layoutParams;

    // 实际计算的尺寸（基于布局参数和容器尺寸）
    float m_actualSlotSize;     // 实际槽位大小（像素）
    float m_actualSpacing;      // 实际间距（像素）

    // 背景和边框纹理名称
    std::string m_backgroundTexture;
    std::string m_borderNormalTexture;
    std::string m_borderSelectedTexture;

    // 子组件指针
    std::vector<std::shared_ptr<UIImage>> m_slotBackgrounds; // 槽位背景（边框）
    std::vector<std::shared_ptr<UIImage>> m_slotIcons;       // 物品图标

    // 初始化槽位
    void initSlots();
    // 更新槽位布局（根据实际尺寸重新计算位置）
    void updateLayout();
    // 更新子组件尺寸（根据选中状态应用不同的边框缩放）
    void updateSlotSizes();
};