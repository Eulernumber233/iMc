#include "Section.h"
#include <algorithm>

Section::Section()
    : m_blocks(std::make_unique<BlockArray>())
{
    m_blocks->fill(BLOCK_AIR);
}

void Section::setCoords(int chunkX, int chunkZ, int sectionY) {
    m_chunkX = chunkX;
    m_chunkZ = chunkZ;
    m_sectionY = sectionY;
}

BlockType Section::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return BLOCK_AIR;
    }
    return (*m_blocks)[idx(x, y, z)];
}

void Section::setBlockRaw(int x, int y, int z, BlockType b) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) return;
    (*m_blocks)[idx(x, y, z)] = b;
}

void Section::addFaceLocal(int x, int y, int z, BlockFace face, BlockType type) {
    BlockFaceLocKey key{ (uint8_t)x, (uint8_t)y, (uint8_t)z, face };
    if (m_PosToInstanceIndex.find(key) != m_PosToInstanceIndex.end()) {
        return;
    }
    int textureLayer = BlockFaceType::getTextureLayer({ type, face });
    uint32_t packed = InstanceData::makePacked(
        (uint8_t)x, (uint8_t)y, (uint8_t)z, face, /*orient*/ 0u);

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

void Section::updateFaceWithNeighbor(int x, int y, int z, BlockFace face, BlockType neighborBlock) {
    BlockType block = getBlock(x, y, z);
    if (block == BLOCK_AIR) {
        removeFaceLocal(x, y, z, face);
        return;
    }
    bool visible = (neighborBlock == BLOCK_AIR);
    BlockFaceLocKey key{ (uint8_t)x, (uint8_t)y, (uint8_t)z, face };
    bool exists = m_PosToInstanceIndex.find(key) != m_PosToInstanceIndex.end();
    if (visible && !exists) {
        addFaceLocal(x, y, z, face, block);
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
                BlockType block = (*m_blocks)[idx(x, y, z)];
                if (block == BLOCK_AIR) continue;

                BlockProperties props = GetBlockProperties(block);
                if (props.isTransparent) {
                    for (int f = 0; f < 6; ++f) addFaceLocal(x, y, z, allFaces[f], block);
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
                        nb = below ? below->getBlock(x, HEIGHT - 1, z) : BLOCK_AIR;
                    } else if (ny >= HEIGHT) {
                        nb = above ? above->getBlock(x, 0, z) : BLOCK_AIR;
                    } else {
                        nb = (*m_blocks)[idx(nx, ny, nz)];
                    }

                    if (nb == BLOCK_AIR) {
                        addFaceLocal(x, y, z, allFaces[f], block);
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
    // GPU slot 清空，之前累计的 dirty index 全部失效
    // free list 里的 ERRER 槽下一次全量上传会被一并写回 GPU，但正确
    // m_dirty 必须为 true：下次该 section 重新进入活跃半径时，rebuildDrawCommands 才会触发 uploadSection
    // → 找不到 oldSlot → 走 reupload 全量分支，重新分配 slot 并完整上传。
    m_dirty = true;
    m_dirtyIndices.clear();
    m_fullRebuildPending = true;
}

void Section::adoptFrom(Section&& other) {
    m_blocks = std::move(other.m_blocks);
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
