#pragma once
#include "../core.h"
#include "BlockType.h"
#include "ChunkDimensions.h"
#include "Section.h"
#include "ChunkManager.h"
#include <array>
#include <vector>
#include <glm/glm.hpp>
#include <memory>
#include "../Camera.h"
#include <mutex>

// 区块：16x64x16，由 4 个 16x16x16 Section 组成（Y 方向堆叠）。
class Chunk {
public:
    static constexpr int WIDTH  = ChunkConstants::CHUNK_WIDTH;
    static constexpr int HEIGHT = ChunkConstants::CHUNK_HEIGHT;
    static constexpr int DEPTH  = ChunkConstants::CHUNK_DEPTH;
    static constexpr int VOLUME = ChunkConstants::CHUNK_VOLUME;
    static constexpr int SECTION_COUNT = HEIGHT / Section::HEIGHT; // 4

    Chunk(const glm::ivec2& position, ChunkManager* chunkManager);
    ~Chunk();

    void unload();

    // 由 worker 走完后，主线程调用 adoptSections 把 result 装入本对象。
    // sections 数组已经由 worker 计算过 visibility（仅 section 内 + 垂直邻居）。
    void adoptSections(std::array<Section, SECTION_COUNT>&& sections);

    // 把横向（4 邻居）的边界面双向缝合：本 chunk 与 4 个已加载邻居各自 mesh 互改。
    void stitchHorizontalNeighbors();

    // 状态
    bool isLoaded() const { return m_isLoaded; }
    bool isMeshReady() const { return m_meshReady; }

    glm::ivec2 getPosition() const { return m_position; }
    glm::vec3 getCenter() const;
    const glm::vec3& getMinPos() const { return m_minPos; }
    const glm::vec3& getMaxPos() const { return m_maxPos; }

    // 全局（chunk 局部）坐标，y ∈ [0, HEIGHT)
    BlockType getBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, BlockType b);
    BlockType setBlockAndUpdate(int x, int y, int z, BlockType newType);

    // section 访问
    Section& getSection(int sy) { return m_sections[sy]; }
    const Section& getSection(int sy) const { return m_sections[sy]; }
    static int sectionIndexOf(int worldY) { return worldY / Section::HEIGHT; }
    static int sectionLocalY(int worldY) { return worldY % Section::HEIGHT; }

    // 把所有 section 标脏（用于 stitch 之后整体重传）
    void markAllDirty();

    // 非空 section 的 bitmask：bit i 置位表示 section[i] 有可见面。
    // ChunkManager 在每帧 rebuildDrawCommands 前主动 refresh 一次。
    uint32_t getNonEmptyMask() const { return m_nonEmptyMask; }
    void refreshNonEmptyMask() {
        uint32_t m = 0;
        for (int sy = 0; sy < SECTION_COUNT; ++sy) {
            if (!m_sections[sy].isEmpty()) m |= (1u << sy);
        }
        m_nonEmptyMask = m;
    }

    // 渲染层可见性 —— 粗剔（chunk 整体 AABB + 距离），每帧/每相机状态变化时重算并缓存
    bool isChunkPotentiallyVisible(std::shared_ptr<Camera> camera);
    // 渲染层可见性 —— 精剔（section AABB），调用前应先确认 isChunkPotentiallyVisible
    bool isSectionVisible(int sectionY, std::shared_ptr<Camera> camera) const;

    // 给 ChunkManager 调用：把本 chunk 在 face 方向的边界面与 other 缝合（双向）。
    // face: 本 chunk 视角的方向（RIGHT/LEFT/FRONT/BACK 之一）
    void stitchWithNeighbor(Chunk* other, BlockFace faceFromSelf);

    mutable std::mutex m_mutex;

private:
    ChunkManager* m_chunkManager = nullptr;
    glm::ivec2 m_position;
    glm::vec3 m_minPos;
    glm::vec3 m_maxPos;

    std::array<Section, SECTION_COUNT> m_sections;

    bool m_isLoaded = false;
    bool m_meshReady = false;
    uint32_t m_nonEmptyMask = 0;   // bit i = section[i].isEmpty() ? 0 : 1

    // 4 横向邻居（z+/z-/x+/x-，与原 NeighborChunk 顺序一致：0:+X 1:-X 2:+Z 3:-Z）
    std::array<Chunk*, 4> m_neighbors{ {nullptr, nullptr, nullptr, nullptr} };

    // 可见性缓存（chunk 粗剔结果）
    mutable bool m_cachedVisibility = true;
    mutable int m_lastVisibilityFrame = -1;
    glm::vec3 m_cachedCameraPos{ 0.0f };
    float m_cachedCameraFOV = 0.0f;

    // 内部：AABB vs 视锥
    static bool aabbInFrustum(const glm::vec3& min, const glm::vec3& max,
        const std::array<glm::vec4, 6>& planes);

    // 内部
    Chunk* getNeighborChunk(int nx, int nz) const;

    // 给定 chunk 局部 (x,y,z) 处方块，更新它在 face 方向的可见性
    // 会自动跨 section 路由以及跨 chunk 路由邻居
    void updateFaceAt(int x, int y, int z, BlockFace face);

    // 给定 (x,y,z) 处的方块在 face 方向的邻居方块（自动跨 section / 跨 chunk）
    BlockType neighborBlock(int x, int y, int z, BlockFace face) const;

    friend class ChunkManager;
};
