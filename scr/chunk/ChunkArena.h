#pragma once
#include "../core.h"
#include "BlockType.h"
#include <vector>
#include <list>

// GPU 端的"段式"实例缓冲：所有可见 chunk 的 InstanceData 共用一块大 VBO，
// 每个 chunk 占据其中一段 slot（offset, capacity）。
// 只有脏 chunk 才上传自己那段，渲染时通过 glMultiDrawElementsIndirect 一次提交。
class ChunkArena {
public:
    struct Slot {
        // 单位：InstanceData 个数（不是字节）
        uint32_t offset = 0;
        uint32_t capacity = 0;   // 已分配空间（实例数）
        uint32_t count = 0;      // 实际有效实例数 (count <= capacity)
        bool valid() const { return capacity > 0; }
    };

    ChunkArena();
    ~ChunkArena();

    // 创建底层 VBO，预留 initialInstances 个 InstanceData 的空间。
    // 必须在有效 GL 上下文内调用。
    bool initialize(uint32_t initialInstances);

    // 销毁底层 GL 资源
    void shutdown();

    // 申请一段空间。capacity 向上取整到 SLOT_ALIGN 的倍数。
    // 失败返回 invalid slot（capacity == 0）。
    Slot allocate(uint32_t requestedInstances);

    // 释放 slot；slot.capacity 之后归还到 free list
    void free(const Slot& slot);

    // 把 data 上传到 slot 起始处，更新 slot.count
    // 调用方需保证 data.size() <= slot.capacity
    void upload(Slot& slot, const InstanceData* data, uint32_t count);

    // 重新为 chunk 安排空间并上传：如果当前 slot.capacity 够用就原地覆盖；
    // 否则 free 旧 slot 并 allocate 新 slot
    Slot reupload(Slot oldSlot, const InstanceData* data, uint32_t count);

    GLuint getVBO() const { return m_vbo; }
    uint32_t getCapacity() const { return m_capacity; }
    uint32_t getInUse() const { return m_inUse; }   // 已分配出去的总实例数

    // arena 对齐粒度（实例为单位）。64 个面 ≈ 一段 cache 行级别，避免碎片爆炸
    static constexpr uint32_t SLOT_ALIGN = 64;

private:
    // 扩容 VBO 到至少 newCapacity 个实例（保留旧数据）
    bool grow(uint32_t newCapacity);

    GLuint m_vbo = 0;
    uint32_t m_capacity = 0;     // VBO 中可容纳的 InstanceData 总数
    uint32_t m_inUse = 0;        // 当前已分配的实例数

    // free list：(offset, capacity) 升序按 offset
    struct FreeBlock {
        uint32_t offset;
        uint32_t capacity;
    };
    std::list<FreeBlock> m_freeBlocks;
};
