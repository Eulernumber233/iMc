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

    // 状态
    bool isLoaded() const { return m_isLoaded; }
    bool isMeshReady() const { return m_meshReady; }

    glm::ivec2 getPosition() const { return m_position; }
    glm::vec3 getCenter() const;
    const glm::vec3& getMinPos() const { return m_minPos; }
    const glm::vec3& getMaxPos() const { return m_maxPos; }

    // 全局（chunk 局部）坐标，y ∈ [0, HEIGHT)
    // 接口对外只用 BlockState；调用方需要类型时取 state.type()。
    BlockState getBlock(int x, int y, int z) const;
    void       setBlock(int x, int y, int z, BlockState s);
    // 放置方块。状态里的 orient 直接落到底层 Section，并参与可见面生成。
    // 返回值：放置前该格的 state（调用方可以用 .type() 比较 BLOCK_AIR）。
    BlockState setBlockAndUpdate(int x, int y, int z, BlockState newState);

    // section 访问
    Section& getSection(int sy) { return m_sections[sy]; }
    const Section& getSection(int sy) const { return m_sections[sy]; }
    static int sectionIndexOf(int worldY) { return worldY / Section::HEIGHT; }
    static int sectionLocalY(int worldY) { return worldY % Section::HEIGHT; }

    // 把所有 section 标脏（用于 stitch 之后整体重传）
    void markAllDirty();

    // 脏 section bitmask：bit i 置位表示 section[i] 可能有未上传的 mesh 改动。
    // 这是"保守超集"——只多不少：每个会让 section 变脏的 Chunk 层入口都置位，
    // ChunkManager::uploadPass 据此跳过整块干净 chunk（一次位运算），避免每帧
    // 对全部 active chunk×16 section 做 isDirty() 空检查。置位多一位最多多一次
    // isDirty() 检查（无害），漏置位才是 bug，故宁可多置。上传成功后由 ChunkManager
    // 调 clearSectionDirtyBit 清位。
    uint32_t getDirtySectionMask() const { return m_dirtySectionMask; }
    void markSectionDirty(int sy) {
        if (sy >= 0 && sy < SECTION_COUNT) m_dirtySectionMask |= (1u << sy);
    }
    void markAllSectionsDirtyMask() { m_dirtySectionMask = (SECTION_COUNT >= 32)
        ? 0xFFFFFFFFu : ((1u << SECTION_COUNT) - 1u); }
    void clearSectionDirtyBit(int sy) {
        if (sy >= 0 && sy < SECTION_COUNT) m_dirtySectionMask &= ~(1u << sy);
    }

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
    bool isChunkPotentiallyVisible(const Camera* camera);
    // 渲染层可见性 —— 精剔（section AABB），调用前应先确认 isChunkPotentiallyVisible
    bool isSectionVisible(int sectionY, const Camera* camera) const;

    // 组合剔除：非空 mask + 视锥 + 距离 + 纵向（下方 section 限制）。
    // frustumPlanes 可选：传入则复用，nullptr 则内部自取。
    // detailed=true 时输出 vis.* 细分计数（每 section 4 次 addCounter），默认关以省开销。
    uint32_t getVisibleSectionMask(const Camera* camera,
                                    int cameraSectionY, int maxDownSections,
                                    const std::array<glm::vec4, 6>* frustumPlanes = nullptr,
                                    bool detailed = false) const;

    // 区块数据是否被玩家修改过（新建 / 放置 / 破坏 → true，写入磁盘后 → false）
    bool isSaveDirty() const { return m_saveDirty; }
    void markSaveDirty() { m_saveDirty = true; }
    void clearSaveDirty() { m_saveDirty = false; }

    // 取某个 section 的 BlockBox（数据 + 锁）。供 ChunkManager 投递 Task 2 时
    // 把邻居各 section 的 box 共享给 worker（worker 持读锁读边界）。
    const std::shared_ptr<BlockBox>& getSectionBox(int sy) const {
        return m_sections[sy].getBox();
    }

private:
    ChunkManager* m_chunkManager = nullptr;
    glm::ivec2 m_position;
    glm::vec3 m_minPos;
    glm::vec3 m_maxPos;

    std::array<Section, SECTION_COUNT> m_sections;

    bool m_isLoaded = false;
    bool m_meshReady = false;
    uint32_t m_nonEmptyMask = 0;   // bit i = section[i].isEmpty() ? 0 : 1
    uint32_t m_dirtySectionMask = 0; // bit i = section[i] 可能有未上传改动（保守超集）

    // 4 横向邻居（z+/z-/x+/x-，与原 NeighborChunk 顺序一致：0:+X 1:-X 2:+Z 3:-Z）
    std::array<Chunk*, 4> m_neighbors{ {nullptr, nullptr, nullptr, nullptr} };

    bool m_saveDirty = true; // 新生成/修改过的区块需要存档；写入磁盘后清零

    // 可见性缓存（chunk 粗剔结果）。用 ChunkManager 的 visGeneration 做版本号，
    // 代替每 chunk 各自 glm::distance 比较相机是否移动。0 表示首次需计算。
    mutable bool m_cachedVisibility = true;
    mutable uint32_t m_cachedVisGeneration = 0;

    // 内部：AABB vs 视锥
    static bool aabbInFrustum(const glm::vec3& min, const glm::vec3& max,
        const std::array<glm::vec4, 6>& planes);

    // 内部
    Chunk* getNeighborChunk(int nx, int nz) const;

    // 给定 chunk 局部 (x,y,z) 处方块，更新它在 face 方向的可见性
    // 会自动跨 section 路由以及跨 chunk 路由邻居
    void updateFaceAt(int x, int y, int z, BlockFace face);

    // 给定 (x,y,z) 处的方块在 face 方向的邻居方块（自动跨 section / 跨 chunk）。
    // 返回完整 BlockState；判可见性时调用方拿 .type() 比较 BLOCK_AIR。
    BlockState neighborBlock(int x, int y, int z, BlockFace face) const;

    friend class ChunkManager;
};
