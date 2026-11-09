#include "BlockModel.h"
#include "../TextureMgr.h"
#include <glm/gtc/matrix_transform.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <iostream>
#include <cmath>
#include <algorithm>

// ── 小工具 ────────────────────────────────────────────────────────────────

// 绕方块中心 (0.5, y, 0.5) 旋转 Y 轴。用于把"挂在 -X 墙上的火把"烘焙成
// 其它三面墙的变体（旋转在启动时做一次，GPU 不用再算）。
static glm::vec3 rotateAroundBlockCenterY(const glm::vec3& p, float degrees) {
    float r = glm::radians(degrees);
    float c = std::cos(r);
    float s = std::sin(r);
    glm::vec3 rel = p - glm::vec3(0.5f, 0.0f, 0.5f);
    glm::vec3 out(c * rel.x + s * rel.z,
                  rel.y,
                  -s * rel.x + c * rel.z);
    return out + glm::vec3(0.5f, 0.0f, 0.5f);
}

static void rotateQuadsY(std::vector<ModelQuad>& quads, float degrees) {
    float r = glm::radians(degrees);
    float c = std::cos(r);
    float s = std::sin(r);
    for (auto& q : quads) {
        // 角点是位置：绕方块中心旋转
        for (int i = 0; i < 4; ++i) q.corners[i] = rotateAroundBlockCenterY(q.corners[i], degrees);
        // 法线是方向：只做同样的线性旋转，不平移
        glm::vec3 n = q.normal;
        q.normal = glm::vec3(c * n.x + s * n.z, n.y, -s * n.x + c * n.z);
    }
}

// 从 OBJ 读取方块模型的四边形列表。复用项目统一的 Assimp 加载路径
// （与 Model / 手持物品同一条导入管线），不再手写 OBJ 解析。
//   - Blockbench 导出的顶点以方块中心为原点（x/z ∈ [-0.5,0.5]），y 从方块底面起算，
//     这里把 x/z 平移到 0..1；
//   - aiProcess_FlipUVs 把 OBJ 的 v（自下而上）翻成方块纹理数组的约定（v=0 为图像顶部）；
//   - 不开启 aiProcess_Triangulate，OBJ 里的四边形会原样保留；
//     导入器若仍给出三角形，则复制最后一个顶点补成退化四边形（第二个三角形退化，无副作用）。
static std::vector<ModelQuad> loadObjQuads(const std::string& path, int textureLayer) {
    std::vector<ModelQuad> out;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_FlipUVs);
    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        std::cerr << "[BlockModel] 无法加载模型文件: " << path
                  << " (" << importer.GetErrorString() << ")" << std::endl;
        return out;
    }

    auto toVec3 = [](const aiVector3D& v) { return glm::vec3(v.x, v.y, v.z); };
    auto toVec2 = [](const aiVector3D& v) { return glm::vec2(v.x, v.y); };

    for (unsigned m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        if (!mesh || !mesh->mVertices) continue;

        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices < 3 || face.mNumIndices > 4) {
                std::cerr << "[BlockModel] " << path << " 中出现非三角形/四边形面（"
                          << face.mNumIndices << " 顶点），已跳过" << std::endl;
                continue;
            }

            ModelQuad q;
            q.textureLayer = textureLayer;
            q.flags = MODEL_QUAD_ALPHA_TEST;

            for (unsigned i = 0; i < 4; ++i) {
                // 三角形时复制最后一个顶点，让第二个三角形退化
                unsigned src = (i < face.mNumIndices) ? i : (face.mNumIndices - 1);
                unsigned vi = face.mIndices[src];
                if (vi >= mesh->mNumVertices) continue;

                q.corners[i] = toVec3(mesh->mVertices[vi]) + glm::vec3(0.5f, 0.0f, 0.5f);
                q.uv[i] = mesh->mTextureCoords[0]
                    ? toVec2(mesh->mTextureCoords[0][vi])
                    : glm::vec2(0.0f);
            }

            if (mesh->mNormals) {
                unsigned vi = face.mIndices[0];
                q.normal = (vi < mesh->mNumVertices) ? toVec3(mesh->mNormals[vi])
                                                     : glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                glm::vec3 e1 = q.corners[1] - q.corners[0];
                glm::vec3 e2 = q.corners[2] - q.corners[0];
                q.normal = glm::cross(e1, e2);
                q.normal = (glm::length(q.normal) > 1e-6f) ? glm::normalize(q.normal)
                                                           : glm::vec3(0.0f, 1.0f, 0.0f);
            }
            out.push_back(q);
        }
    }
    return out;
}

// 备用模型：一个轴对齐盒子。OBJ 缺失时至少还能看到火把。
static std::vector<ModelQuad> makeBoxQuads(glm::vec3 mn, glm::vec3 mx, int textureLayer) {
    auto make = [&](glm::vec3 n, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
        ModelQuad q;
        q.corners[0] = a; q.corners[1] = b; q.corners[2] = c; q.corners[3] = d;
        q.uv[0] = {0, 1}; q.uv[1] = {1, 1}; q.uv[2] = {1, 0}; q.uv[3] = {0, 0};
        q.normal = n;
        q.textureLayer = textureLayer;
        q.flags = MODEL_QUAD_ALPHA_TEST;
        return q;
    };
    float x0 = mn.x, y0 = mn.y, z0 = mn.z;
    float x1 = mx.x, y1 = mx.y, z1 = mx.z;
    std::vector<ModelQuad> q;
    q.push_back(make({ 1, 0, 0 }, { x1,y0,z1 }, { x1,y0,z0 }, { x1,y1,z0 }, { x1,y1,z1 }));
    q.push_back(make({ -1, 0, 0 }, { x0,y0,z0 }, { x0,y0,z1 }, { x0,y1,z1 }, { x0,y1,z0 }));
    q.push_back(make({ 0, 1, 0 }, { x0,y1,z0 }, { x0,y1,z1 }, { x1,y1,z1 }, { x1,y1,z0 }));
    q.push_back(make({ 0, -1, 0 }, { x0,y0,z1 }, { x0,y0,z0 }, { x1,y0,z0 }, { x1,y0,z1 }));
    q.push_back(make({ 0, 0, 1 }, { x0,y0,z1 }, { x1,y0,z1 }, { x1,y1,z1 }, { x0,y1,z1 }));
    q.push_back(make({ 0, 0, -1 }, { x1,y0,z0 }, { x0,y0,z0 }, { x0,y1,z0 }, { x1,y1,z0 }));
    return q;
}

// ── 注册表 ────────────────────────────────────────────────────────────────

BlockModelRegistry& BlockModelRegistry::instance() {
    static BlockModelRegistry inst;
    return inst;
}

void BlockModelRegistry::initialize() {
    if (m_ready) return;
    m_ready = true;

    registerTorch();

    std::cout << "[BlockModel] 已烘焙 " << m_gpuQuads.size() << " 条 quad，"
              << "模型方块类型数: ";
    int n = 0;
    for (int i = 0; i < 256; ++i) if (m_isModel[i]) ++n;
    std::cout << n << std::endl;
}

ModelState* BlockModelRegistry::stateSlot(BlockType type, uint8_t orient, uint8_t variant) {
    uint8_t t = static_cast<uint8_t>(type);
    orient &= 0xFu;
    variant &= 0xFu;
    if (variant == 0) return &m_statesV0[(size_t)t * 16 + orient];
    uint32_t key = (uint32_t(t) << 8) | (uint32_t(orient) << 4) | variant;
    return &m_statesOther[key];   // unordered_map::operator[] 默认构造
}

const ModelState* BlockModelRegistry::getState(BlockType type, uint8_t orient,
                                               uint8_t variant) const {
    uint8_t t = static_cast<uint8_t>(type);
    orient &= 0xFu;
    variant &= 0xFu;
    if (variant == 0) {
        const ModelState& s = m_statesV0[(size_t)t * 16 + orient];
        return s.valid ? &s : nullptr;
    }
    uint32_t key = (uint32_t(t) << 8) | (uint32_t(orient) << 4) | variant;
    auto it = m_statesOther.find(key);
    return (it != m_statesOther.end() && it->second.valid) ? &it->second : nullptr;
}

uint32_t BlockModelRegistry::getQuadId(BlockType type, uint8_t orient,
                                       uint8_t variant, uint8_t localQuad) const {
    const ModelState* s = getState(type, orient, variant);
    if (!s || localQuad >= s->quadCount) return 0;
    return s->firstQuad + localQuad;
}

void BlockModelRegistry::addState(BlockType type, uint8_t orient, uint8_t variant,
                                  const std::vector<ModelQuad>& quads) {
    if (quads.empty()) return;
    ModelState* s = stateSlot(type, orient, variant);
    if (s->valid) return;   // 已注册

    s->valid = true;
    s->firstQuad = (uint32_t)m_gpuQuads.size();
    s->quadCount = (uint8_t)std::min<size_t>(quads.size(), 255);

    for (size_t i = 0; i < s->quadCount; ++i) {
        const ModelQuad& q = quads[i];
        GpuQuad g;
        g.p0 = glm::vec4(q.corners[0], 0.0f);
        g.p1 = glm::vec4(q.corners[1], 0.0f);
        g.p2 = glm::vec4(q.corners[2], 0.0f);
        g.p3 = glm::vec4(q.corners[3], 0.0f);
        g.n  = glm::vec4(q.normal, 0.0f);
        g.uv01 = glm::vec4(q.uv[0], q.uv[1]);
        g.uv23 = glm::vec4(q.uv[2], q.uv[3]);
        g.meta = glm::uvec4((uint32_t)q.textureLayer, q.flags, 0u, 0u);
        m_gpuQuads.push_back(g);
    }

    m_isModel[static_cast<uint8_t>(type)] = true;
}

void BlockModelRegistry::registerTorch() {
    int layer = TextureMgr::GetInstance()->GetTextureLayerIndex("torch");
    if (layer < 0) {
        std::cerr << "[BlockModel] 未找到 torch 纹理层，火把将退回层 0" << std::endl;
        layer = 0;
    }

    // 站立火把（地面）
    std::vector<ModelQuad> standing =
        loadObjQuads("assert/mode/template_torch/template_torch.obj", layer);
    if (standing.empty()) {
        std::cerr << "[BlockModel] 站立火把 OBJ 加载失败，使用备用盒子" << std::endl;
        standing = makeBoxQuads({0.4375f, 0.0f, 0.4375f},
                                {0.5625f, 0.625f, 0.5625f}, layer);
    }

    // 墙上火把。项目里的 template_torch_wall_unlit 与点亮版只有贴图不同，
    // 几何一致，直接复用其 OBJ。
    std::vector<ModelQuad> wall =
        loadObjQuads("assert/mode/template_torch_wall_unlit/template_torch_wall_unlit.obj", layer);
    if (wall.empty()) {
        std::cerr << "[BlockModel] 墙上火把 OBJ 加载失败，使用备用盒子" << std::endl;
        wall = makeBoxQuads({0.0f, 0.1875f, 0.4375f},
                            {0.3125f, 0.8125f, 0.5625f}, layer);
    }

    // orient 的语义（见 Ray.cpp / Item.cpp）：hitFace 是【放置格指向支撑方块】的方向，
    // 所以 ORIENT_NX 表示支撑在 -X 侧（火把贴在 -X 墙上、朝 +X 倾斜）。
    // 基准 OBJ 正好挂在 -X 墙上、朝 +X 伸出，故它对应 ORIENT_NX。
    addState(BLOCK_TORCH, ORIENT_PY, 0, standing);   // 支撑在上方（正常放置不可达，兜底）
    addState(BLOCK_TORCH, ORIENT_NX, 0, wall);       // 支撑在 -X 侧，朝 +X
    {
        std::vector<ModelQuad> q = wall;
        rotateQuadsY(q, 180.0f);
        addState(BLOCK_TORCH, ORIENT_PX, 0, q);      // 支撑在 +X 侧，朝 -X
    }
    {
        std::vector<ModelQuad> q = wall;
        rotateQuadsY(q, -90.0f);
        addState(BLOCK_TORCH, ORIENT_NZ, 0, q);      // 支撑在 -Z 侧，朝 +Z
    }
    {
        std::vector<ModelQuad> q = wall;
        rotateQuadsY(q, 90.0f);
        addState(BLOCK_TORCH, ORIENT_PZ, 0, q);      // 支撑在 +Z 侧，朝 -Z
    }
    // 兜底：未指定朝向时按站立处理（ORIENT_NY = 支撑在下方 = 地面放置）
    addState(BLOCK_TORCH, ORIENT_NONE, 0, standing);
    addState(BLOCK_TORCH, ORIENT_NY, 0, standing);
}
