#include "ChunkArena.h"
#include <iostream>
#include <algorithm>
#include <cstring>

ChunkArena::ChunkArena() = default;

ChunkArena::~ChunkArena() {
    shutdown();
}

static uint32_t alignUp(uint32_t v, uint32_t a) {
    return (v + a - 1) / a * a;
}

bool ChunkArena::initialize(uint32_t initialInstances) {
    if (m_vbo != 0) return true;
    if (initialInstances == 0) initialInstances = 1 << 16;
    initialInstances = alignUp(initialInstances, SLOT_ALIGN);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(initialInstances) * sizeof(InstanceData),
        nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_capacity = initialInstances;
    m_inUse = 0;
    m_freeBlocks.clear();
    m_freeBlocks.push_back({ 0, m_capacity });
    return true;
}

void ChunkArena::shutdown() {
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    m_capacity = 0;
    m_inUse = 0;
    m_freeBlocks.clear();
}

ChunkArena::Slot ChunkArena::allocate(uint32_t requestedInstances) {
    Slot slot{};
    if (requestedInstances == 0) return slot;

    uint32_t need = alignUp(requestedInstances, SLOT_ALIGN);

    // first-fit
    for (auto it = m_freeBlocks.begin(); it != m_freeBlocks.end(); ++it) {
        if (it->capacity >= need) {
            slot.offset = it->offset;
            slot.capacity = need;
            slot.count = 0;

            it->offset += need;
            it->capacity -= need;
            if (it->capacity == 0) m_freeBlocks.erase(it);

            m_inUse += need;
            return slot;
        }
    }

    // 没有合适块 —— 扩容到 max(2*cap, cap+need)
    uint32_t newCap = std::max(m_capacity * 2, m_capacity + need);
    if (!grow(newCap)) {
        std::cerr << "ChunkArena: grow failed (cap=" << m_capacity << ", need=" << need << ")\n";
        return Slot{};
    }
    return allocate(requestedInstances); // grow 后重试
}

void ChunkArena::free(const Slot& slot) {
    if (!slot.valid()) return;

    FreeBlock fb{ slot.offset, slot.capacity };
    m_inUse = (m_inUse >= slot.capacity) ? (m_inUse - slot.capacity) : 0;

    // 按 offset 升序插入并尝试合并相邻块
    auto it = m_freeBlocks.begin();
    while (it != m_freeBlocks.end() && it->offset < fb.offset) ++it;
    auto inserted = m_freeBlocks.insert(it, fb);

    // 与后继合并
    auto next = std::next(inserted);
    if (next != m_freeBlocks.end() && inserted->offset + inserted->capacity == next->offset) {
        inserted->capacity += next->capacity;
        m_freeBlocks.erase(next);
    }
    // 与前驱合并
    if (inserted != m_freeBlocks.begin()) {
        auto prev = std::prev(inserted);
        if (prev->offset + prev->capacity == inserted->offset) {
            prev->capacity += inserted->capacity;
            m_freeBlocks.erase(inserted);
        }
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

uint32_t ChunkArena::getLargestFreeBlock() const {
    uint32_t best = 0;
    for (const auto& fb : m_freeBlocks) if (fb.capacity > best) best = fb.capacity;
    return best;
}

ChunkArena::Slot ChunkArena::reupload(Slot oldSlot, const InstanceData* data, uint32_t count) {
    // 容量足够就原地覆盖
    if (oldSlot.valid() && count <= oldSlot.capacity) {
        upload(oldSlot, data, count);
        return oldSlot;
    }
    // 否则换一段
    if (oldSlot.valid()) free(oldSlot);
    Slot s = allocate(count);
    if (s.valid()) upload(s, data, count);
    return s;
}

bool ChunkArena::grow(uint32_t newCapacity) {
    newCapacity = alignUp(newCapacity, SLOT_ALIGN);
    if (newCapacity <= m_capacity) return true;

    GLuint newVBO = 0;
    glGenBuffers(1, &newVBO);
    glBindBuffer(GL_ARRAY_BUFFER, newVBO);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(newCapacity) * sizeof(InstanceData),
        nullptr, GL_DYNAMIC_DRAW);

    if (m_vbo && m_capacity > 0) {
        // 把旧 VBO 内容拷贝过来，所有现存 slot 的 offset 保持不变
        glBindBuffer(GL_COPY_READ_BUFFER, m_vbo);
        glBindBuffer(GL_COPY_WRITE_BUFFER, newVBO);
        glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
            0, 0, GLsizeiptr(m_capacity) * sizeof(InstanceData));
        glBindBuffer(GL_COPY_READ_BUFFER, 0);
        glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
        glDeleteBuffers(1, &m_vbo);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // 把多出来的尾部加进 free list（与最后一个空闲块合并）
    uint32_t tailOffset = m_capacity;
    uint32_t tailSize = newCapacity - m_capacity;

    m_vbo = newVBO;
    m_capacity = newCapacity;

    if (!m_freeBlocks.empty()) {
        auto& back = m_freeBlocks.back();
        if (back.offset + back.capacity == tailOffset) {
            back.capacity += tailSize;
            return true;
        }
    }
    m_freeBlocks.push_back({ tailOffset, tailSize });
    return true;
}
