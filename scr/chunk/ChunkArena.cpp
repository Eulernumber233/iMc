#include "ChunkArena.h"
#include <iostream>
#include <algorithm>
#include <ostream>
#include <cstring>

ChunkArena::ChunkArena() = default;

ChunkArena::~ChunkArena() {
    shutdown();
}

int ChunkArena::classFor(uint32_t needed) {
    // oversize 1.5x，再选最小的够用 class
    uint32_t target = needed + needed / 2;
    for (int i = 0; i < CLASS_COUNT; ++i) {
        if (SIZE_CLASSES[i] >= target) return i;
    }
    return -1;   // 超过最大 class
}

bool ChunkArena::initialize(uint32_t initialInstances) {
    if (m_vbo != 0) return true;
    if (initialInstances == 0) initialInstances = 1u << 16;

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(initialInstances) * sizeof(InstanceData),
        nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_capacity = initialInstances;
    m_cursor = 0;
    m_inUse = 0;
    for (auto& fl : m_freeLists) fl.clear();
    return true;
}

void ChunkArena::shutdown() {
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    m_capacity = 0;
    m_cursor = 0;
    m_inUse = 0;
    for (auto& fl : m_freeLists) fl.clear();
}

//
// TODO 还需要优化
// 1. 目前 free list 是 vector，pop_back/push_back；如果频繁分配/释放，可能导致内存碎片化。
// 2. 目前没有合并相邻空闲块的逻辑，可能导致大量小块空闲，无法满足大块请求。
// 3. 可以考虑使用更复杂的数据结构（如 std::set 或自定义的 free list）来管理空闲块，支持合并和按大小查找。
// 4. 可以考虑在 allocate 时，如果 free list 没有合适的块，尝试从 larger class 的 free list 中拆分一个块。
// 5. 可以考虑在 free 时，如果相邻的块也空闲，尝试合并成一个更大的块。
// 6. 没有VBO缩容的逻辑，可能导致VBO占用过大内存。可以考虑在一定条件下缩小VBO容量。
ChunkArena::Slot ChunkArena::allocate(uint32_t requestedInstances) {
    Slot slot{};
    if (requestedInstances == 0) return slot;

    int c = classFor(requestedInstances);
    if (c < 0) {
        std::cerr << "ChunkArena::allocate: request " << requestedInstances
                  << " exceeds MAX_SLOT_INSTANCES " << MAX_SLOT_INSTANCES << std::endl;
        return slot;
    }
    uint32_t cls = SIZE_CLASSES[c];

    // 1. 优先从对应 class 的 free list 取
    auto& fl = m_freeLists[c];
    if (!fl.empty()) {
        slot.offset = fl.back();
        fl.pop_back();
        slot.capacity = cls;
        slot.count = 0;
        slot.sizeClass = (int8_t)c;
        m_inUse += cls;
        return slot;
    }

    // 2. 从未切区 cursor 切一块
    if (m_cursor + cls > m_capacity) {
        // grow 到至少能容下当前请求的两倍
        uint32_t newCap = std::max(m_capacity * 2, m_capacity + cls);
        if (!grow(newCap)) {
            std::cerr << "ChunkArena: grow failed (cap=" << m_capacity << ", need=" << cls << ")\n";
            return Slot{};
        }
    }
    slot.offset = m_cursor;
    slot.capacity = cls;
    slot.count = 0;
    slot.sizeClass = (int8_t)c;
    m_cursor += cls;
    m_inUse += cls;
    return slot;
}

void ChunkArena::free(const Slot& slot) {
    if (!slot.valid()) return;
    if (slot.sizeClass < 0 || slot.sizeClass >= CLASS_COUNT) 
        return;
    
    m_freeLists[slot.sizeClass].push_back(slot.offset);
    if (m_inUse >= slot.capacity) {
        m_inUse -= slot.capacity;
    } 
    else m_inUse = 0;
}

void ChunkArena::upload(Slot& slot, const InstanceData* data, uint32_t count) {
    if (!slot.valid() || count == 0) {
        slot.count = 0;
        return;
    }
    if (count > slot.capacity) {
        std::cerr << "ChunkArena::upload count > capacity (" << count << " > " << slot.capacity << ")\n";
        count = slot.capacity;
    }
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,
        GLintptr(slot.offset) * sizeof(InstanceData),
        GLsizeiptr(count) * sizeof(InstanceData),
        data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    slot.count = count;
}

void ChunkArena::patch(Slot& slot, const InstanceData* data,
                       const uint32_t* indices, uint32_t indexCount,
                       uint32_t newCount) {
    if (!slot.valid() || indexCount == 0 || data == nullptr || indices == nullptr) return;
    if (newCount > slot.capacity) {
        // 容量不够 —— 调用方应该在上层 fallback 到全量 reupload，不该走到这里
        std::cerr << "ChunkArena::patch newCount " << newCount
                  << " > capacity " << slot.capacity << std::endl;
        return;
    }

    // 找出 dirty index 的 [minIdx, maxIdx]，map 这段范围一次写完
    uint32_t minIdx = indices[0];
    uint32_t maxIdx = indices[0];
    for (uint32_t i = 1; i < indexCount; ++i) {
        uint32_t v = indices[i];
        if (v < minIdx) minIdx = v;
        if (v > maxIdx) maxIdx = v;
    }
    if (maxIdx >= slot.capacity) {
        std::cerr << "ChunkArena::patch index " << maxIdx
                  << " >= capacity " << slot.capacity << std::endl;
        return;
    }

    GLintptr off = (GLintptr(slot.offset) + GLintptr(minIdx)) * (GLintptr)sizeof(InstanceData);
    GLsizeiptr len = (GLsizeiptr)(maxIdx - minIdx + 1) * (GLsizeiptr)sizeof(InstanceData);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    // 不带 INVALIDATE_RANGE：未触及的位置保留旧数据；只写 dirty index 处的 8 字节。
    // UNSYNCHRONIZED：跳过驱动的隐式同步等待，调用方需保证 GPU 当前不在读这段
    // （ChunkManager::update 在帧首调用，上一帧已经画完，安全）。
    void* ptr = glMapBufferRange(GL_ARRAY_BUFFER, off, len,
        GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
    if (!ptr) {
        std::cerr << "ChunkArena::patch glMapBufferRange failed (off=" << off
                  << " len=" << len << ")\n";
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        return;
    }

    InstanceData* dst = reinterpret_cast<InstanceData*>(ptr);
    // 仅写 dirty index 对应的位置；映射的相对偏移 = idx - minIdx
    for (uint32_t i = 0; i < indexCount; ++i) {
        uint32_t idx = indices[i];
        if (idx >= slot.capacity) continue;          // 越界防御
        dst[idx - minIdx] = data[idx];
    }

    glUnmapBuffer(GL_ARRAY_BUFFER);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    slot.count = newCount;
}

ChunkArena::Slot ChunkArena::reupload(Slot oldSlot, const InstanceData* data, uint32_t count) {
    if (oldSlot.valid() && count <= oldSlot.capacity) {
        upload(oldSlot, data, count);
        return oldSlot;
    }
    if (oldSlot.valid()) free(oldSlot);
    Slot s = allocate(count);
    if (s.valid()) upload(s, data, count);
    return s;
}

bool ChunkArena::grow(uint32_t newCapacity) {
    if (newCapacity <= m_capacity) return true;

    GLuint newVBO = 0;
    glGenBuffers(1, &newVBO);
    glBindBuffer(GL_ARRAY_BUFFER, newVBO);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(newCapacity) * sizeof(InstanceData),
        nullptr, GL_DYNAMIC_DRAW);

    if (m_vbo && m_capacity > 0) {
        // 已使用的部分（[0, m_cursor)）拷贝过来。free list 的 offset 在 cursor 之前都还有效。
        glBindBuffer(GL_COPY_READ_BUFFER, m_vbo);
        glBindBuffer(GL_COPY_WRITE_BUFFER, newVBO);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
            0, 0, GLsizeiptr(m_capacity) * sizeof(InstanceData));
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        glDeleteBuffers(1, &m_vbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_vbo = newVBO;
    m_capacity = newCapacity;
    return true;
}

int ChunkArena::getFreeBlockCount() const {
    int total = 0;
    for (const auto& fl : m_freeLists) total += (int)fl.size();
    return total;
}

uint32_t ChunkArena::getLargestFreeBlock() const {
    // 最大空闲块 = 最大有空闲条目的 class 的 size，或 cursor 之后的连续空白
    uint32_t best = (m_cursor < m_capacity) ? (m_capacity - m_cursor) : 0;
    for (int i = CLASS_COUNT - 1; i >= 0; --i) {
        if (!m_freeLists[i].empty()) {
            best = std::max(best, SIZE_CLASSES[i]);
            break;   // 只看最大的有空闲的 class
        }
    }
    return best;
}

void ChunkArena::dumpClassStats(std::ostream& os) const {
    os << "Arena classes:";
    for (int i = 0; i < CLASS_COUNT; ++i) {
        os << " [" << SIZE_CLASSES[i] << "]=" << m_freeLists[i].size();
    }
    os << " cursor=" << m_cursor << "/" << m_capacity;
}
