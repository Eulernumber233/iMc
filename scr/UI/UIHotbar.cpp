#include "UIHotbar.h"
#include <iostream>
#include <algorithm>

UIHotbar::UIHotbar(const std::string& id, int slotCount)
    : UIContainer(id), m_slotCount(slotCount > 0 ? slotCount : 10), m_selectedSlot(0),
    m_actualSlotSize(0.0f), m_actualSpacing(0.0f)
{
    // 确保槽位数量为正数
    if (m_slotCount <= 0) {
        m_slotCount = 10;
    }

    // 设置默认锚点：水平居中，底部对齐
    anchor = glm::vec2(0.5f, 0.0f);

    // 设置默认纹理
    m_backgroundTexture = "";  // 不使用背景纹理
    m_borderNormalTexture = "hotbar_offhand_right";  // 使用右侧副手纹理作为普通边框
    m_borderSelectedTexture = "hotbar_selection";

    // 设置默认布局参数（可根据实际贴图调整）
    m_layoutParams.baseSlotSize = 40.0f;
    m_layoutParams.borderScaleNormal = 1.0f;
    m_layoutParams.borderScaleSelected = 1.0f;
    m_layoutParams.iconScale = 0.8f;
    m_layoutParams.iconOffsetX = 0.0f;
    m_layoutParams.iconOffsetY = 0.0f;
    m_layoutParams.slotSpacing = 0.0f;  // 默认无间距
    m_layoutParams.overallScale = 1.0f;

    // 初始化槽位
    initSlots();

    // 设置默认尺寸（基于布局参数）
    float totalWidth = m_slotCount * m_layoutParams.getScaledSlotSize()
                     + (m_slotCount - 1) * m_layoutParams.getScaledSpacing();
    float slotHeight = m_layoutParams.getScaledSlotSize();
    size = glm::vec2(totalWidth, slotHeight); // 直接设置尺寸，不调用updateLayout
}

UIHotbar::~UIHotbar() {
    // 子组件通过UIContainer自动管理
}

void UIHotbar::initSlots() {
    m_slotBackgrounds.clear();
    m_slotIcons.clear();

    for (int i = 0; i < m_slotCount; ++i) {
        // 创建背景（边框）
        auto bg = std::make_shared<UIImage>(id + "_slot_bg_" + std::to_string(i));
        bg->loadTextureByTextureName(m_borderNormalTexture);
        bg->zIndex = 1;
        m_slotBackgrounds.push_back(bg);
        addComponent(bg);

        // 创建物品图标
        auto icon = std::make_shared<UIImage>(id + "_slot_icon_" + std::to_string(i));
        icon->zIndex = 2;
        icon->visible = false; // 默认隐藏
        m_slotIcons.push_back(icon);
        addComponent(icon);
    }

    // 更新选中状态
    setSelectedSlot(m_selectedSlot);
    // 初始尺寸计算
    updateSlotSizes();
    updateLayout();
}

void UIHotbar::updateSlotSizes() {
    // 计算实际槽位大小和间距（基于布局参数）
    m_actualSlotSize = m_layoutParams.getScaledSlotSize();
    m_actualSpacing = m_layoutParams.getScaledSpacing();

    for (int i = 0; i < m_slotCount; ++i) {
        if (i >= m_slotBackgrounds.size() || i >= m_slotIcons.size()) break;

        // 根据选中状态确定边框缩放比例
        float borderScale = (i == m_selectedSlot) ?
            m_layoutParams.borderScaleSelected : m_layoutParams.borderScaleNormal;
        float borderSize = m_actualSlotSize * borderScale;

        // 设置边框大小
        m_slotBackgrounds[i]->setSize(borderSize*1.0, borderSize*0.8);

        // 计算图标大小（相对于边框内部尺寸）
        float iconSize = borderSize * m_layoutParams.iconScale;
        // 应用偏移量
        float iconOffsetX = m_layoutParams.iconOffsetX * m_layoutParams.overallScale;
        float iconOffsetY = m_layoutParams.iconOffsetY * m_layoutParams.overallScale;

        // 设置图标大小（图标位置在updateLayout中计算）
        m_slotIcons[i]->setSize(iconSize, iconSize);
    }
}

void UIHotbar::updateLayout() {
    // 检查是否已经初始化槽位
    if (m_slotBackgrounds.empty() || m_slotIcons.empty()) {
        return; // 槽位尚未初始化
    }

    // 确保槽位数量与向量大小匹配
    if (m_slotCount <= 0 || m_slotCount != m_slotBackgrounds.size()) {
        return;
    }

    // 计算局部坐标（相对于容器原点）
    // 容器原点在左下角（因为anchor为(0.5,0)）
    // 计算总占用宽度
    float totalOccupiedWidth = m_slotCount * m_actualSlotSize
                             + (m_slotCount - 1) * m_actualSpacing;
    // 槽位在容器内水平居中
    float startX = (size.x - totalOccupiedWidth) * 0.5f;

    // 槽位在容器内垂直居中（考虑实际槽位大小）
    float startY = (size.y - m_actualSlotSize) * 0.5f;

    for (int i = 0; i < m_slotCount; ++i) {
        // 计算槽位位置（考虑间距）
        float x = startX + i * (m_actualSlotSize + m_actualSpacing);
        float y = startY;

        // 根据选中状态确定边框缩放比例
        float borderScale = (i == m_selectedSlot) ?
            m_layoutParams.borderScaleSelected : m_layoutParams.borderScaleNormal;
        float borderSize = m_actualSlotSize * borderScale;

        // 边框在槽位内居中（因为边框大小可能不同于实际槽位大小）
        float borderOffsetX = (m_actualSlotSize - borderSize) * 0.5f;
        float borderOffsetY = (m_actualSlotSize - borderSize) * 0.5f;

        m_slotBackgrounds[i]->setPosition(x + borderOffsetX, y + borderOffsetY);

        // 图标位置（相对于槽位，考虑偏移量）
        // 图标大小已经在updateSlotSizes中设置
        float iconSize = borderSize * m_layoutParams.iconScale;
        float iconOffsetX = m_layoutParams.iconOffsetX * m_layoutParams.overallScale;
        float iconOffsetY = m_layoutParams.iconOffsetY * m_layoutParams.overallScale;

        // 图标在边框内居中，再加上用户定义的偏移量
        float iconPosX = x + borderOffsetX + (borderSize - iconSize) * 0.5f + iconOffsetX;
        float iconPosY = y + borderOffsetY + (borderSize - iconSize) * 0.5f + iconOffsetY;

        m_slotIcons[i]->setPosition(iconPosX, iconPosY);
    }
}

void UIHotbar::setSelectedSlot(int slot) {
    if (slot < 0 || slot >= m_slotCount) return;

    // 检查向量是否已初始化
    if (m_slotBackgrounds.empty() || m_slotIcons.empty()) {
        m_selectedSlot = slot; // 只更新选中槽位，不设置纹理
        return;
    }

    // 恢复之前选中槽位的边框
    if (m_selectedSlot >= 0 && m_selectedSlot < m_slotCount &&
        m_selectedSlot < m_slotBackgrounds.size()) {
        m_slotBackgrounds[m_selectedSlot]->loadTextureByTextureName(m_borderNormalTexture);
    }

    m_selectedSlot = slot;

    // 设置新选中槽位的边框
    if (m_selectedSlot >= 0 && m_selectedSlot < m_slotCount &&
        m_selectedSlot < m_slotBackgrounds.size()) {
        m_slotBackgrounds[m_selectedSlot]->loadTextureByTextureName(m_borderSelectedTexture);
    }

    // 更新尺寸（因为选中状态可能改变边框缩放比例）
    updateSlotSizes();
    updateLayout();
}

void UIHotbar::setSlotItem(int slot, const std::string& textureName) {
    if (slot < 0 || slot >= m_slotCount) return;

    if (textureName.empty()) {
        clearSlotItem(slot);
    }
    else {
        m_slotIcons[slot]->loadTextureByTextureName(textureName);
        m_slotIcons[slot]->visible = true;
    }
}

void UIHotbar::clearSlotItem(int slot) {
    if (slot < 0 || slot >= m_slotCount) return;

    m_slotIcons[slot]->visible = false;
}

void UIHotbar::scroll(float yoffset) {
    if (yoffset > 0) {
        // 向上滚动，选择上一个槽位
        int newSlot = m_selectedSlot - 1;
        if (newSlot < 0) newSlot = m_slotCount - 1;
        setSelectedSlot(newSlot);
    }
    else if (yoffset < 0) {
        // 向下滚动，选择下一个槽位
        int newSlot = m_selectedSlot + 1;
        if (newSlot >= m_slotCount) newSlot = 0;
        setSelectedSlot(newSlot);
    }
}

void UIHotbar::update(float deltaTime) {
    UIContainer::update(deltaTime);
}

void UIHotbar::render(const glm::mat4& projection) {
    UIContainer::render(projection);
}

void UIHotbar::setPosition(float x, float y) {
    UIComponent::setPosition(x, y);
    // 如果槽位已经初始化，更新布局
    if (!m_slotBackgrounds.empty() && !m_slotIcons.empty()) {
        updateLayout();
    }
}

void UIHotbar::setSize(float width, float height) {
    // 调用基类设置尺寸
    UIComponent::setSize(width, height);
    // 根据容器尺寸更新布局
    updateLayoutFromContainerSize(width, height);
}

void UIHotbar::setLayoutParams(const LayoutParams& params) {
    m_layoutParams = params;
    // 如果槽位已经初始化，更新尺寸和布局
    if (!m_slotBackgrounds.empty() && !m_slotIcons.empty()) {
        updateSlotSizes();
        updateLayout();
    }
}

void UIHotbar::updateLayoutFromContainerSize(float containerWidth, float containerHeight) {
    if (m_slotCount <= 0 || containerWidth <= 0 || containerHeight <= 0) {
        return;
    }

    // 计算每个槽位可用的最大宽度
    float maxSlotWidth = containerWidth / m_slotCount;
    // 最大高度不超过容器高度
    float maxSlotHeight = containerHeight;

    // 基础槽位尺寸（参考值）
    float baseSize = m_layoutParams.baseSlotSize;
    
    // 计算宽度和高度的缩放系数
    float scaleWidth = maxSlotWidth / baseSize;
    float scaleHeight = maxSlotHeight / baseSize;
    
    // 取较小的缩放系数，确保槽位不超出容器
    float scale = std::min(scaleWidth, scaleHeight);
    
    // 限制缩放系数在合理范围内
    const float MIN_SCALE = 0.1f;
    const float MAX_SCALE = 5.0f;
    if (scale < MIN_SCALE) scale = MIN_SCALE;
    if (scale > MAX_SCALE) scale = MAX_SCALE;
    
    // 更新整体缩放系数
    m_layoutParams.overallScale = scale;
    
    // 计算实际槽位大小和间距
    m_actualSlotSize = m_layoutParams.getScaledSlotSize();
    m_actualSpacing = m_layoutParams.getScaledSpacing();
    
    // 计算总占用宽度
    float totalOccupiedWidth = m_slotCount * m_actualSlotSize 
                             + (m_slotCount - 1) * m_actualSpacing;
    float totalOccupiedHeight = m_actualSlotSize;
    
    // 更新容器尺寸为实际占用的尺寸（保持传入的尺寸，但可以调整）
    // 这里我们设置容器尺寸为实际占用尺寸，以便锚点计算正确
    UIComponent::setSize(totalOccupiedWidth, totalOccupiedHeight);
    
    // 更新子组件尺寸和布局
    if (!m_slotBackgrounds.empty() && !m_slotIcons.empty()) {
        updateSlotSizes();
        updateLayout();
    }
}

