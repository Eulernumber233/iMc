#include "Chunk.h"
#include "../Profiler.h"
#include <iostream>
#include <algorithm>
#ifdef _MSC_VER
#include <intrin.h>
#endif

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

BlockState Chunk::getBlock(int x, int y, int z) const {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) {
        return BlockState{};
    }
    int sy = y / Section::HEIGHT;
    int ly = y % Section::HEIGHT;
    return m_sections[sy].getBlock(x, ly, z);
}

void Chunk::setBlock(int x, int y, int z, BlockState s) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT || z < 0 || z >= DEPTH) return;
    int sy = y / Section::HEIGHT;
    int ly = y % Section::HEIGHT;
    m_sections[sy].setBlock(x, ly, z, s);
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
    m_saveDirty = true;  // 新数据进入，标记需要存档
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

BlockState Chunk::neighborBlock(int x, int y, int z, BlockFace face) const {
    int nx = x, ny = y, nz = z;
    switch (face) {
    case RIGHT: nx++; break;
    case LEFT:  nx--; break;
    case UP:    ny++; break;
    case DOWN:  ny--; break;
    case FRONT: nz++; break;
    case BACK:  nz--; break;
    }
    if (ny < 0 || ny >= HEIGHT) return BlockState{}; // 顶/底视为透空
    if (nx < 0 || nx >= WIDTH || nz < 0 || nz >= DEPTH) {
        Chunk* nc = getNeighborChunk(nx, nz);
        if (!nc) return BlockState{};
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
    BlockState nb = neighborBlock(x, y, z, face);
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

// 未来进一步优化：TODO
// 持久映射（GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT）
// addFaceLocal/removeFaceLocal 直接写映射内存，省掉 staging vector。
BlockState Chunk::setBlockAndUpdate(int x, int y, int z, BlockState newState) {
    BlockState oldState = getBlock(x, y, z);
    if (oldState == newState) return oldState;

    BlockType oldType = oldState.type();
    BlockType newType = newState.type();

    setBlock(x, y, z, newState);

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
            BlockState nb = neighborBlock(x, y, z, allFaces[f]);
            if (nb.type() == BLOCK_AIR) {
                s.addFaceLocal(x, ly, z, allFaces[f], newState);
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

    m_saveDirty = true;  // 方块变更，标记需要存档
    refreshNonEmptyMask();
    return oldState;
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

bool Chunk::isChunkPotentiallyVisible(const Camera* camera) {
    if (!camera) return true;
    if (!m_chunkManager) return true;

    // ChunkManager 统一追踪相机移动，递增 visGeneration；
    // chunk 只需比较本地记录的版本号，省掉原先每 chunk 各自 glm::distance 的 284 次 sqrt/帧。
    uint32_t curGen = m_chunkManager->getVisibilityGeneration();
    if (m_cachedVisGeneration == curGen) {
        return m_cachedVisibility;
    }
    m_cachedVisGeneration = curGen;

    const bool frustumEnabled = camera->FrustumCullingEnabled;
    if (frustumEnabled) {
        auto planes = camera->GetFrustumPlanes();
        if (!aabbInFrustum(m_minPos, m_maxPos, planes)) {
            m_cachedVisibility = false;
            return false;
        }
    }
    glm::vec3 d = getCenter() - camera->Position;
    const float MAX_RENDER_DISTANCE_SQ = 300.0f * 300.0f;
    m_cachedVisibility = (glm::dot(d, d) <= MAX_RENDER_DISTANCE_SQ);
    return m_cachedVisibility;
}

bool Chunk::isSectionVisible(int sectionY, const Camera* camera) const {
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

uint32_t Chunk::getVisibleSectionMask(const Camera* camera,
                                       int cameraSectionY, int maxDownSections,
                                       const std::array<glm::vec4, 6>* frustumPlanes) const {
    uint32_t mask = m_nonEmptyMask;
    if (mask == 0) return 0;
    if (!camera) return mask;

    const bool frustumEnabled = camera->FrustumCullingEnabled;
    const std::array<glm::vec4, 6>* planes = frustumPlanes;
    std::array<glm::vec4, 6> localPlanes;
    if (frustumEnabled && !planes) {
        localPlanes = camera->GetFrustumPlanes();
        planes = &localPlanes;
    }

    const int minSectionY = (maxDownSections > 0)
        ? cameraSectionY - maxDownSections
        : 0; // 0 或负值：不限制下方

    const float chunkBaseX = m_position.x * (float)WIDTH;
    const float chunkBaseZ = m_position.y * (float)DEPTH;
    const float MAX_RENDER_DISTANCE_SQ = 300.0f * 300.0f;
    const float secHalf = Section::HEIGHT * 0.5f;
    const float chalfX = chunkBaseX + WIDTH * 0.5f;
    const float chalfZ = chunkBaseZ + DEPTH * 0.5f;

    int secTested = 0, secVertCull = 0, secFrustCull = 0, secDistCull = 0;

    uint32_t result = 0;
    while (mask) {
        int sy = 0;
#ifdef _MSC_VER
        unsigned long idx;
        _BitScanForward(&idx, mask);
        sy = (int)idx;
#else
        sy = __builtin_ctz(mask);
#endif
        mask &= mask - 1u;
        ++secTested;

        // 纵向剔除：太靠下的 section 不渲染
        if (sy < minSectionY) { ++secVertCull; continue; }

        // 视锥 + 距离剔除
        if (frustumEnabled && planes) {
            float sy0 = sy * (float)Section::HEIGHT;
            glm::vec3 smin(chunkBaseX, sy0, chunkBaseZ);
            glm::vec3 smax(chunkBaseX + (float)Section::WIDTH,
                           sy0 + (float)Section::HEIGHT,
                           chunkBaseZ + (float)Section::DEPTH);
            if (!aabbInFrustum(smin, smax, *planes)) { ++secFrustCull; continue; }
        }

        // 距离剔除：用平方距离避免 sqrt
        float cy = sy * (float)Section::HEIGHT + secHalf;
        float dx = chalfX - camera->Position.x;
        float dy = cy - camera->Position.y;
        float dz = chalfZ - camera->Position.z;
        if (dx * dx + dy * dy + dz * dz > MAX_RENDER_DISTANCE_SQ) { ++secDistCull; continue; }

        result |= (1u << sy);
    }

    Profiler::addCounter("vis.secTested", secTested);
    Profiler::addCounter("vis.secVertCull", secVertCull);
    Profiler::addCounter("vis.secFrustCull", secFrustCull);
    Profiler::addCounter("vis.secDistCull", secDistCull);
    return result;
}
