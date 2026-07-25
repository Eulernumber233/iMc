#include "ChunkArena.h"
#include <iostream>
#include <algorithm>
#include <cstring>

ChunkArena::ChunkArena() = default;

ChunkArena::~ChunkArena() {
    shutdown();
}

uint32_t ChunkArena::oversizeTarget(uint32_t needed) {
    uint32_t target = needed + needed / 2;
    if (target > MAX_SLOT_INSTANCES) target = MAX_SLOT_INSTANCES;
    return target;
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
    m_freeIntervals.clear();
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
    m_freeIntervals.clear();
}

ChunkArena::Slot ChunkArena::allocate(uint32_t requestedInstances) {
    Slot slot{};
    if (requestedInstances == 0) return slot;
    if (requestedInstances > MAX_SLOT_INSTANCES) {
        std::cerr << "ChunkArena::allocate: request " << requestedInstances
                  << " exceeds MAX_SLOT_INSTANCES " << MAX_SLOT_INSTANCES << std::endl;
        return slot;
    }

    uint32_t target = oversizeTarget(requestedInstances);

    // ── 1. 区间空闲表 best-fit 搜索 ──
    // 找 >= target 的最小空闲区间；没有则退而求 >= requestedInstances
    auto tryAllocFromFree = [&](uint32_t minSize) -> bool {
        // 线性扫描找 best-fit。区间数量通常很少（< 100），
        // alloc/free 不在热路径上，O(n) 足够。
        auto bestIt = m_freeIntervals.end();
        for (auto it = m_freeIntervals.begin(); it != m_freeIntervals.end(); ++it) {
            if (it->second >= minSize) {
                if (bestIt == m_freeIntervals.end() || it->second < bestIt->second) {
                    bestIt = it;
                    if (it->second == minSize) break; // 精确匹配，已最优
                }
            }
        }
        if (bestIt == m_freeIntervals.end()) return false;

        uint32_t off = bestIt->first;
        uint32_t size = bestIt->second;
        m_freeIntervals.erase(bestIt);

        uint32_t allocSize = (size >= target) ? target : size;
        if (size > allocSize) {
            // 拆分：剩余部分放回空闲表
            m_freeIntervals[off + allocSize] = size - allocSize;
        }

        slot.offset = off;
        slot.capacity = allocSize;
        slot.count = 0;
        m_inUse += allocSize;
        return true;
    };

    // 先尝试 oversize target
    if (tryAllocFromFree(target)) return slot;

    // target 不够时退而求 requestedInstances
    if (target > requestedInstances && tryAllocFromFree(requestedInstances)) return slot;

    // ── 2. 从 cursor 切一块 ──
    if (m_cursor + target > m_capacity) {
        uint32_t newCap = std::max(m_capacity * 2, m_capacity + target);
        if (!grow(newCap)) {
            std::cerr << "ChunkArena: grow failed (cap=" << m_capacity << ", need=" << target << ")\n";
            return Slot{};
        }
    }
    slot.offset = m_cursor;
    slot.capacity = target;
    slot.count = 0;
    m_cursor += target;
    m_inUse += target;
    return slot;
}

void ChunkArena::free(const Slot& slot) {
    if (!slot.valid()) return;

    uint32_t off = slot.offset;
    uint32_t size = slot.capacity;

    // ── 与后继空闲区间合并 ──
    auto next = m_freeIntervals.lower_bound(off);
    if (next != m_freeIntervals.end() && next->first == off + size) {
        size += next->second;
        m_freeIntervals.erase(next);
    }

    // ── 与前驱空闲区间合并 ──
    auto prev = m_freeIntervals.lower_bound(off);
    if (prev != m_freeIntervals.begin()) {
        --prev;
        if (prev->first + prev->second == off) {
            off = prev->first;
            size += prev->second;
            m_freeIntervals.erase(prev);
        }
    }

    m_freeIntervals[off] = size;

    if (m_inUse >= slot.capacity) {
        m_inUse -= slot.capacity;
    } else {
        m_inUse = 0;
    }
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
    for (uint32_t i = 0; i < indexCount; ++i) {
        uint32_t idx = indices[i];
        if (idx >= slot.capacity) continue;
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
        // 已使用的部分（[0, m_cursor)）拷贝过来。
        // 空闲区间表中的 offset 都在 cursor 之前，仍然有效。
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
    return (int)m_freeIntervals.size();
}

uint32_t ChunkArena::getLargestFreeBlock() const {
    uint32_t best = (m_cursor < m_capacity) ? (m_capacity - m_cursor) : 0;
    for (const auto& kv : m_freeIntervals) {
        if (kv.second > best) best = kv.second;
    }
    return best;
}

void ChunkArena::dumpClassStats(std::ostream& os) const {
    os << "Arena free intervals(" << m_freeIntervals.size() << "):";
    int count = 0;
    for (const auto& kv : m_freeIntervals) {
        os << " [" << kv.first << "+" << kv.second << "]";
        if (++count >= 12) { os << " ..."; break; }
    }
    os << " cursor=" << m_cursor << "/" << m_capacity
       << " inUse=" << m_inUse;
}
