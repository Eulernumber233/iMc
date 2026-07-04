#include "UISlot.h"
#include <algorithm>

UISlot::UISlot(const std::string& id) : UIContainer(id) {
    anchor = glm::vec2(0.0f); // 位置即格子左下角

    // 子组件坐标相对格子原点 (0,0)。渲染顺序：图标 → 耐久条 → 数量角标（最上）。
    m_icon = std::make_shared<UIImage>(id + "_icon");
    m_icon->visible = false;
    addComponent(m_icon);

    m_durBg = std::make_shared<UIRect>(id + "_durbg");
    m_durBg->color = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    m_durBg->visible = false;
    addComponent(m_durBg);

    m_durFill = std::make_shared<UIRect>(id + "_durfill");
    m_durFill->color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    m_durFill->visible = false;
    addComponent(m_durFill);

    m_count = std::make_shared<UINumber>(id + "_count");
    addComponent(m_count);
}

void UISlot::configure(float iconPx, float guiScale) {
    m_iconPx = iconPx;
    m_scale = guiScale;
    size = glm::vec2(iconPx, iconPx);
    relayout();
}

void UISlot::relayout() {
    // 图标铺满格子；耐久条贴底；数量角标位置在 setContent 按宽度对齐
    m_icon->setPosition(0.0f, 0.0f);
    m_icon->setSize(m_iconPx, m_iconPx);

    float durH = 2.0f * m_scale;
    m_durBg->setPosition(0.0f, 0.0f);
    m_durBg->setSize(m_iconPx, durH);
    m_durFill->setPosition(0.0f, 0.0f);
    m_durFill->setSize(m_iconPx, durH);

    m_count->setPixelScale(m_scale);
}

void UISlot::clear() {
    m_icon->visible = false;
    m_count->setValue(0);
    m_durBg->visible = false;
    m_durFill->visible = false;
}

void UISlot::setContent(const std::string& iconName, int count, float durRatio,
                        GLuint iconTexOverride) {
    if ((iconName.empty() && iconTexOverride == 0) || count <= 0) { clear(); return; }

    if (iconTexOverride != 0)
        m_icon->loadTextureByGLid(iconTexOverride);
    else
        m_icon->loadTextureByTextureName(iconName);
    m_icon->visible = (m_icon->textureID != 0);

    // 数量角标（右下角）
    m_count->setPixelScale(m_scale);
    m_count->setValue(count);
    if (m_count->visible)
        m_count->setPosition(m_iconPx - m_count->size.x, 0.0f);

    // 耐久条
    if (durRatio >= 0.0f) {
        float r = std::clamp(durRatio, 0.0f, 1.0f);
        m_durBg->visible = true;
        m_durFill->visible = true;
        m_durFill->setSize(m_iconPx * r, m_durFill->size.y);
        m_durFill->color = glm::vec4(1.0f - r, r, 0.0f, 1.0f);
    } else {
        m_durBg->visible = false;
        m_durFill->visible = false;
    }
}
