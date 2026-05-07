#include "Section.h"
#include <algorithm>

Section::Section() {
    m_blocks.fill(BLOCK_AIR);
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
    return m_blocks[idx(x, y, z)];
}

void Section::setBlockRaw(int x, int y, int z, BlockType b) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) return;
    m_blocks[idx(x, y, z)] = b;
}

glm::vec3 Section::worldCenterFor(int x, int y, int z) const {
    // y 是 section 局部 y，叠加 section 起始 worldY
    int worldY = m_sectionY * HEIGHT + y;
    float wx = m_chunkX * WIDTH + x + 0.5f;
    float wy = worldY + 0.5f;
    float wz = m_chunkZ * DEPTH + z + 0.5f;
    return glm::vec3(wx, wy, wz);
}

void Section::addFaceLocal(int x, int y, int z, BlockFace face, BlockType type) {
    BlockFaceLocKey key{ (uint8_t)x, (uint8_t)y, (uint8_t)z, face };
    if (m_PosToInstanceIndex.find(key) != m_PosToInstanceIndex.end()) {
        return;
    }
    glm::vec3 wc = worldCenterFor(x, y, z);
    m_instanceData.push_back({ wc, face, type, BlockFaceType::getTextureLayer({type, face}) });
    m_PosToInstanceIndex[key] = (int)m_instanceData.size() - 1;
    m_dirty = true;
}

void Section::removeFaceLocal(int x, int y, int z, BlockFace face) {
    BlockFaceLocKey key{ (uint8_t)x, (uint8_t)y, (uint8_t)z, face };
    auto it = m_PosToInstanceIndex.find(key);
    if (it == m_PosToInstanceIndex.end()) return;

    int index = it->second;
    m_instanceData[index].blockType = BLOCK_ERRER;
    m_PosToInstanceIndex.erase(it);
    m_errerCount++;
    m_dirty = true;
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

        // 反推局部坐标。InstanceData.position 是世界坐标 + 0.5。
        int wx = (int)std::floor(d.position.x);
        int wy = (int)std::floor(d.position.y);
        int wz = (int)std::floor(d.position.z);
        int lx = wx - m_chunkX * WIDTH;
        int ly = wy - m_sectionY * HEIGHT;
        int lz = wz - m_chunkZ * DEPTH;
        BlockFaceLocKey key{ (uint8_t)lx, (uint8_t)ly, (uint8_t)lz, (BlockFace)d.faceIndex };
        nm[key] = (int)nd.size();
        nd.push_back(d);
    }

    m_instanceData.swap(nd);
    m_PosToInstanceIndex.swap(nm);
    m_errerCount = 0;
    m_dirty = true;
}

void Section::rebuildVisibilityInternal(const Section* above, const Section* below) {
    m_instanceData.clear();
    m_PosToInstanceIndex.clear();
    m_errerCount = 0;

    BlockFace allFaces[6] = { RIGHT, LEFT, FRONT, BACK, UP, DOWN };

    for (int z = 0; z < DEPTH; ++z) {
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                BlockType block = m_blocks[idx(x, y, z)];
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
                        nb = m_blocks[idx(nx, ny, nz)];
                    }

                    if (nb == BLOCK_AIR) {
                        addFaceLocal(x, y, z, allFaces[f], block);
                    }
                }
            }
        }
    }

    m_dirty = true;
}

void Section::adoptFrom(Section&& other) {
    m_blocks = other.m_blocks;
    m_instanceData = std::move(other.m_instanceData);
    m_PosToInstanceIndex = std::move(other.m_PosToInstanceIndex);
    m_errerCount = other.m_errerCount;
    m_dirty = true;
    // 坐标在 setCoords 时已设置
}
