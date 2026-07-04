#pragma once
#include "UIManager.h"

// ── 数字显示组件 ────────────────────────────────────────────────
// 用内置 3x5 像素字体把一个整数烘焙成一张小 RGBA 纹理（白字 + 黑色投影），
// 当作普通带纹理 quad 画（复用 ui 着色器，uHasTexture=1）。仅在 value 改变时
// 重建纹理。用于物品栈数量角标（原版 MC 风格）。
//
// 现有 UIText 是占位桩（loadFont 空实现、updateTexture 只画竖条），无法显示真实
// 数字，故单独实现本组件。
class UINumber : public UIComponent {
public:
    UINumber(const std::string& id);
    ~UINumber();

    // 设置要显示的数值；<=1 时隐藏（原版数量为 1 不显示角标）
    void setValue(int v);
    int  getValue() const { return m_value; }

    // 每字体像素在屏幕上的缩放（决定角标大小）
    void setPixelScale(float s);

    void render(const glm::mat4& projection, Shader& shader) override;

private:
    int    m_value = 0;
    float  m_pixelScale = 2.0f;
    GLuint m_texture = 0;
    glm::vec2 m_texPixels = glm::vec2(0.0f); // 纹理像素尺寸（含投影 +1）

    GLuint VAO = 0;
    void rebuild();
};
