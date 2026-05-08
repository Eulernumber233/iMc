#pragma once
#include "../core.h"
#include "BlockType.h"
#include "ChunkDimensions.h"
#include <array>
#include <vector>
#include <unordered_map>

// Section：16x16x16 的子区块。Chunk 持有 4 个 Section 沿 Y 方向堆叠。
// 它是 GPU arena slot 的真正所有者粒度。
//
// 与 Chunk 的关系：
//  - Section 不持有 chunk 坐标；构造时由外部传入它在 chunk 内的 (chunkX, chunkZ, sectionY)。
//    这些信息只用来生成 InstanceData 的"世界坐标"和邻居路由。
//  - Section 内部所有 (x,y,z) 都是 section 局部坐标，y ∈ [0,16)。
class Section {
public:
    static constexpr int WIDTH  = ChunkConstants::CHUNK_WIDTH;
    static constexpr int HEIGHT = ChunkConstants::SECTION_HEIGHT;
    static constexpr int DEPTH  = ChunkConstants::CHUNK_DEPTH;
    static constexpr int VOLUME = WIDTH * HEIGHT * DEPTH;

    Section();

    // 由 Chunk 在构造时初始化坐标
    void setCoords(int chunkX, int chunkZ, int sectionY);

    int getChunkX() const { return m_chunkX; }
    int getChunkZ() const { return m_chunkZ; }
    int getSectionY() const { return m_sectionY; }

    // section 局部坐标
    BlockType getBlock(int x, int y, int z) const;
    void setBlockRaw(int x, int y, int z, BlockType b);

    // 直接拿 raw block buffer 指针，方便 worker 端批量写入
    BlockType* blockData() { return m_blocks.data(); }
    const BlockType* blockData() const { return m_blocks.data(); }

    // mesh
    const std::vector<InstanceData>& getInstanceData() const { return m_instanceData; }
    size_t getInstanceCount() const { return m_instanceData.size(); }

    // 是否完全没有有效面（含全空气、全包裹的实心、纯被邻居挡住三种情况）。
    // 注意：m_instanceData 可能仍含 BLOCK_ERRER 占位，但 m_PosToInstanceIndex 是 ground truth。
    bool isEmpty() const { return m_PosToInstanceIndex.empty(); }

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; }

    // 全量重算：仅在 worker 线程或主线程做完整重建时使用。
    // 不处理跨 section/跨 chunk 边界面 —— 调用方需另行 stitch。
    // 参数 above/below 可为空指针，为空时表示该方向边界对外（视为可见）。
    void rebuildVisibilityInternal(const Section* above, const Section* below);

    // 增量：插入/删除/更新单个面（局部坐标）
    // 把它们设为 public 是为了 Chunk 在做"邻居 section 跨 16-边界面更新"时可以直接调。
    void addFaceLocal(int x, int y, int z, BlockFace face, BlockType type);
    void removeFaceLocal(int x, int y, int z, BlockFace face);

    // 压缩 BLOCK_ERRER 占位面
    void compact();

    // 给定局部坐标 + 面，根据当前 block 状态和给定的"邻居方块"重新计算该面是否可见。
    // 邻居方块 neighborBlock = BLOCK_AIR 表示透空，否则不可见。
    void updateFaceWithNeighbor(int x, int y, int z, BlockFace face, BlockType neighborBlock);

    // 当前内部 errer 计数
    int getErrerCount() const { return m_errerCount; }

    // 接管另一个 Section 的数据（move 语义，用于把 worker 生产的 result 装入主线程 chunk）
    void adoptFrom(Section&& other);

private:
    std::array<BlockType, VOLUME> m_blocks{};
    std::vector<InstanceData> m_instanceData;
    std::unordered_map<BlockFaceLocKey, int> m_PosToInstanceIndex;
    int m_errerCount = 0;
    bool m_dirty = true;

    // section 在世界中的位置信息（由 Chunk 设置）
    int m_chunkX = 0;
    int m_chunkZ = 0;
    int m_sectionY = 0;   // 0..3，section 起始 worldY = m_sectionY * HEIGHT

    // 内部辅助
    static int idx(int x, int y, int z) { return (y * DEPTH + z) * WIDTH + x; }
    glm::vec3 worldCenterFor(int x, int y, int z) const;
};
