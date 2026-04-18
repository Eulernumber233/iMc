#include "UIHotbar.h"
#include <algorithm>

UIHotbar::UIHotbar(const std::string& id, int slotCount)
    : UIContainer(id),
      m_slotCount(slotCount > 0 ? slotCount : 10)
{
    // 默认锚点：底部水平居中
    anchor = glm::vec2(0.5f, 0.0f);
    initChildren();
    layout();
}

UIHotbar::~UIHotbar() = default;

void UIHotbar::initChildren() {
    m_slotBackgrounds.clear();
    m_slotIcons.clear();

    // 渲染顺序（UIContainer 按添加顺序画）：
    //   1) 所有槽位背景
    //   2) 选中高亮（压在物品图标之下，避免遮住图标）
    //   3) 所有物品图标（最上层）
    for (int i = 0; i < m_slotCount; ++i) {
        auto bg = std::make_shared<UIImage>(id + "_bg_" + std::to_string(i));
        bg->loadTextureByTextureName(m_borderTexture);
        bg->zIndex = 1;
        m_slotBackgrounds.push_back(bg);
        addComponent(bg);
    }

    m_selection = std::make_shared<UIImage>(id + "_selection");
    m_selection->loadTextureByTextureName(m_selectionTexture);
    m_selection->zIndex = 2;
    addComponent(m_selection);

    for (int i = 0; i < m_slotCount; ++i) {
        auto icon = std::make_shared<UIImage>(id + "_icon_" + std::to_string(i));
        icon->zIndex = 3;
        icon->visible = false;
        m_slotIcons.push_back(icon);
        addComponent(icon);
    }
}

void UIHotbar::layout() {
    if (m_slotBackgrounds.empty()) return;

    const float s = static_cast<float>(m_guiScale);
    const float pitch   = SLOT_PITCH * s;
    const float bgW     = BG_W       * s;
    const float bgH     = BG_H       * s;
    const float iconPx  = ICON_SIZE  * s;
    const float selPx   = SEL_SIZE   * s;

    // 贴图资源的内部偏移（按当前缩放换算到屏幕像素）
    const float bgDx  = BG_SLOT_OFFSET_X  * s;  // 背景"可见槽位中心"相对贴图中心
    const float bgDy  = BG_SLOT_OFFSET_Y  * s;
    const float selDx = SEL_WINDOW_OFFSET_X * s; // 选中框"窗口中心"相对贴图中心
    const float selDy = SEL_WINDOW_OFFSET_Y * s;

    const float logicalW = pitch * m_slotCount;
    const float logicalH = pitch;
    size = glm::vec2(logicalW, logicalH);

    // 每个槽位的"真实视觉中心" = 逻辑中心 = 第 i 槽 ((i+0.5)*pitch, pitch/2)。
    // 背景贴图的几何中心需要反向偏移 (-bgDx, -bgDy)，才能让贴图的"可见槽位"落在逻辑中心。
    // 图标和选中框直接以逻辑中心为基准，因此它们自动对齐到可见槽位内。
    for (int i = 0; i < m_slotCount; ++i) {
        const float cx = (i + 0.5f) * pitch;
        const float cy = pitch * 0.5f;

        m_slotBackgrounds[i]->setSize(bgW, bgH);
        // bg 几何中心放在 (cx - bgDx, cy - bgDy)，其可见槽位中心即为 (cx, cy)
        m_slotBackgrounds[i]->setPosition(
            (cx - bgDx) - bgW * 0.5f,
            (cy - bgDy) - bgH * 0.5f);

        m_slotIcons[i]->setSize(iconPx, iconPx);
        m_slotIcons[i]->setPosition(cx - iconPx * 0.5f, cy - iconPx * 0.5f);
    }

    if (m_selection) {
        const int sel = std::clamp(m_selectedSlot, 0, m_slotCount - 1);
        const float cx = (sel + 0.5f) * pitch;
        const float cy = pitch * 0.5f;
        m_selection->setSize(selPx, selPx);
        // sel 几何中心放在 (cx - selDx, cy - selDy)，其窗口中心即为 (cx, cy)
        m_selection->setPosition(
            (cx - selDx) - selPx * 0.5f,
            (cy - selDy) - selPx * 0.5f);
    }
}

void UIHotbar::setSelectedSlot(int slot) {
    if (slot < 0 || slot >= m_slotCount) return;
    m_selectedSlot = slot;
    if (m_selection) {
        const float s = static_cast<float>(m_guiScale);
        const float pitch = SLOT_PITCH * s;
        const float selPx = SEL_SIZE   * s;
        const float selDx = SEL_WINDOW_OFFSET_X * s;
        const float selDy = SEL_WINDOW_OFFSET_Y * s;
        const float cx = (slot + 0.5f) * pitch;
        const float cy = pitch * 0.5f;
        m_selection->setPosition(
            (cx - selDx) - selPx * 0.5f,
            (cy - selDy) - selPx * 0.5f);
    }
}

void UIHotbar::setSlotItem(int slot, const std::string& textureName) {
    if (slot < 0 || slot >= m_slotCount) return;
    if (textureName.empty()) {
        clearSlotItem(slot);
        return;
    }
    m_slotIcons[slot]->loadTextureByTextureName(textureName);
    m_slotIcons[slot]->visible = true;
}

void UIHotbar::clearSlotItem(int slot) {
    if (slot < 0 || slot >= m_slotCount) return;
    m_slotIcons[slot]->visible = false;
}

void UIHotbar::scroll(float yoffset) {
    if (yoffset > 0) {
        int n = m_selectedSlot - 1;
        if (n < 0) n = m_slotCount - 1;
        setSelectedSlot(n);
    } else if (yoffset < 0) {
        int n = m_selectedSlot + 1;
        if (n >= m_slotCount) n = 0;
        setSelectedSlot(n);
    }
}

void UIHotbar::update(float deltaTime) { UIContainer::update(deltaTime); }
void UIHotbar::render(const glm::mat4& projection) { UIContainer::render(projection); }

void UIHotbar::setGuiScale(int scale) {
    m_guiScale = std::max(1, scale);
    layout();
}

void UIHotbar::setGuiScaleForScreen(int screenWidth, int screenHeight) {
    // 与原版 MC 类似：按屏幕高度选择整数 GUI 缩放，保证像素完美。
    // 参考：高度 < 480 用 1×，< 720 用 2×，< 1080 用 3×，否则 4×。
    int s = 1;
    if (screenHeight >= 1080)       s = 4;
    else if (screenHeight >= 720)   s = 3;
    else if (screenHeight >= 480)   s = 2;
    // 防止整条 hotbar 比屏幕还宽
    while (s > 1 && SLOT_PITCH * m_slotCount * s > screenWidth) --s;
    setGuiScale(s);
}
