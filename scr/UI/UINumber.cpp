#include "UINumber.h"
#include "../stb_image.h"
#include <string>
#include <vector>
#include <cstdint>

// 内置 3x5 像素数字字体（ascii.png 加载失败时的兜底）。每行 3 bit（bit2 最左）。
static const uint8_t FONT35[10][5] = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b110,0b010,0b010,0b111}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b001,0b010,0b010}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
};

namespace {
// ── ascii.png 位图字体图集（128x128，16x16 格，每格 8x8，按 ASCII 码定位）──
struct FontAtlas {
    std::vector<unsigned char> px;   // RGBA
    int w = 0, h = 0, cw = 0, ch = 0;
    int width[128] = {0};            // 各字符字形有效宽度（非空列数）
    bool ok = false, tried = false;
};

FontAtlas& fontAtlas() {
    static FontAtlas a;
    if (a.tried) return a;
    a.tried = true;
    int w, h, n;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* d = stbi_load("assert/minecraft/textures/font/ascii.png", &w, &h, &n, 4);
    if (!d) return a;
    a.px.assign(d, d + (size_t)w * h * 4);
    stbi_image_free(d);
    a.w = w; a.h = h; a.cw = w / 16; a.ch = h / 16;
    // 预计算 0-127 字形宽度（最右非空列 + 1）
    for (int c = 0; c < 128; ++c) {
        int col = c % 16, row = c / 16;
        int sx = col * a.cw, sy = row * a.ch;
        int maxCol = -1;
        for (int gx = 0; gx < a.cw; ++gx)
            for (int gy = 0; gy < a.ch; ++gy)
                if (a.px[((sy + gy) * a.w + (sx + gx)) * 4 + 3] > 127) { maxCol = gx; break; }
        a.width[c] = (maxCol >= 0) ? (maxCol + 1) : (a.cw / 2); // 空格用半格
    }
    a.ok = true;
    return a;
}

inline int glyphAlpha(const FontAtlas& a, int c, int gx, int gy) {
    int col = c % 16, row = c / 16;
    int x = col * a.cw + gx, y = row * a.ch + gy;
    return a.px[((size_t)y * a.w + x) * 4 + 3];
}
} // namespace

UINumber::UINumber(const std::string& id) : UIComponent(id) {
    UIRect::initGeometry();
    VAO = UIRect::s_VAO;
    visible = false;
}

UINumber::~UINumber() {
    if (m_texture) glDeleteTextures(1, &m_texture);
}

void UINumber::setPixelScale(float s) {
    m_pixelScale = s;
    if (m_texPixels.x > 0.0f)
        size = m_texPixels * m_pixelScale;
}

void UINumber::setValue(int v) {
    if (v == m_value && m_texture != 0) { visible = (v > 1); return; }
    m_value = v;
    if (v > 1) { rebuild(); visible = true; }
    else       { visible = false; }
}

void UINumber::rebuild() {
    std::string s = std::to_string(m_value);
    const FontAtlas& a = fontAtlas();

    std::vector<uint8_t> buf;
    int texW = 0, texH = 0;

    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        if (x < 0 || y < 0 || x >= texW || y >= texH) return;
        size_t idx = ((size_t)y * texW + x) * 4;
        buf[idx] = r; buf[idx + 1] = g; buf[idx + 2] = b; buf[idx + 3] = 255;
    };

    if (a.ok) {
        // 用 ascii.png 真字形烘焙（白字 + 右下黑投影）
        const int spacing = 1;
        int total = 0;
        std::vector<int> adv;
        adv.reserve(s.size());
        for (char ch : s) { int w = a.width[(int)ch]; adv.push_back(w); total += w + spacing; }
        if (!adv.empty()) total -= spacing;
        texW = total + 1;          // +1 给右下投影
        texH = a.ch + 1;
        buf.assign((size_t)texW * texH * 4, 0);

        int penX = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            int c = (int)s[i];
            for (int gy = 0; gy < a.ch; ++gy)
                for (int gx = 0; gx < a.cw; ++gx)
                    if (glyphAlpha(a, c, gx, gy) > 127) {
                        put(penX + gx + 1, gy + 1, 0, 0, 0);       // 投影
                        put(penX + gx,     gy,     255, 255, 255); // 白字
                    }
            penX += adv[i] + spacing;
        }
    } else {
        // 兜底：3x5 手绘字体
        const int gw = 3, gh = 5, spacing = 1;
        int contentW = (int)s.size() * gw + ((int)s.size() - 1) * spacing;
        texW = contentW + 1;
        texH = gh + 1;
        buf.assign((size_t)texW * texH * 4, 0);
        for (size_t i = 0; i < s.size(); ++i) {
            int d = s[i] - '0';
            if (d < 0 || d > 9) continue;
            int baseX = (int)i * (gw + spacing);
            for (int fy = 0; fy < gh; ++fy) {
                uint8_t row = FONT35[d][fy];
                for (int fx = 0; fx < gw; ++fx)
                    if (row & (1 << (gw - 1 - fx))) {
                        put(baseX + fx + 1, fy + 1, 0, 0, 0);
                        put(baseX + fx,     fy,     255, 255, 255);
                    }
            }
        }
    }

    if (m_texture) glDeleteTextures(1, &m_texture);
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_texPixels = glm::vec2((float)texW, (float)texH);
    size = m_texPixels * m_pixelScale;
}

void UINumber::render(const glm::mat4& projection, Shader& shader) {
    if (!visible || m_texture == 0) return;

    glm::mat4 model = calculateTransform();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    shader.use();
    shader.setMat4("uProjection", projection);
    shader.setMat4("uTransform", model);
    shader.setVec4("uColor", glm::vec4(1.0f));
    shader.setFloat("uAlpha", alpha);
    shader.setInt("uHasTexture", 1);
    shader.setInt("uIsText", 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}
