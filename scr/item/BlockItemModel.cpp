#include "BlockItemModel.h"
#include <glm/glm.hpp>
#include <vector>

namespace {
struct CubeVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv;
    float     layer;
};

// 往缓冲追加一个面（两三角形，逆时针正面）。p0..p3 逆时针排列。
void addFace(std::vector<CubeVertex>& v,
             const glm::vec3& p0, const glm::vec3& p1,
             const glm::vec3& p2, const glm::vec3& p3,
             const glm::vec3& n, float layer) {
    // uv：p0=(0,1) 左下, p1=(1,1) 右下, p2=(1,0) 右上, p3=(0,0) 左上
    // 纹理 row0 在顶部（v=0），与 TextureMgr 一致，故上边用 v=0。
    v.push_back({ p0, n, {0.0f, 1.0f}, layer });
    v.push_back({ p1, n, {1.0f, 1.0f}, layer });
    v.push_back({ p2, n, {1.0f, 0.0f}, layer });
    v.push_back({ p0, n, {0.0f, 1.0f}, layer });
    v.push_back({ p2, n, {1.0f, 0.0f}, layer });
    v.push_back({ p3, n, {0.0f, 0.0f}, layer });
}

float layerOf(BlockType t, BlockFace f) {
    int l = BlockFaceType::getTextureLayer({ t, f });
    return (l >= 0) ? (float)l : 0.0f;
}
} // namespace

BlockItemModel::~BlockItemModel() {
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

std::unique_ptr<BlockItemModel> BlockItemModel::buildForBlock(BlockType type) {
    const float h = 0.5f;
    // 8 个角点
    glm::vec3 v000(-h, -h, -h), v100(+h, -h, -h), v110(+h, +h, -h), v010(-h, +h, -h);
    glm::vec3 v001(-h, -h, +h), v101(+h, -h, +h), v111(+h, +h, +h), v011(-h, +h, +h);

    std::vector<CubeVertex> verts;
    verts.reserve(36);
    // 各面按「面朝外看去左下→右下→右上→左上」逆时针取点
    // RIGHT (+X)
    addFace(verts, v101, v100, v110, v111, glm::vec3(1, 0, 0), layerOf(type, RIGHT));
    // LEFT (-X)
    addFace(verts, v000, v001, v011, v010, glm::vec3(-1, 0, 0), layerOf(type, LEFT));
    // FRONT (+Z)
    addFace(verts, v001, v101, v111, v011, glm::vec3(0, 0, 1), layerOf(type, FRONT));
    // BACK (-Z)
    addFace(verts, v100, v000, v010, v110, glm::vec3(0, 0, -1), layerOf(type, BACK));
    // UP (+Y)
    addFace(verts, v011, v111, v110, v010, glm::vec3(0, 1, 0), layerOf(type, UP));
    // DOWN (-Y)
    addFace(verts, v000, v100, v101, v001, glm::vec3(0, -1, 0), layerOf(type, DOWN));

    auto model = std::unique_ptr<BlockItemModel>(new BlockItemModel());
    glGenVertexArrays(1, &model->m_vao);
    glGenBuffers(1, &model->m_vbo);
    glBindVertexArray(model->m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, model->m_vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(CubeVertex), verts.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(CubeVertex), (void*)offsetof(CubeVertex, layer));
    glBindVertexArray(0);

    model->m_vertexCount = (GLsizei)verts.size();
    return model;
}

bool BlockItemModel::hasValidTextures(BlockType type) {
    const BlockFace faces[6] = { RIGHT, LEFT, FRONT, BACK, UP, DOWN };
    for (BlockFace f : faces)
        if (BlockFaceType::getTextureLayer({ type, f }) >= 0) return true;
    return false;
}

void BlockItemModel::draw() const {
    if (!m_vao || m_vertexCount == 0) return;
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, m_vertexCount);
    glBindVertexArray(0);
}

// ── 缓存 ──
BlockItemModelCache& BlockItemModelCache::instance() {
    static BlockItemModelCache inst;
    return inst;
}

const BlockItemModel* BlockItemModelCache::get(BlockType type) {
    int key = (int)type;
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second.get();
    auto model = BlockItemModel::buildForBlock(type);
    BlockItemModel* raw = model.get();
    m_cache.emplace(key, std::move(model));
    return raw;
}
