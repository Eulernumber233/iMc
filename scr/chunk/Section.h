#pragma once
#include "../core.h"
#include "BlockType.h"
#include "BlockBox.h"
#include "ChunkArena.h"
#include "ChunkDimensions.h"
#include "../light/LightCache.h"  // SectionLightData
#include <array>
#include <memory>
#include <vector>
#include <unordered_map>
#include <shared_mutex>

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

    // 禁止拷贝（拷贝会让两个 Section 共享同一个 BlockBox，语义错误）；保留 move。
    // 原来 m_blocks 是 unique_ptr 时拷贝本就被隐式删除；换成 shared_ptr 后需显式删除以维持该语义。
    Section(const Section&) = delete;
    Section& operator=(const Section&) = delete;
    Section(Section&&) noexcept = default;
    Section& operator=(Section&&) noexcept = default;

    // 由 Chunk 在构造时初始化坐标
    void setCoords(int chunkX, int chunkZ, int sectionY);

    int getChunkX() const { return m_chunkX; }
    int getChunkZ() const { return m_chunkZ; }
    int getSectionY() const { return m_sectionY; }

    // section 局部坐标。整套对外接口统一用 BlockState；
    // 调用方需要类型时拿 state.type() 自己取，需要朝向时拿 state.orient()。
    // 越界返回 BlockState{} == (AIR, orient=0)。
    BlockState getBlock(int x, int y, int z) const;
    void       setBlock(int x, int y, int z, BlockState s);

    // 直接拿 raw state buffer 指针，方便 worker 端批量写入（地形生成器写入这里）。
    // 注意：不加锁。仅在「Section 尚未对外可见」的阶段（worker 构建、装载前）使用。
    BlockState* stateData() { return m_box->blocks.data(); }
    const BlockState* stateData() const { return m_box->blocks.data(); }

    // ---- BlockBox 共享数据源 ----
    // Section 不再独占一份方块数组，而是持有一个 shared_ptr<BlockBox>（数据 + 读写锁）。
    // 该 box 由 Task 1 产出，Task 2 把它直接共享给 Section（零拷贝）。
    const std::shared_ptr<BlockBox>& getBox() const { return m_box; }
    void setBox(std::shared_ptr<BlockBox> box) { m_box = std::move(box); }

    // mesh
    const std::vector<InstanceData>& getInstanceData() const { return m_instanceData; }
    size_t getInstanceCount() const { return m_instanceData.size(); }

    // 是否完全没有有效面（含全空气、全包裹的实心、纯被邻居挡住三种情况）。
    bool isEmpty() const { return m_PosToInstanceIndex.empty(); }

    bool isDirty() const { return m_dirty; }
    void markDirty() { m_dirty = true; }
    void clearDirty() { m_dirty = false; m_dirtyIndices.clear(); m_fullRebuildPending = false; }

    // 增量上传支持
    const std::vector<uint32_t>& getDirtyIndices() const { return m_dirtyIndices; }
    // 是否要求走全量上传（rebuild / compact / adopt 这类大幅替换语义）。
    // 平常的 add / remove 都走增量，capacity 是否够由 ChunkManager 比对 newCount vs slot.capacity 决定。
    bool fullRebuildPending() const { return m_fullRebuildPending; }

    // 全量重算：仅在 worker 线程或主线程做完整重建时使用。
    // 不处理跨 section/跨 chunk 边界面 —— 调用方需另行 stitch。
    // 参数 above/below 可为空指针，为空时表示该方向边界对外（视为可见）。
    void rebuildVisibilityInternal(const Section* above, const Section* below);

    // 增量：插入/删除/更新单个面（局部坐标）
    // 把它们设为 public 是为了 Chunk 在做"邻居 section 跨 16-边界面更新"时可以直接调。
    // state 给定该格当前的 BlockState（type + orient）；如果调用方手头没有，可以传
    // getBlock(x,y,z) 重新读一遍。
    void addFaceLocal(int x, int y, int z, BlockFace face, BlockState state);
    void removeFaceLocal(int x, int y, int z, BlockFace face);

    // 压缩 BLOCK_ERRER 占位面
    void compact();

    // 给定局部坐标 + 面，根据当前 block 状态和给定的"邻居方块"重新计算该面是否可见。
    // 邻居方块 neighbor.type() == BLOCK_AIR 表示透空，否则不可见。
    void updateFaceWithNeighbor(int x, int y, int z, BlockFace face, BlockState neighbor);

    // 当前内部 errer 计数
    int getErrerCount() const { return m_errerCount; }

    // 接管另一个 Section 的数据（move 语义，用于把 worker 生产的 result 装入主线程 chunk）
    void adoptFrom(Section&& other);

    // GPU slot 已被 ChunkManager 释放：抹掉所有增量状态，下次进入活跃半径时强制走全量上传。
    // 不动 m_box / m_instanceData / m_PosToInstanceIndex —— 这些是 CPU 端方块数据与 mesh，仍然有效。
    void notifyGpuSlotReleased();

    // ── 光照数据（与 m_box 平行的独立数据源）──────────────────────
    // 由 Task 2 产出，loadMeshResult 中通过 setLightData 写入 Section。
    // GPU 上传时 ChunkManager 直接从此读取。
    const std::shared_ptr<SectionLightData>& getLightData() const { return m_lightData; }
    void setLightData(std::shared_ptr<SectionLightData> data) {
        m_lightData = std::move(data);
        m_lightDirty = true;
    }
    bool isLightDirty() const { return m_lightDirty; }
    void clearLightDirty() { m_lightDirty = false; }
    bool hasLightData() const { return m_lightData != nullptr; }

    // ── 光源位置数据（与 m_box 平行的独立数据源）──────────────────
    // 由 Task 2 拆分 chunk 级光源列表产出，loadMeshResult 中写入 Section。
    // 格式：uint16_t 压缩 chunk 内坐标（x:4, y:8, z:4），见 LightSource.h。
    // 增删通过 addLightSource / removeLightSource（swap-and-pop，O(1) 删除）。
    const std::shared_ptr<std::vector<uint16_t>>& getLightSources() const { return m_lightSources; }
    void setLightSources(std::shared_ptr<std::vector<uint16_t>> sources) {
        m_lightSources = std::move(sources);
    }
    void addLightSource(uint16_t packed) {
        if (!m_lightSources) m_lightSources = std::make_shared<std::vector<uint16_t>>();
        m_lightSources->push_back(packed);
    }
    void removeLightSource(uint16_t packed) {
        if (!m_lightSources || m_lightSources->empty()) return;
        auto& v = *m_lightSources;
        auto it = std::find(v.begin(), v.end(), packed);
        if (it != v.end()) {
            *it = v.back();
            v.pop_back();
        }
    }
    bool hasLightSources() const { return m_lightSources && !m_lightSources->empty(); }

    // 网络序列化：读取/写入整个 section 的 BlockState 数据
    void readAllBlocks(std::vector<BlockState>& out) const;
    void writeAllBlocks(const std::vector<BlockState>& data);

    // 缓存 GPU arena slot，避免 rebuildDrawCommands 每帧查 unordered_map
    ChunkArena::Slot getGpuSlot() const { return m_gpuSlot; }
    void setGpuSlot(const ChunkArena::Slot& s) { m_gpuSlot = s; }

private:
    // section 方块数据 + 读写锁，打包在 BlockBox 里，全程只经 shared_ptr 共享（见 BlockBox.h）。
    // Section() 构造时新建一个；adoptFrom / setBox 时换指针（锁随数据一起转移，但锁对象本身原地不动）。
    // 玩家修改持 m_box->mutex 写锁；worker 读邻居边界持读锁；主线程内部串行读不加锁。
    std::shared_ptr<BlockBox> m_box;
    std::vector<InstanceData> m_instanceData;
    std::unordered_map<BlockFaceLocKey, int> m_PosToInstanceIndex;
    int m_errerCount = 0;
    bool m_dirty = true;

    // 增量上传辅助：
    //   m_dirtyIndices: 自上次 upload 起被修改/新增的 instance 在数组中的下标
    //   m_freeSlots:    被 remove 留下的 ERRER 占位槽 free list；下次 add 优先复用这些槽，避免数组无限增长
    //   m_fullRebuildPending: rebuild / compact / adopt 这类大幅替换 → 强制走全量
    // 注意：add/remove 一律走增量。slot.capacity 不够时由 ChunkManager 在上传前判定并 fallback 到全量。
    std::vector<uint32_t> m_dirtyIndices;
    std::vector<uint32_t> m_freeSlots;
    bool m_fullRebuildPending = false;

    // section 在世界中的位置信息（由 Chunk 设置）
    int m_chunkX = 0;
    int m_chunkZ = 0;
    int m_sectionY = 0;   // 0..3，section 起始 worldY = m_sectionY * HEIGHT

    // GPU arena slot 缓存：rebuildDrawCommands 直接读此字段，无需查 unordered_map。
    ChunkArena::Slot m_gpuSlot;

    // 光照数据（shared_ptr 共享所有权，与 BlockBox 模式一致）
    std::shared_ptr<SectionLightData> m_lightData;
    
    // 光源数据（shared_ptr 共享所有权，与 BlockBox 模式一致），用于增量光照传播。
    std::shared_ptr<std::vector<uint16_t>> m_lightSources;
    bool m_lightDirty = true;

    // 内部辅助
    static int idx(int x, int y, int z) { return (y * DEPTH + z) * WIDTH + x; }
};
