#pragma once
#include "../core.h"
#include "BlockType.h"
#include <vector>
#include <array>

// GPU 端的"段式"实例缓冲：所有 section 的 InstanceData 共用一块大 VBO，
// 每个 section 占据其中一段 slot（offset, capacity）。
//
// 内部用 size-class allocator：把 slot 容量限制在固定的若干档位，
// 每个 size class 维护一个独立 free list。这样：
//  - alloc/free 都是 O(1) 操作；
//  - 已 free 的块永远停留在原 class（不与相邻块合并），外部碎片受控；
//  - section 实例数小幅波动（±50%）时不会触发跨 class 重分配；
//  - 首次分配时按 1.5x 预留，进一步减少 grow 频率。
class ChunkArena {
public:
    struct Slot {
        uint32_t offset = 0;
        uint32_t capacity = 0;     // 实例数；等于某个 size class 的固定值
        uint32_t count = 0;        // 实际有效实例数
        int8_t   sizeClass = -1;   // -1 = invalid；否则索引到 SIZE_CLASSES
        bool valid() const { return capacity > 0 && sizeClass >= 0; }
    };

    ChunkArena();
    ~ChunkArena();

    bool initialize(uint32_t initialInstances);
    void shutdown();

    // 申请一段空间。requestedInstances 为真实需要的实例数；
    // 内部会 oversize 到下一个 size class 提供缓冲。
    Slot allocate(uint32_t requestedInstances);

    void free(const Slot& slot);

    // 上传到 slot。data 长度为 count，必须 <= slot.capacity
    void upload(Slot& slot, const InstanceData* data, uint32_t count);

    // count <= slot.capacity 时原地 upload；否则 free 旧 slot，分配新 slot。
    Slot reupload(Slot oldSlot, const InstanceData* data, uint32_t count);

    GLuint getVBO() const { return m_vbo; }
    uint32_t getCapacity() const { return m_capacity; }
    uint32_t getInUse() const { return m_inUse; }

    // 各 size class 的当前空闲块数量
    int getFreeBlockCount() const;
    uint32_t getLargestFreeBlock() const;
    // 每个 class 的统计（调试用）
    void dumpClassStats(std::ostream& os) const;

    // size class 表（实例数）。从小到大排列，最后一个是单 slot 上限。
    // 选取依据：覆盖空（64）→ 普通地形 section (200~1500) → 复杂(2000~5000) → 极端(>5000)
    static constexpr std::array<uint32_t, 7> SIZE_CLASSES = {
        64, 256, 768, 1536, 3072, 6144, 12288
    };
    static constexpr int CLASS_COUNT = (int)SIZE_CLASSES.size();
    // 超过最大 class 的请求被拒绝（实际不应发生 —— 一个 section 最多 16³*6 = 24576 面，
    // 但最坏情况是棋盘格地形，普通游戏中不会出现）。
    static constexpr uint32_t MAX_SLOT_INSTANCES = 12288;

private:
    bool grow(uint32_t newCapacity);
    static int classFor(uint32_t needed);

    GLuint m_vbo = 0;
    uint32_t m_capacity = 0;       // VBO 总容量（实例）
    uint32_t m_cursor = 0;         // 未切区起点：[m_cursor, m_capacity) 是从未分配过的空间
    uint32_t m_inUse = 0;

    // 每个 size class 一个 free list，存 offset
    std::array<std::vector<uint32_t>, CLASS_COUNT> m_freeLists;
};
