#pragma once
#include "../core.h"
#include "BlockType.h"
#include <vector>
#include <map>
#include <cstdint>
#include <ostream>

// GPU 端的"段式"实例缓冲：所有 section 的 InstanceData 共用一块大 VBO，
// 每个 section 占据其中一段 slot（offset, capacity）。
//
// 使用区间空闲表（std::map<offset, size>）管理空闲块：
//  - allocate: best-fit 搜索，大块自动拆分为小块
//  - free: 自动与相邻空闲块合并，避免碎片化
//  - 首次分配时按 1.5x 预留，减少 grow 频率
class ChunkArena {
public:
    struct Slot {
        uint32_t offset = 0;
        uint32_t capacity = 0;     // 实例数
        uint32_t count = 0;        // 实际有效实例数
        bool valid() const { return capacity > 0; }
    };

    ChunkArena();
    ~ChunkArena();

    bool initialize(uint32_t initialInstances);
    void shutdown();

    // 申请一段空间。requestedInstances 为真实需要的实例数；
    // 内部会 oversize 1.5x 提供缓冲。
    Slot allocate(uint32_t requestedInstances);

    void free(const Slot& slot);

    // 上传到 slot。data 长度为 count，必须 <= slot.capacity
    void upload(Slot& slot, const InstanceData* data, uint32_t count);

    // count <= slot.capacity 时原地 upload；否则 free 旧 slot，分配新 slot。
    Slot reupload(Slot oldSlot, const InstanceData* data, uint32_t count);

    // 增量 patch：对 slot 内若干位置做小段更新。
    // - data 指向完整 instance 数组（长度 ≥ max(indices)+1，但实际只读 indices 指定的位置）
    // - indices 是相对 slot 内（0-based）的下标列表
    // - newCount 表示更新后 slot 的有效实例数（必须 <= slot.capacity）
    //
    // 实现策略：单次 glMapBufferRange 覆盖 [minIdx, maxIdx] 范围，
    // 配 GL_MAP_UNSYNCHRONIZED_BIT，跳过驱动同步等待。
    // 调用方需保证：上一帧 GPU 已经画完这些位置（每帧帧首调用即可满足）。
    void patch(Slot& slot, const InstanceData* data,
               const uint32_t* indices, uint32_t indexCount,
               uint32_t newCount);

    GLuint getVBO() const { return m_vbo; }
    uint32_t getCapacity() const { return m_capacity; }
    uint32_t getInUse() const { return m_inUse; }

    // 调试统计
    int getFreeBlockCount() const;
    uint32_t getLargestFreeBlock() const;
    void dumpClassStats(std::ostream& os) const;

    // 超出此值的请求被拒绝（实际不应发生 —— 一个 section 最多 16³*6 = 24576 面，
    // 但最坏情况是棋盘格地形，普通游戏中不会出现）。
    static constexpr uint32_t MAX_SLOT_INSTANCES = 12288;

private:
    // VBO 总容量不足时扩容。
    bool grow(uint32_t newCapacity);

    // oversize 1.5x，上限 MAX_SLOT_INSTANCES
    static uint32_t oversizeTarget(uint32_t needed);

    GLuint m_vbo = 0;
    uint32_t m_capacity = 0;       // VBO 总容量（实例）
    uint32_t m_cursor = 0;         // 未切区起点：[m_cursor, m_capacity) 是从未分配过的空间
    uint32_t m_inUse = 0;          // 已分配的总容量

    // 空闲区间表：offset → size，按 offset 排序
    // allocate: best-fit（选 >= 需求的最小区间），大块拆分
    // free: 插入后自动与前后相邻空闲区间合并
    std::map<uint32_t, uint32_t> m_freeIntervals;
};
