#include "UIInventory.h"
#include <algorithm>

// 原版 inventory.png 度量（1x 像素）
namespace {
    const float TEX_W = 256.0f, TEX_H = 256.0f; // 纹理尺寸
    const float GUI_W = 176.0f, GUI_H = 166.0f; // GUI 内容（左上区域）
    const int   PITCH = 18;                      // 格距
    const int   ICON  = 16;                      // 图标/内格
    const int   MAIN_X0 = 8,  MAIN_Y0 = 84;      // 主背包首格左上（GUI 像素，y 向下）
    const int   HOT_X0  = 8,  HOT_Y0  = 142;     // hotbar 首格左上
}

UIInventory::UIInventory(const std::string& id) : UIContainer(id) {
    anchor = glm::vec2(0.0f); // 位置即容器左下角（= GUI 左下角）
    initChildren();
}

UIInventory::~UIInventory() = default;

void UIInventory::initChildren() {
    // 背景：整张 inventory.png（GUI 内容在纹理左上 176x166）
    m_panel = std::make_shared<UIImage>(id + "_panel");
    m_panel->loadTextureByTextureName("inventory_gui");
    addComponent(m_panel);

    // 36 个物品格（自包含图标/数量/耐久）
    for (int i = 0; i < SLOT_COUNT; ++i) {
        auto slot = std::make_shared<UISlot>(id + "_slot_" + std::to_string(i));
        m_slots.push_back(slot);
        addComponent(slot);
    }

    // 光标携带栈（最上层，自带图标 + 数量角标）
    m_cursorSlot = std::make_shared<UISlot>(id + "_cursor");
    m_cursorSlot->visible = false;
    addComponent(m_cursorSlot);
}

void UIInventory::setScreenSize(int screenWidth, int screenHeight) {
    m_screenW = screenWidth;
    m_screenH = screenHeight;
    int s = 2;
    if (screenHeight >= 1080)     s = 4;
    else if (screenHeight >= 720) s = 3;
    else if (screenHeight >= 480) s = 2;
    m_guiScale = s;
    layout();
}

void UIInventory::layout() {
    const float s = static_cast<float>(m_guiScale);
    m_iconPx = ICON * s;

    // 容器（GUI 可见区）居中，size = 176x166 * s，position = 左下角（UI 空间 y 向上）
    const float panelW = GUI_W * s;
    const float panelH = GUI_H * s;
    position = glm::vec2((m_screenW - panelW) * 0.5f, (m_screenH - panelH) * 0.5f);
    size = glm::vec2(panelW, panelH);

    // 背景整张 256x256 纹理：让其左上角(纹理原点)对齐 GUI 左上角(局部 (0, panelH))。
    // 纹理 v=0(顶)映射到局部 y=panelH，v=1(底)映射到 y=panelH-256s，故 quad 左下角在 (0, panelH-256s)。
    m_panel->setSize(TEX_W * s, TEX_H * s);
    m_panel->setPosition(0.0f, panelH - TEX_H * s);

    // 把一个 GUI 像素坐标 (gx,gy，y 向下，图标左上) 的 16x16 格摆好
    auto placeSlot = [&](int index, int gx, int gy) {
        // 命中盒用 18x18 整格（含 1px 边）
        float cellX = (gx - 1) * s;
        float cellYtopUI = panelH - (gy - 1) * s;      // 该格上边在 UI 局部的 y
        float cellY = cellYtopUI - PITCH * s;          // 左下角 y
        m_slotRects[index] = glm::vec4(cellX, cellY, PITCH * s, PITCH * s);

        // 格子容器摆在图标 16x16 的左下角
        float ix = gx * s;
        float iy = (panelH - gy * s) - ICON * s;       // 图标左下角 y
        m_slots[index]->setPosition(ix, iy);
        m_slots[index]->configure(m_iconPx, s);
    };

    // hotbar：slot 0..8
    for (int c = 0; c < 9; ++c)
        placeSlot(c, HOT_X0 + c * PITCH, HOT_Y0);
    // 主背包：slot 9..35（上 3 行）
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 9; ++c)
            placeSlot(9 + r * 9 + c, MAIN_X0 + c * PITCH, MAIN_Y0 + r * PITCH);

    // 光标格：与普通格同尺寸
    m_cursorSlot->configure(m_iconPx, s);
}

void UIInventory::syncFrom(const std::vector<ItemStack>& inventory) {
    for (int i = 0; i < SLOT_COUNT && i < (int)inventory.size(); ++i) {
        const ItemStack& st = inventory[i];
        if (st.empty() || !st.def) { m_slots[i]->clear(); continue; }
        float durRatio = (st.def->hasDurability && st.def->maxDurability > 0)
            ? (float)st.durability / (float)st.def->maxDurability : -1.0f;
        m_slots[i]->setContent(st.def->iconName, st.count, durRatio, st.def->guiIconTexture);
    }
}

int UIInventory::slotAt(const glm::vec2& uiPoint) const {
    glm::vec2 local = uiPoint - position; // 转容器局部
    for (int i = 0; i < SLOT_COUNT; ++i) {
        const glm::vec4& r = m_slotRects[i];
        if (local.x >= r.x && local.x <= r.x + r.z &&
            local.y >= r.y && local.y <= r.y + r.w) {
            return i;
        }
    }
    return -1;
}

void UIInventory::setCursorStack(const ItemStack& st) {
    if (st.empty() || !st.def) {
        m_cursorSlot->clear();
        m_cursorSlot->visible = false;
        return;
    }
    float durRatio = (st.def->hasDurability && st.def->maxDurability > 0)
        ? (float)st.durability / (float)st.def->maxDurability : -1.0f;
    m_cursorSlot->setContent(st.def->iconName, st.count, durRatio, st.def->guiIconTexture);
    m_cursorSlot->visible = true;
}

void UIInventory::updateCursorPos(const glm::vec2& uiPoint) {
    glm::vec2 local = uiPoint - position;
    m_cursorSlot->setPosition(local.x - m_iconPx * 0.5f, local.y - m_iconPx * 0.5f);
}

bool UIInventory::panelContains(const glm::vec2& uiPoint) const {
    glm::vec2 local = uiPoint - position;
    return local.x >= 0.0f && local.x <= size.x &&
           local.y >= 0.0f && local.y <= size.y;
}
