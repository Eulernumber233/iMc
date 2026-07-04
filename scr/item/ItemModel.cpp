#include "ItemModel.h"
#include "../stb_image.h"
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <iostream>

namespace {
struct ItemVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
};

// 往顶点/索引缓冲追加一个四边形（两三角形）。不依赖 winding（渲染时关背面剔除）。
void addQuad(std::vector<ItemVertex>& v, std::vector<unsigned>& idx,
             const glm::vec3& p0, const glm::vec3& p1,
             const glm::vec3& p2, const glm::vec3& p3,
             const glm::vec3& n,
             const glm::vec2& uv0, const glm::vec2& uv1,
             const glm::vec2& uv2, const glm::vec2& uv3) {
    unsigned base = (unsigned)v.size();
    v.push_back({ p0, n, uv0 });
    v.push_back({ p1, n, uv1 });
    v.push_back({ p2, n, uv2 });
    v.push_back({ p3, n, uv3 });
    idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
    idx.push_back(base + 0); idx.push_back(base + 2); idx.push_back(base + 3);
}
} // namespace

ItemModel::~ItemModel() {
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

std::unique_ptr<ItemModel> ItemModel::buildFromIcon(const std::string& iconPath) {
    int w = 0, h = 0, n = 0;
    // 强制 RGBA；不翻转（与 TextureMgr 一致，纹理 row0 = 图像顶行）
    stbi_set_flip_vertically_on_load(false);
    unsigned char* pixels = stbi_load(iconPath.c_str(), &w, &h, &n, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        std::cerr << "[ItemModel] failed to load icon: " << iconPath << std::endl;
        return nullptr;
    }

    auto alphaAt = [&](int px, int py) -> int {
        if (px < 0 || py < 0 || px >= w || py >= h) return 0;
        return pixels[(py * w + px) * 4 + 3];
    };
    const int OPAQUE = 128;

    const float pixelSize = 1.0f / (float)std::max(w, h);
    const float halfW = w * pixelSize * 0.5f;
    const float halfH = h * pixelSize * 0.5f;
    const float halfD = pixelSize * 0.5f; // 厚度 = 1 像素

    std::vector<ItemVertex> verts;
    std::vector<unsigned> idx;
    verts.reserve(w * 4);
    idx.reserve(w * 6);

    // 前面（+z）与后面（-z）整片，UV 覆盖整图。v=0 在顶部（+y）。
    glm::vec3 TL(-halfW, +halfH, +halfD), TR(+halfW, +halfH, +halfD);
    glm::vec3 BR(+halfW, -halfH, +halfD), BL(-halfW, -halfH, +halfD);
    addQuad(verts, idx, TL, TR, BR, BL, glm::vec3(0, 0, 1),
            {0,0}, {1,0}, {1,1}, {0,1});
    glm::vec3 tl(-halfW, +halfH, -halfD), tr(+halfW, +halfH, -halfD);
    glm::vec3 br(+halfW, -halfH, -halfD), bl(-halfW, -halfH, -halfD);
    addQuad(verts, idx, tr, tl, bl, br, glm::vec3(0, 0, -1),
            {1,0}, {0,0}, {0,1}, {1,1});

    // 侧壁：不透明像素在 alpha 边缘处生成墙面
    for (int py = 0; py < h; ++py) {
        for (int px = 0; px < w; ++px) {
            if (alphaAt(px, py) < OPAQUE) continue;
            float x0 = px * pixelSize - halfW;
            float x1 = x0 + pixelSize;
            float yT = halfH - py * pixelSize;
            float yB = yT - pixelSize;
            glm::vec2 uv((px + 0.5f) / (float)w, (py + 0.5f) / (float)h);

            // 左邻透明 → -x 墙
            if (alphaAt(px - 1, py) < OPAQUE)
                addQuad(verts, idx,
                    {x0, yT, +halfD}, {x0, yT, -halfD}, {x0, yB, -halfD}, {x0, yB, +halfD},
                    glm::vec3(-1, 0, 0), uv, uv, uv, uv);
            // 右邻透明 → +x 墙
            if (alphaAt(px + 1, py) < OPAQUE)
                addQuad(verts, idx,
                    {x1, yT, -halfD}, {x1, yT, +halfD}, {x1, yB, +halfD}, {x1, yB, -halfD},
                    glm::vec3(1, 0, 0), uv, uv, uv, uv);
            // 上邻（更小 py，更高 y）透明 → +y 墙
            if (alphaAt(px, py - 1) < OPAQUE)
                addQuad(verts, idx,
                    {x0, yT, -halfD}, {x1, yT, -halfD}, {x1, yT, +halfD}, {x0, yT, +halfD},
                    glm::vec3(0, 1, 0), uv, uv, uv, uv);
            // 下邻透明 → -y 墙
            if (alphaAt(px, py + 1) < OPAQUE)
                addQuad(verts, idx,
                    {x0, yB, +halfD}, {x1, yB, +halfD}, {x1, yB, -halfD}, {x0, yB, -halfD},
                    glm::vec3(0, -1, 0), uv, uv, uv, uv);
        }
    }

    stbi_image_free(pixels);

    auto model = std::unique_ptr<ItemModel>(new ItemModel());
    glGenVertexArrays(1, &model->m_vao);
    glGenBuffers(1, &model->m_vbo);
    glGenBuffers(1, &model->m_ebo);
    glBindVertexArray(model->m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, model->m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(ItemVertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model->m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ItemVertex), (void*)offsetof(ItemVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ItemVertex), (void*)offsetof(ItemVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ItemVertex), (void*)offsetof(ItemVertex, uv));
    glBindVertexArray(0);

    model->m_indexCount = (GLsizei)idx.size();
    return model;
}

void ItemModel::draw() const {
    if (!m_vao || m_indexCount == 0) return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

// ── 缓存 ──
ItemModelCache& ItemModelCache::instance() {
    static ItemModelCache inst;
    return inst;
}

const ItemModel* ItemModelCache::get(const std::string& id, const std::string& iconPath) {
    auto it = m_cache.find(id);
    if (it != m_cache.end()) return it->second.get();
    auto model = ItemModel::buildFromIcon(iconPath);
    ItemModel* raw = model.get();
    m_cache.emplace(id, std::move(model)); // 可能是 nullptr（构建失败），缓存避免反复尝试
    return raw;
}
