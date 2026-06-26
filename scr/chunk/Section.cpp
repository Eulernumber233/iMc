#include "Section.h"
#include <algorithm>
#include <cstring>

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

    int idx;
    if (!m_freeSlots.empty()) {
        // 复用一个 ERRER 占位槽：原地写入，不增长数组长度
        idx = (int)m_freeSlots.back();
        m_freeSlots.pop_back();
        m_instanceData[idx] = InstanceData(packed, (uint16_t)type, (uint16_t)textureLayer);
        if (m_errerCount > 0) m_errerCount--;
    } else {
        // 没有可复用槽 → 数组尾部追加；slot.count 在上传时由 ChunkManager 同步
        idx = (int)m_instanceData.size();
        m_instanceData.emplace_back(packed, (uint16_t)type, (uint16_t)textureLayer);
    }
    m_PosToInstanceIndex[key] = idx;
    m_dirty = true;
    // 已标记全量重建时无需累积增量 index（最终会全量传）
    if (!m_fullRebuildPending) m_dirtyIndices.push_back((uint32_t)idx);
}

void Section::removeFaceLocal(int x, int y, int z, BlockFace face) {
    BlockFaceLocKey key{ (uint8_t)x, (uint8_t)y, (uint8_t)z, face };
    auto it = m_PosToInstanceIndex.find(key);
    if (it == m_PosToInstanceIndex.end()) return;

    int index = it->second;
    // 占位：g_buffer.frag 见到 BLOCK_ERRER 直接 discard。位置上仍占一格，slot.count 不变。
    // 同时把 index 收进 free list，下次 addFaceLocal 优先复用此槽，避免数组无限膨胀。
    m_instanceData[index].blockType = BLOCK_ERRER;
    m_PosToInstanceIndex.erase(it);
    m_errerCount++;
    m_dirty = true;
    if (!m_fullRebuildPending) m_dirtyIndices.push_back((uint32_t)index);
    m_freeSlots.push_back((uint32_t)index);
}

void Section::updateFaceWithNeighbor(int x, int y, int z, BlockFace face, BlockState neighbor) {
    BlockState state = getBlock(x, y, z);
    if (state.type() == BLOCK_AIR) {
        removeFaceLocal(x, y, z, face);
        return;
    }
    bool visible = (neighbor.type() == BLOCK_AIR);
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
        BlockFace face = InstanceData::unpackFace(d.packed);
        BlockFaceLocKey key{ lx, ly, lz, face };
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

void Section::rebuildVisibilityInternal(const Section* above, const Section* below) {
    m_instanceData.clear();
    m_PosToInstanceIndex.clear();
    m_errerCount = 0;
    // rebuild 是大幅替换 → 不能走增量
    m_dirtyIndices.clear();
    m_freeSlots.clear();
    m_fullRebuildPending = true;

    BlockFace allFaces[6] = { RIGHT, LEFT, FRONT, BACK, UP, DOWN };

    for (int z = 0; z < DEPTH; ++z) {
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                BlockState state = m_box->blocks[idx(x, y, z)];
                BlockType block = state.type();
                if (block == BLOCK_AIR) continue;

                BlockProperties props = GetBlockProperties(block);
                if (props.isTransparent) {
                    for (int f = 0; f < 6; ++f) addFaceLocal(x, y, z, allFaces[f], state);
                    continue;
                }
                if (!props.isSolid) continue;

                for (int f = 0; f < 6; ++f) {
                    int nx = x, ny = y, nz = z;
                    switch (allFaces[f]) {
                    case RIGHT: nx++; break;
                    case LEFT:  nx--; break;
                    case UP:    ny++; break;
                    case DOWN:  ny--; break;
                    case FRONT: nz++; break;
                    case BACK:  nz--; break;
                    }

                    BlockType nb;
                    if (nx < 0 || nx >= WIDTH || nz < 0 || nz >= DEPTH) {
                        // 横向跨 chunk：worker 不知道，默认不可见，主线程 stitch 时再补
                        continue;
                    } else if (ny < 0) {
                        nb = below ? below->getBlock(x, HEIGHT - 1, z).type() : BLOCK_AIR;
                    } else if (ny >= HEIGHT) {
                        nb = above ? above->getBlock(x, 0, z).type() : BLOCK_AIR;
                    } else {
                        nb = m_box->blocks[idx(nx, ny, nz)].type();
                    }

                    if (nb == BLOCK_AIR) {
                        addFaceLocal(x, y, z, allFaces[f], state);
                    }
                }
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
