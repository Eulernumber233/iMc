#include "Section.h"
#include <algorithm>
#include <cstring>

// ── 烘焙网格辅助 ──────────────────────────────────────────────────────
//
// 这里的 faceMatrices / 基础 quad 数据与 shader/g_buffer.vert 中的完全一致：
// 实例化路径把这些变换放在顶点着色器里做；烘焙路径在 CPU 上一次性做完，
// 结果直接是 section 局部坐标，shader 只做 uSectionBase + pos 的平移。
#ifdef IMC_BAKED_SECTION_MESH
namespace {

// 单位 quad（+Z 朝向）的 4 个角，与 BlockRenderer::createFaceVertices 一致。
const glm::vec3 kBasePos[4] = {
    {-0.5f, -0.5f, 0.0f},
    { 0.5f, -0.5f, 0.0f},
    { 0.5f,  0.5f, 0.0f},
    {-0.5f,  0.5f, 0.0f},
};
const glm::vec2 kBaseUV[4] = {
    {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f},
};

// 与 g_buffer.vert 的 faceMatrices 逐列相同（GLM 构造参数是列向量）。
const glm::mat4 kFaceMatrices[6] = {
    glm::mat4(glm::vec4( 0,0,-1,0), glm::vec4(0,1,0,0), glm::vec4( 1,0,0,0), glm::vec4( 0.5f,0,0,1)), // RIGHT
    glm::mat4(glm::vec4( 0,0, 1,0), glm::vec4(0,1,0,0), glm::vec4(-1,0,0,0), glm::vec4(-0.5f,0,0,1)), // LEFT
    glm::mat4(glm::vec4( 1,0, 0,0), glm::vec4(0,1,0,0), glm::vec4( 0,0,1,0), glm::vec4(0,0, 0.5f,1)), // FRONT
    glm::mat4(glm::vec4(-1,0, 0,0), glm::vec4(0,1,0,0), glm::vec4( 0,0,-1,0), glm::vec4(0,0,-0.5f,1)), // BACK
    glm::mat4(glm::vec4( 1,0, 0,0), glm::vec4(0,0,-1,0), glm::vec4(0,1,0,0), glm::vec4(0, 0.5f,0,1)), // UP
    glm::mat4(glm::vec4( 1,0, 0,0), glm::vec4(0,0, 1,0), glm::vec4(0,-1,0,0), glm::vec4(0,-0.5f,0,1)), // DOWN
};

// BlockFace / BlockOrient 0..5 → axis = idx>>1（0=X, 1=Z, 2=Y）。
const int kVAxis[6] = { 2, 2, 2, 2, 1, 1 };

inline uint32_t packBakedMeta(BlockType type, int layer, int face, uint32_t flags) {
    uint32_t l = (layer < 0) ? 0u : (uint32_t)layer;
    if (l > 255u) l = 255u;
    return (uint32_t(uint8_t(type)) & 0xFFu)
         | ((l & 0xFFu) << 8)
         | ((uint32_t(face) & 0xFu) << 16)
         | ((flags & 0xFu) << 20);
}

// 与 g_buffer.vert 的 isEndFace / shouldRotateUV 等价（CPU 版）。
inline bool isEndFaceCpu(BlockFace face, uint8_t orient) {
    if (orient == ORIENT_NONE) return false;
    return ((int(face) >> 1) == (int(orient) >> 1));
}
inline bool shouldRotateUvCpu(BlockFace face, uint8_t orient) {
    if (orient == ORIENT_NONE) return false;
    int orientAxis = int(orient) >> 1;
    if (orientAxis == 2) return false;                 // 主轴 Y
    if ((int(face) >> 1) == orientAxis) return false;  // 端面
    return kVAxis[int(face)] != orientAxis;
}

// 与 BlockFace 顺序一致的方向表：RIGHT/LEFT/FRONT/BACK/UP/DOWN。
constexpr int kFaceDir[6][3] = {
    {  1, 0,  0 }, { -1, 0,  0 },
    {  0, 0,  1 }, {  0, 0, -1 },
    {  0, 1,  0 }, {  0,-1,  0 },
};

// 立方体面的几何模板：把 kFaceMatrices 的变换提前算好，建 section 时只做加法。
struct FaceTemplate {
    glm::vec3 pos[4];     // 已含 +0.5 方块中心偏移，可直接加 blockOrigin
    glm::vec3 normal;
    glm::vec2 uv[4];
};

const std::array<FaceTemplate, 6>& faceTemplates() {
    // 函数内 static：首次调用（可能发生在 worker 线程）时初始化一次，之后只读。
    static const std::array<FaceTemplate, 6> table = [] {
        std::array<FaceTemplate, 6> t{};
        for (int f = 0; f < 6; ++f) {
            const glm::mat4& fm = kFaceMatrices[f];
            t[f].normal = glm::mat3(fm) * glm::vec3(0.0f, 0.0f, 1.0f);
            for (int i = 0; i < 4; ++i) {
                t[f].pos[i] = glm::vec3(0.5f) + glm::vec3(fm * glm::vec4(kBasePos[i], 1.0f));
                t[f].uv[i]  = kBaseUV[i];
            }
        }
        return t;
    }();
    return table;
}

} // namespace

// 构建烘焙网格时需要的一层邻居方块（16×16）。索引布局见 copyBoundaryLayer。
using BakedBoundaryLayer = std::array<BlockState, Section::WIDTH * Section::DEPTH>;

namespace {

inline int bakedBoxIndex(int x, int y, int z) {
    return (y * Section::DEPTH + z) * Section::WIDTH + x;
}

// 从一个 section box 拷出边界一层（持读锁）。
//   X 面索引：y*D + z；Z 面索引：y*W + x；Y 面索引：z*W + x。
void copyBoundaryLayer(const BlockBox* box, BlockFace face, bool maxSide,
                       BakedBoundaryLayer& out) {
    out.fill(BlockState{});
    if (!box) return;
    std::shared_lock<std::shared_mutex> lk(box->mutex);
    const auto& b = box->blocks;
    switch (face) {
    case RIGHT:
    case LEFT: {
        const int x = maxSide ? Section::WIDTH - 1 : 0;
        for (int y = 0; y < Section::HEIGHT; ++y)
            for (int z = 0; z < Section::DEPTH; ++z)
                out[y * Section::DEPTH + z] = b[bakedBoxIndex(x, y, z)];
        break;
    }
    case FRONT:
    case BACK: {
        const int z = maxSide ? Section::DEPTH - 1 : 0;
        for (int y = 0; y < Section::HEIGHT; ++y)
            for (int x = 0; x < Section::WIDTH; ++x)
                out[y * Section::WIDTH + x] = b[bakedBoxIndex(x, y, z)];
        break;
    }
    case UP:
    case DOWN: {
        const int y = maxSide ? Section::HEIGHT - 1 : 0;
        for (int z = 0; z < Section::DEPTH; ++z)
            for (int x = 0; x < Section::WIDTH; ++x)
                out[z * Section::WIDTH + x] = b[bakedBoxIndex(x, y, z)];
        break;
    }
    }
}

} // namespace
#endif // IMC_BAKED_SECTION_MESH

Section::Section()
    : m_box(std::make_shared<BlockBox>())
{
    // BlockBox 构造函数已把数据整段写 0（type=BLOCK_AIR, orient=ORIENT_PX）。
}

void Section::setCoords(int chunkX, int chunkZ, int sectionY) {
    m_chunkX = chunkX;
    m_chunkZ = chunkZ;
    m_sectionY = sectionY;
}

BlockState Section::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return BlockState{};
    }
    return m_box->blocks[idx(x, y, z)];
}

void Section::setBlock(int x, int y, int z, BlockState s) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) return;
    // 玩家修改方块数据：持写锁，与 worker（Task 2）读邻居边界的读锁互斥。
    std::unique_lock<std::shared_mutex> lk(m_box->mutex);
    m_box->blocks[idx(x, y, z)] = s;
}

void Section::insertInstance(const BlockFaceLocKey& key, const InstanceData& data) {
    int idx;
    if (!m_freeSlots.empty()) {
        // 复用一个 ERRER 占位槽：原地写入，不增长数组长度
        idx = (int)m_freeSlots.back();
        m_freeSlots.pop_back();
        m_instanceData[idx] = data;
        if (m_errerCount > 0) m_errerCount--;
    } else {
        // 没有可复用槽 → 数组尾部追加；slot.count 在上传时由 ChunkManager 同步
        idx = (int)m_instanceData.size();
        m_instanceData.push_back(data);
    }
    m_PosToInstanceIndex[key] = idx;
    m_dirty = true;
    // 已标记全量重建时无需累积增量 index（最终会全量传）
    if (!m_fullRebuildPending) m_dirtyIndices.push_back((uint32_t)idx);
}

void Section::eraseInstance(const BlockFaceLocKey& key) {
    auto it = m_PosToInstanceIndex.find(key);
    if (it == m_PosToInstanceIndex.end()) return;

    int index = it->second;
    // 占位：g_buffer.frag 见到 BLOCK_ERRER 直接 discard。位置上仍占一格，slot.count 不变。
    // 同时把 index 收进 free list，下次插入优先复用此槽，避免数组无限膨胀。
    m_instanceData[index].blockType = BLOCK_ERRER;
    m_PosToInstanceIndex.erase(it);
    m_errerCount++;
    m_dirty = true;
    if (!m_fullRebuildPending) m_dirtyIndices.push_back((uint32_t)index);
    m_freeSlots.push_back((uint32_t)index);
}

void Section::addFaceLocal(int x, int y, int z, BlockFace face, BlockState state) {
    BlockFaceLocKey key{ (uint8_t)x, (uint8_t)y, (uint8_t)z, face };
    if (m_PosToInstanceIndex.find(key) != m_PosToInstanceIndex.end()) {
        return;
    }
    BlockType type = state.type();
    // 纹理层选择：
    //   - 带轴方块（hasAxis，如原木）：CPU 端固定写"侧面层"（不看 face），shader 拿到后按
    //     orient 决定哪两个面切换到 endLayer。这样无论 orient 是 PY/PX/PZ，无论这个面在
    //     默认放置下叫什么名字，shader 都能正确选层。
    //   - 其他方块：按 face 查 type_to_texture 表（草方块顶面 grass_top、底面 dirt 等）。
    int sideLayer = BlockFaceType::getSideLayer(type);
    int textureLayer = (sideLayer >= 0)
        ? sideLayer
        : BlockFaceType::getTextureLayer({ type, face });
    uint32_t packed = InstanceData::makePacked(
        (uint8_t)x, (uint8_t)y, (uint8_t)z, face, state.orient());

    insertInstance(key, InstanceData(packed, (uint16_t)type, (uint16_t)textureLayer));
}

void Section::removeFaceLocal(int x, int y, int z, BlockFace face) {
    eraseInstance(BlockFaceLocKey{ (uint8_t)x, (uint8_t)y, (uint8_t)z, face });
}

void Section::addModelQuadLocal(int x, int y, int z, uint8_t localQuad,
                                BlockState state, uint32_t gpuQuadId) {
    BlockFaceLocKey key{ (uint8_t)x, (uint8_t)y, (uint8_t)z,
                         static_cast<BlockFace>(localQuad) };
    if (m_PosToInstanceIndex.find(key) != m_PosToInstanceIndex.end()) return;

    // 模型实例：quadId 放 packed 的 bits 12..30，textureLayer 字段存 localQuadIndex。
    // GPU 端纹理层/UV/法线都从 quad 表读，不再用 textureLayer 属性。
    uint32_t packed = InstanceData::makeModelPacked(
        (uint8_t)x, (uint8_t)y, (uint8_t)z, gpuQuadId);
    insertInstance(key, InstanceData(packed, (uint16_t)state.type(), (uint16_t)localQuad));
}

void Section::removeModelQuadLocal(int x, int y, int z, uint8_t localQuad) {
    eraseInstance(BlockFaceLocKey{ (uint8_t)x, (uint8_t)y, (uint8_t)z,
                                    static_cast<BlockFace>(localQuad) });
}

void Section::removeBlockInstances(int x, int y, int z, BlockState oldState) {
    BlockType type = oldState.type();
    if (type == BLOCK_AIR) return;

    const BlockModelRegistry& reg = BlockModelRegistry::instance();
    if (reg.isModelBlock(type)) {
        const ModelState* ms = reg.getState(type, oldState.orient(), oldState.variant());
        if (!ms) return;
        for (uint8_t q = 0; q < ms->quadCount; ++q) removeModelQuadLocal(x, y, z, q);
        return;
    }

    for (int f = 0; f < 6; ++f) removeFaceLocal(x, y, z, (BlockFace)f);
}

void Section::updateFaceWithNeighbor(int x, int y, int z, BlockFace face, BlockState neighbor) {
    BlockState state = getBlock(x, y, z);
    if (state.type() == BLOCK_AIR) {
        removeFaceLocal(x, y, z, face);
        return;
    }
    // 模型方块的 quad 不随邻居变化（火把/楼梯等由自身模型决定可见性）
    if (BlockModelRegistry::instance().isModelBlock(state.type())) return;

    // 只有「不透明满方块」邻居才挡面；透明方块（水/树叶/火把）不挡。
    bool visible = !BlockOccludesFace(neighbor.type());
    BlockFaceLocKey key{ (uint8_t)x, (uint8_t)y, (uint8_t)z, face };
    bool exists = m_PosToInstanceIndex.find(key) != m_PosToInstanceIndex.end();
    if (visible && !exists) {
        addFaceLocal(x, y, z, face, state);
    } else if (!visible && exists) {
        removeFaceLocal(x, y, z, face);
    }
}

void Section::compact() {
    if (m_errerCount == 0) return;

    std::vector<InstanceData> nd;
    std::unordered_map<BlockFaceLocKey, int> nm;
    nd.reserve(m_instanceData.size() - m_errerCount);

    for (size_t i = 0; i < m_instanceData.size(); ++i) {
        const auto& d = m_instanceData[i];
        if (d.blockType == BLOCK_ERRER) continue;

        // 局部坐标直接从 packed 字段拆出（不再需要从世界坐标反推）
        uint8_t lx = InstanceData::unpackX(d.packed);
        uint8_t ly = InstanceData::unpackY(d.packed);
        uint8_t lz = InstanceData::unpackZ(d.packed);
        // 立方体实例的槽位是 face(0..5)；模型实例是 localQuadIndex（存在 textureLayer 低字节）。
        uint8_t slot = InstanceData::isModel(d.packed)
            ? (uint8_t)(d.textureLayer & 0xFFu)
            : (uint8_t)InstanceData::unpackFace(d.packed);
        BlockFaceLocKey key{ lx, ly, lz, static_cast<BlockFace>(slot) };
        nm[key] = (int)nd.size();
        nd.push_back(d);
    }

    m_instanceData.swap(nd);
    m_PosToInstanceIndex.swap(nm);
    m_errerCount = 0;
    m_dirty = true;
    // compact 改变了 instanceData 整体布局 → 走全量
    m_fullRebuildPending = true;
    m_dirtyIndices.clear();
    m_freeSlots.clear();
}

#ifdef IMC_BAKED_SECTION_MESH
// 从方块数据直接把整个 section 展开成「每 quad 4 顶点 + 2 三角形」的烘焙网格。
// 这是原版 MC「重建整个 section 网格」的等价物，也是烘焙路径唯一的数据入口：
//   - 立方体面：逐格做邻居可见性判定，可见才写面；
//   - 模型方块：按 (type, orient, variant) 查一次状态，然后整组拷贝预烘焙模板。
// 全程不进 InstanceData、不建哈希表；改一个方块 = 整 section 重做。
void Section::rebuildBakedMeshFromBlocks(const BlockBox* above, const BlockBox* below,
                                         const BlockBox* neighbors[4]) {
    // 先把需要跨 section / 跨 chunk 读的边界层各拷一份（每份只加一次读锁），
    // 之后逐格查询就是纯数组访问。
    BakedBoundaryLayer layerPosX, layerNegX, layerPosZ, layerNegZ, layerUp, layerDown;
    copyBoundaryLayer(neighbors[0], LEFT,  false, layerPosX);  // +X 邻居的 x=0 面
    copyBoundaryLayer(neighbors[1], RIGHT, true,  layerNegX);  // -X 邻居的 x=W-1 面
    copyBoundaryLayer(neighbors[2], BACK,  false, layerPosZ);  // +Z 邻居的 z=0 面
    copyBoundaryLayer(neighbors[3], FRONT, true,  layerNegZ);  // -Z 邻居的 z=D-1 面
    copyBoundaryLayer(above,        DOWN,  false, layerUp);    // 上 section 的 y=0 面
    copyBoundaryLayer(below,        UP,    true,  layerDown);  // 下 section 的 y=H-1 面

    m_bakedVertices.clear();
    m_bakedIndices.clear();
    m_bakedVertices.reserve(VOLUME);
    m_bakedIndices.reserve(VOLUME + VOLUME / 2);

    // self 不加锁：worker 阶段本 section 尚未发布，主线程的读写天然串行。
    const auto& self = m_box->blocks;
    const BlockModelRegistry& reg = BlockModelRegistry::instance();
    const auto& quadTemplates = reg.bakedQuadTemplates();
    const auto& faceTable = faceTemplates();

    // 取某格在 (dx,dy,dz) 方向的邻居类型。起点在 section 内，偏移后最多只有
    // 一个坐标越界，按越界轴路由到对应的边界层（无邻居 = 空气）。
    auto neighborType = [&](int x, int y, int z, int dx, int dy, int dz) -> BlockType {
        const int nx = x + dx, ny = y + dy, nz = z + dz;
        if (nx < 0)            return layerNegX[(size_t)(ny * DEPTH + nz)].type();
        if (nx >= WIDTH)       return layerPosX[(size_t)(ny * DEPTH + nz)].type();
        if (ny < 0)            return layerDown[(size_t)(nz * WIDTH + nx)].type();
        if (ny >= HEIGHT)      return layerUp[(size_t)(nz * WIDTH + nx)].type();
        if (nz < 0)            return layerNegZ[(size_t)(ny * WIDTH + nx)].type();
        if (nz >= DEPTH)       return layerPosZ[(size_t)(ny * WIDTH + nx)].type();
        return self[bakedBoxIndex(nx, ny, nz)].type();
    };

    auto pushQuadIndices = [this](uint32_t base) {
        // 与实例化路径的 EBO {0,1,2, 0,2,3} 一致
        m_bakedIndices.push_back(base + 0);
        m_bakedIndices.push_back(base + 1);
        m_bakedIndices.push_back(base + 2);
        m_bakedIndices.push_back(base + 0);
        m_bakedIndices.push_back(base + 2);
        m_bakedIndices.push_back(base + 3);
    };

    for (int y = 0; y < HEIGHT; ++y) {
        for (int z = 0; z < DEPTH; ++z) {
            for (int x = 0; x < WIDTH; ++x) {
                const BlockState st = self[bakedBoxIndex(x, y, z)];
                const BlockType type = st.type();
                if (type == BLOCK_AIR) continue;

                const glm::vec3 origin((float)x, (float)y, (float)z);

                // ── 模型方块：一次状态查询，整组模板直拷 ──
                if (reg.isModelBlock(type)) {
                    const ModelState* ms = reg.getState(type, st.orient(), st.variant());
                    if (!ms) continue;
                    const uint32_t typeBits = (uint32_t)(uint8_t)type;
                    for (uint32_t q = 0; q < ms->quadCount; ++q) {
                        const BakedModelQuad& tq = quadTemplates[ms->firstQuad + q];
                        const uint32_t meta = tq.meta | typeBits;
                        const uint32_t base = (uint32_t)m_bakedVertices.size();
                        for (int i = 0; i < 4; ++i) {
                            m_bakedVertices.push_back(
                                { origin + tq.pos[i], tq.normal, tq.uv[i], meta });
                        }
                        pushQuadIndices(base);
                    }
                    continue;
                }

                // ── 立方体面：与 addBlockInstances 完全相同的可见性规则 ──
                const BlockProperties props = GetBlockProperties(type);
                if (!props.isTransparent && !props.isSolid) continue;

                const uint8_t orient = st.orient();
                const int sideLayer = BlockFaceType::getSideLayer(type);

                for (int f = 0; f < 6; ++f) {
                    const BlockFace face = (BlockFace)f;
                    if (!props.isTransparent &&
                        BlockOccludesFace(neighborType(x, y, z,
                            kFaceDir[f][0], kFaceDir[f][1], kFaceDir[f][2]))) {
                        continue;
                    }

                    int layer = (sideLayer >= 0)
                        ? sideLayer
                        : BlockFaceType::getTextureLayer({ type, face });
                    if (isEndFaceCpu(face, orient)) {
                        const int endLayer = BlockFaceType::end_layer_by_type[(uint8_t)type];
                        if (endLayer >= 0) layer = endLayer;
                    }

                    const FaceTemplate& ft = faceTable[f];
                    const uint32_t meta = packBakedMeta(type, layer, f, 0u);
                    const bool rotateUV = shouldRotateUvCpu(face, orient);
                    const uint32_t base = (uint32_t)m_bakedVertices.size();
                    for (int i = 0; i < 4; ++i) {
                        glm::vec2 uv = ft.uv[i];
                        if (rotateUV) uv = glm::vec2(uv.y, 1.0f - uv.x);
                        m_bakedVertices.push_back({ origin + ft.pos[i], ft.normal, uv, meta });
                    }
                    pushQuadIndices(base);
                }
            }
        }
    }

    m_bakedDirty = false;
}
#endif // IMC_BAKED_SECTION_MESH

void Section::rebuildVisibilityInternal(const Section* above, const Section* below) {
    m_instanceData.clear();
    m_PosToInstanceIndex.clear();
    m_errerCount = 0;
    // rebuild 是大幅替换 → 不能走增量
    m_dirtyIndices.clear();
    m_freeSlots.clear();
    m_fullRebuildPending = true;

    for (int z = 0; z < DEPTH; ++z) {
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                BlockState state = m_box->blocks[idx(x, y, z)];
                if (state.type() == BLOCK_AIR) continue;

                // 邻居查询：section 内直接读；垂直方向读上下 section；横向越界
                // 在这里先视为透空（跨 chunk 边界面由 Task 2 的 stitch 阶段补/删）。
                auto neighbor = [&](BlockFace face) -> BlockState {
                    int nx = x, ny = y, nz = z;
                    switch (face) {
                    case RIGHT: nx++; break;
                    case LEFT:  nx--; break;
                    case UP:    ny++; break;
                    case DOWN:  ny--; break;
                    case FRONT: nz++; break;
                    case BACK:  nz--; break;
                    }
                    if (nx < 0 || nx >= WIDTH || nz < 0 || nz >= DEPTH) return BlockState{};
                    if (ny < 0) return below ? below->getBlock(x, HEIGHT - 1, z) : BlockState{};
                    if (ny >= HEIGHT) return above ? above->getBlock(x, 0, z) : BlockState{};
                    return BlockState(m_box->blocks[idx(nx, ny, nz)]);
                };

                addBlockInstances(x, y, z, state, neighbor);
            }
        }
    }

    // 给后续 stitch 阶段预留容量：4 边 × 16×16 边界格上限 = 1024 个新增面。
    // 在 worker 上下文做这次堆分配，避免主线程 adoptFrom 时的尖峰。
    m_instanceData.reserve(m_instanceData.size() + 1024);
    m_PosToInstanceIndex.reserve(m_PosToInstanceIndex.size() + 1024);

    m_dirty = true;
}

void Section::notifyGpuSlotReleased() {
    m_dirty = true;
    m_dirtyIndices.clear();
    m_fullRebuildPending = true;
    m_gpuSlot = ChunkArena::Slot{};  // 清除缓存，下次上传走全量
}

void Section::adoptFrom(Section&& other) {
    m_box = std::move(other.m_box);
    m_instanceData = std::move(other.m_instanceData);
    m_PosToInstanceIndex = std::move(other.m_PosToInstanceIndex);
    m_errerCount = other.m_errerCount;
#ifdef IMC_BAKED_SECTION_MESH
    // worker 已经从方块数据构建好整 section 的顶点流，这里直接接管。
    m_bakedVertices = std::move(other.m_bakedVertices);
    m_bakedIndices = std::move(other.m_bakedIndices);
    m_bakedDirty = other.m_bakedDirty;
    m_bakedGpu = std::move(other.m_bakedGpu);
#endif
    m_dirty = true;
    // worker 产出的整段都是新的 → 首次上传必然是全量
    m_dirtyIndices.clear();
    m_freeSlots.clear();
    m_fullRebuildPending = true;
    // reserve 由 worker 端在 rebuildVisibilityInternal 内做（避免主线程在 adopt 时的堆分配尖峰）
}

void Section::readAllBlocks(std::vector<BlockState>& out) const {
    out.resize(VOLUME);
    // 网络序列化在主线程调用；持读锁以防 worker 此刻正读同一 box 边界（读读共享，主要是规范化）。
    std::shared_lock<std::shared_mutex> lk(m_box->mutex);
    std::memcpy(out.data(), m_box->blocks.data(), VOLUME * sizeof(BlockState));
}

void Section::writeAllBlocks(const std::vector<BlockState>& data) {
    std::unique_lock<std::shared_mutex> lk(m_box->mutex);
    std::memcpy(m_box->blocks.data(), data.data(),
        std::min(data.size(), (size_t)VOLUME) * sizeof(BlockState));
}
