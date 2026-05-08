#include "Chunk.h"
#include <iostream>
#include <algorithm>

Chunk::Chunk(const glm::ivec2& position, ChunkManager* chunkManager)
    : m_chunkManager(chunkManager), m_position(position) {
    m_minPos = glm::vec3(m_position.x * WIDTH, 0.0f, m_position.y * DEPTH);
    m_maxPos = m_minPos + glm::vec3(WIDTH, HEIGHT, DEPTH);

    for (int sy = 0; sy < SECTION_COUNT; ++sy) {
        m_sections[sy].setCoords(m_position.x, m_position.y, sy);
    }
}

Chunk::~Chunk() {
    unload();
}

glm::vec3 Chunk::getCenter() const {
    return glm::vec3(
        m_position.x * WIDTH + WIDTH / 2.0f,
        HEIGHT / 2.0f,
        m_position.y * DEPTH + DEPTH / 2.0f
    );
}

BlockType Chunk::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return BLOCK_AIR;
    }
    int sy = y / Section::HEIGHT;
    int ly = y % Section::HEIGHT;
    return m_sections[sy].getBlock(x, ly, z);
}

void Chunk::setBlock(int x, int y, int z, BlockType b) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) return;
    int sy = y / Section::HEIGHT;
    int ly = y % Section::HEIGHT;
    m_sections[sy].setBlockRaw(x, ly, z, b);
}

void Chunk::markAllDirty() {
    for (auto& s : m_sections) s.markDirty();
}

void Chunk::adoptSections(std::array<Section, SECTION_COUNT>&& sections) {
    for (int sy = 0; sy < SECTION_COUNT; ++sy) {
        m_sections[sy].setCoords(m_position.x, m_position.y, sy);
        m_sections[sy].adoptFrom(std::move(sections[sy]));
    }
    m_isLoaded = true;
    m_meshReady = true;
    refreshNonEmptyMask();
    markAllDirty();
}

void Chunk::unload() {
    if (!m_isLoaded) return;
    // 把邻居反向指针清掉，避免悬挂引用
    // slot 映射：0(+X)<->1(-X), 2(+Z)<->3(-Z)
    static constexpr int oppSlot[4] = { 1, 0, 3, 2 };
    for (int i = 0; i < 4; ++i) {
        if (m_neighbors[i] && m_neighbors[i]->m_neighbors[oppSlot[i]] == this) {
            m_neighbors[i]->m_neighbors[oppSlot[i]] = nullptr;
        }
        m_neighbors[i] = nullptr;
    }
    m_isLoaded = false;
    m_meshReady = false;
}

Chunk* Chunk::getNeighborChunk(int nx, int nz) const {
    if (nx < 0)        return m_neighbors[1]; // -X
    if (nx >= WIDTH)   return m_neighbors[0]; // +X
    if (nz < 0)        return m_neighbors[3]; // -Z
    if (nz >= DEPTH)   return m_neighbors[2]; // +Z
    return nullptr;
}

BlockType Chunk::neighborBlock(int x, int y, int z, BlockFace face) const {
    int nx = x, ny = y, nz = z;
    switch (face) {
    case RIGHT: nx++; break;
    case LEFT:  nx--; break;
    case UP:    ny++; break;
    case DOWN:  ny--; break;
    case FRONT: nz++; break;
    case BACK:  nz--; break;
    }
    if (ny < 0 || ny >= HEIGHT) return BLOCK_AIR; // 顶/底视为透空
    if (nx < 0 || nx >= WIDTH || nz < 0 || nz >= DEPTH) {
        Chunk* nc = getNeighborChunk(nx, nz);
        if (!nc) return BLOCK_AIR;
        int lx = (nx + WIDTH) % WIDTH;
        int lz = (nz + DEPTH) % DEPTH;
        return nc->getBlock(lx, ny, lz);
    }
    return getBlock(nx, ny, nz);
}

void Chunk::updateFaceAt(int x, int y, int z, BlockFace face) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) return;
    int sy = y / Section::HEIGHT;
    int ly = y % Section::HEIGHT;
    BlockType nb = neighborBlock(x, y, z, face);
    m_sections[sy].updateFaceWithNeighbor(x, ly, z, face, nb);
}

void Chunk::stitchWithNeighbor(Chunk* other, BlockFace faceFromSelf) {
    if (!other || !m_meshReady || !other->m_meshReady) return;

    // 在 self 一侧的边界（x=WIDTH-1 等）和 other 一侧（对面）双向更新。
    BlockFace oppFace;
    int selfX = -1, selfZ = -1;     // -1 表示该轴遍历
    int otherX = -1, otherZ = -1;
    switch (faceFromSelf) {
    case RIGHT: oppFace = LEFT;  selfX = WIDTH - 1; otherX = 0; break;
    case LEFT:  oppFace = RIGHT; selfX = 0;         otherX = WIDTH - 1; break;
    case FRONT: oppFace = BACK;  selfZ = DEPTH - 1; otherZ = 0; break;
    case BACK:  oppFace = FRONT; selfZ = 0;         otherZ = DEPTH - 1; break;
    default: return;
    }

    auto runRange = [](int fixedX, int fixedZ, auto&& fn) {
        for (int y = 0; y < HEIGHT; ++y) {
            if (fixedX >= 0) {
                for (int z = 0; z < DEPTH; ++z) fn(fixedX, y, z);
            } else {
                for (int x = 0; x < WIDTH; ++x) fn(x, y, fixedZ);
            }
        }
    };

    // self 边界面更新
    runRange(selfX, selfZ, [&](int x, int y, int z) {
        updateFaceAt(x, y, z, faceFromSelf);
    });
    // other 边界面更新
    runRange(otherX, otherZ, [&](int x, int y, int z) {
        other->updateFaceAt(x, y, z, oppFace);
    });
}

void Chunk::stitchHorizontalNeighbors() {
    // 4 个方向都试一次，邻居为空就跳过
    BlockFace dirs[4] = { RIGHT, LEFT, FRONT, BACK };
    int slots[4] = { 0, 1, 2, 3 };
    for (int i = 0; i < 4; ++i) {
        Chunk* nc = m_neighbors[slots[i]];
        if (!nc) continue;
        stitchWithNeighbor(nc, dirs[i]);
        nc->refreshNonEmptyMask();
    }
    refreshNonEmptyMask();
}


// 未来进一步优化：TODO
// 持久映射（GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT）
// addFaceLocal/removeFaceLocal 直接写映射内存，省掉 staging vector。
BlockType Chunk::setBlockAndUpdate(int x, int y, int z, BlockType newType) {
    std::lock_guard<std::mutex> lock(m_mutex);

    BlockType oldType = getBlock(x, y, z);
    if (oldType == newType) return oldType;

    setBlock(x, y, z, newType);

    int sy = y / Section::HEIGHT;
    int ly = y % Section::HEIGHT;
    Section& s = m_sections[sy];

    // 1. 删除旧方块的所有面（在所属 section）
    if (oldType != BLOCK_AIR) {
        BlockFace allFaces[6] = { RIGHT, LEFT, FRONT, BACK, UP, DOWN };
        for (int f = 0; f < 6; ++f) s.removeFaceLocal(x, ly, z, allFaces[f]);
    }

    // 2. 添加新方块的可见面
    if (newType != BLOCK_AIR) {
        BlockFace allFaces[6] = { RIGHT, LEFT, FRONT, BACK, UP, DOWN };
        for (int f = 0; f < 6; ++f) {
            BlockType nb = neighborBlock(x, y, z, allFaces[f]);
            if (nb == BLOCK_AIR) {
                s.addFaceLocal(x, ly, z, allFaces[f], newType);
            }
        }
    }

    // 3. 6 邻居的反向面也要重新算
    glm::ivec3 dirs[6] = {
        {x + 1,y,z}, {x - 1,y,z}, {x,y,z + 1}, {x,y,z - 1}, {x,y + 1,z}, {x,y - 1,z}
    };
    BlockFace neighborFaces[6] = { LEFT, RIGHT, BACK, FRONT, DOWN, UP };
    Chunk* touchedNeighbors[2] = { nullptr, nullptr };  // 至多 2 个跨 chunk 邻居 (X 向 + Z 向)
    int tnCount = 0;
    for (int i = 0; i < 6; ++i) {
        int nx = dirs[i].x, ny = dirs[i].y, nz = dirs[i].z;
        if (ny < 0 || ny >= HEIGHT) continue;
        if (nx < 0 || nx >= WIDTH || nz < 0 || nz >= DEPTH) {
            Chunk* nc = getNeighborChunk(nx, nz);
            if (!nc) continue;
            int lx = (nx + WIDTH) % WIDTH;
            int lz = (nz + DEPTH) % DEPTH;
            nc->updateFaceAt(lx, ny, lz, neighborFaces[i]);
            // 记录被动邻居以便循环外刷新它的 mask
            if (tnCount < 2 && touchedNeighbors[0] != nc && touchedNeighbors[1] != nc) {
                touchedNeighbors[tnCount++] = nc;
            }
        } else {
            updateFaceAt(nx, ny, nz, neighborFaces[i]);
        }
    }
    for (int i = 0; i < tnCount; ++i) touchedNeighbors[i]->refreshNonEmptyMask();

    // 4. 阈值压缩自己这个 section
    if (s.getErrerCount() > (int)s.getInstanceCount() / 4) {
        s.compact();
    }

    refreshNonEmptyMask();
    return oldType;
}

bool Chunk::aabbInFrustum(const glm::vec3& min, const glm::vec3& max,
    const std::array<glm::vec4, 6>& planes) {
    for (const auto& plane : planes) {
        glm::vec3 p = min;
        if (plane.x >= 0) p.x = max.x;
        if (plane.y >= 0) p.y = max.y;
        if (plane.z >= 0) p.z = max.z;
        if (glm::dot(glm::vec3(plane), p) + plane.w < 0) return false;
    }
    return true;
}

bool Chunk::isChunkPotentiallyVisible(std::shared_ptr<Camera> camera) {
    if (!camera) return true;
    const bool frustumEnabled = camera->FrustumCullingEnabled;

    static int frameCount = 0;
    frameCount++;

    bool cameraMoved = (glm::distance(m_cachedCameraPos, camera->Position) > 1.0f);
    bool fovChanged = (std::abs(m_cachedCameraFOV - camera->FOV) > 0.1f);

    if (cameraMoved || fovChanged || (frameCount - m_lastVisibilityFrame > 10)) {
        m_cachedCameraPos = camera->Position;
        m_cachedCameraFOV = camera->FOV;
        m_lastVisibilityFrame = frameCount;

        if (frustumEnabled) {
            auto planes = camera->GetFrustumPlanes();
            if (!aabbInFrustum(m_minPos, m_maxPos, planes)) {
                m_cachedVisibility = false;
                return false;
            }
        }
        glm::vec3 chunkCenter = getCenter();
        float distance = glm::distance(chunkCenter, camera->Position);
        const float MAX_RENDER_DISTANCE = 300.0f;
        m_cachedVisibility = (distance <= MAX_RENDER_DISTANCE);
    }
    return m_cachedVisibility;
}

bool Chunk::isSectionVisible(int sectionY, std::shared_ptr<Camera> camera) const {
    if (!camera) return true;

    // section AABB
    glm::vec3 smin(
        m_position.x * (float)WIDTH,
        sectionY * (float)Section::HEIGHT,
        m_position.y * (float)DEPTH
    );
    glm::vec3 smax = smin + glm::vec3((float)Section::WIDTH, (float)Section::HEIGHT, (float)Section::DEPTH);

    if (camera->FrustumCullingEnabled) {
        auto planes = camera->GetFrustumPlanes();
        if (!aabbInFrustum(smin, smax, planes)) return false;
    }
    // 距离用 section 中心
    glm::vec3 center = (smin + smax) * 0.5f;
    const float MAX_RENDER_DISTANCE = 300.0f;
    return glm::distance(center, camera->Position) <= MAX_RENDER_DISTANCE;
}
