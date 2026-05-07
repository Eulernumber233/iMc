#include "ChunkManager.h"
#include <iostream>
#include <algorithm>
#include "Chunk.h"

ChunkManager::ChunkManager(unsigned int seed)
    : m_renderRadius(8) {
	generator = std::make_shared<TerrainGenerator>();
	generator->setSeed(seed);
}

ChunkManager::~ChunkManager() {
    // 清理所有区块
    m_loadedChunks.clear();
    m_visibleChunks.clear();
    m_chunkSlots.clear();
    m_drawCommands.clear();

    if (m_indirectBuffer) {
        glDeleteBuffers(1, &m_indirectBuffer);
        m_indirectBuffer = 0;
    }
    m_arena.shutdown();
}

void ChunkManager::initialize(int renderRadius, const glm::vec3& cameraPos) {
    m_renderRadius = renderRadius;
    m_currentCenterChunk = glm::ivec2(0);

    // 初始 arena 容量按典型可见 chunk 数预估（(2R+1)^2 * 平均面数）
    const uint32_t initialInstances = std::max<uint32_t>(1u << 16,
        (uint32_t)((2 * renderRadius + 1) * (2 * renderRadius + 1) * 1024));
    m_arena.initialize(initialInstances);

    glGenBuffers(1, &m_indirectBuffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
    m_indirectBufferCapacityBytes = sizeof(DrawElementsIndirectCommand) * 256;
    glBufferData(GL_DRAW_INDIRECT_BUFFER, m_indirectBufferCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    updateVisibleChunks(cameraPos);
}

void ChunkManager::update(std::shared_ptr<Camera> camera) {
    m_camera = camera;
    glm::ivec2 cameraChunk(
        static_cast<int>(std::floor(m_camera->Position.x / Chunk::WIDTH)),
        static_cast<int>(std::floor(m_camera->Position.z / Chunk::DEPTH))
    );

    if (cameraChunk != m_currentCenterChunk) {
        m_currentCenterChunk = cameraChunk;
        updateVisibleChunks(m_camera->Position);
    }

    int loadsThisFrame = 0;
    while (!m_loadQueue.empty() && loadsThisFrame < MAX_LOADS_PER_FRAME) {
        glm::ivec2 chunkPos = m_loadQueue.front();
        m_loadQueue.pop();

        loadChunk(chunkPos);
        loadsThisFrame++;
    }

    rebuildDrawCommands();
}

Chunk* ChunkManager::getChunk(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    auto it = m_loadedChunks.find(key);
    if (it != m_loadedChunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

Chunk* ChunkManager::getChunk(int x, int z){
    glm::ivec2 a = { x, z };
    return getChunk(a);
}

Chunk* ChunkManager::getChunkAtWorld(const glm::vec3& worldPos) {
    glm::ivec2 chunkPos(
        static_cast<int>(std::floor(worldPos.x / Chunk::WIDTH)),
        static_cast<int>(std::floor(worldPos.z / Chunk::DEPTH))
    );
    return getChunk(chunkPos);
}

std::vector<glm::ivec2> ChunkManager::getVisibleChunkPositions()const
{
    std::vector<glm::ivec2>ret;
    for(auto chunk : m_visibleChunks){
        ret.push_back(chunk->getPosition());
	}
    return ret;
}

void ChunkManager::setRenderRadius(int radius) {
    if (radius > 0 && radius != m_renderRadius) {
        m_renderRadius = radius;
        updateVisibleChunks(glm::vec3(
            m_currentCenterChunk.x * Chunk::WIDTH + Chunk::WIDTH / 2.0f,
            Chunk::HEIGHT / 2.0f,
            m_currentCenterChunk.y * Chunk::DEPTH + Chunk::DEPTH / 2.0f
        ));
    }
}

int ChunkManager::getTotalInstances() const {
    return m_visibleInstanceCount;
}

void ChunkManager::printStats() const {
    std::cout << "=== Chunk Manager Stats ===" << std::endl;
    std::cout << "Render Radius: " << m_renderRadius << std::endl;
    std::cout << "Loaded Chunks: " << getLoadedChunkCount() << std::endl;
    std::cout << "Visible Chunks: " << getVisibleChunkCount() << std::endl;
    std::cout << "Visible Instances: " << m_visibleInstanceCount << std::endl;
    std::cout << "Arena: " << m_arena.getInUse() << " / " << m_arena.getCapacity() << " instances" << std::endl;
    std::cout << "===========================" << std::endl;
}

ChunkKey ChunkManager::chunkPosToKey(const glm::ivec2& pos) const {
    return ((static_cast<int64_t>(pos.x) & 0xFFFFFFFF) << 32) |
        (static_cast<int64_t>(pos.y) & 0xFFFFFFFF);
}

void ChunkManager::updateVisibleChunks(const glm::vec3& cameraPos) {
    m_visibleChunks.clear();

    for (int dx = -m_renderRadius; dx <= m_renderRadius; ++dx) {
        for (int dz = -m_renderRadius; dz <= m_renderRadius; ++dz) {
            glm::ivec2 chunkPos(m_currentCenterChunk.x + dx,
                m_currentCenterChunk.y + dz);

            if (shouldChunkBeVisible(chunkPos, m_currentCenterChunk)) {
                Chunk* chunk = getChunk(chunkPos);

                if (chunk) {
                    chunk->setVisible(true);
                    m_visibleChunks.push_back(chunk);
                }
                else {
                    m_loadQueue.push(chunkPos);
                }
            }
        }
    }

    // 把不再可见的 chunk 标记为不可见（卸载策略另议）
    for (const auto& pair : m_loadedChunks) {
        Chunk* chunk = pair.second.get();
        glm::ivec2 chunkPos = chunk->getPosition();

        if (!shouldChunkBeVisible(chunkPos, m_currentCenterChunk)) {
            chunk->setVisible(false);
        }
    }
}

void ChunkManager::loadChunk(const glm::ivec2& chunkPos) {
    if (getChunk(chunkPos) != nullptr) {
        return;
    }

    auto chunk = std::make_unique<Chunk>(chunkPos,this);
    ChunkKey key = chunkPosToKey(chunkPos);

    chunk->load();

    m_visibleChunks.push_back(chunk.get());
    chunk->setVisible(true);

    m_loadedChunks[key] = std::move(chunk);

    std::cout << "Loaded chunk at (" << chunkPos.x << ", " << chunkPos.y << ")" << std::endl;
}

void ChunkManager::unloadChunk(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    auto it = m_loadedChunks.find(key);

    if (it != m_loadedChunks.end()) {
        std::cout << "Unloading chunk at (" << chunkPos.x << ", " << chunkPos.y << ")" << std::endl;
        releaseChunkSlot(key);
        m_loadedChunks.erase(it);
    }
}

void ChunkManager::releaseChunkSlot(ChunkKey key) {
    auto it = m_chunkSlots.find(key);
    if (it == m_chunkSlots.end()) return;
    m_arena.free(it->second);
    m_chunkSlots.erase(it);
}

void ChunkManager::uploadChunkToArena(Chunk* chunk) {
    // 不强制 compact：g_buffer.frag 对 BLOCK_ERRER 已 discard，
    // BLOCK_ERRER 占位顶点只是顶点着色器里几个白扔的 ALU，远比每次都 compact 便宜。
    // compact 由 Chunk::setBlockAndUpdate 内部按 errerCount/总数阈值触发，
    // compact 后下一次 reupload 自然把 GPU 段同步到压缩后的尺寸。
    const auto& data = chunk->getInstanceData();
    ChunkKey key = chunkPosToKey(chunk->getPosition());

    auto it = m_chunkSlots.find(key);
    ChunkArena::Slot oldSlot = (it != m_chunkSlots.end()) ? it->second : ChunkArena::Slot{};
    ChunkArena::Slot newSlot = m_arena.reupload(oldSlot,
        data.empty() ? nullptr : data.data(),
        (uint32_t)data.size());

    if (newSlot.valid() || data.empty()) {
        if (data.empty()) {
            // 没有面，把 slot 释放掉
            if (oldSlot.valid()) m_chunkSlots.erase(key);
        } else {
            m_chunkSlots[key] = newSlot;
        }
    }
    chunk->clearDirty();
}

void ChunkManager::rebuildDrawCommands() {
    std::lock_guard<std::mutex> lock(m_renderDataMutex);

    m_drawCommands.clear();
    m_drawCommands.reserve(m_visibleChunks.size());
    m_visibleInstanceCount = 0;

    for (Chunk* chunk : m_visibleChunks) {
        if (!chunk->isVisible() || !chunk->is_can_render(m_camera)) continue;

        // 脏的就上传一遍
        if (chunk->isDirty()) {
            uploadChunkToArena(chunk);
        }

        ChunkKey key = chunkPosToKey(chunk->getPosition());
        auto it = m_chunkSlots.find(key);
        if (it == m_chunkSlots.end()) continue;       // 该 chunk 没有面（如纯空气）
        const ChunkArena::Slot& slot = it->second;
        if (slot.count == 0) continue;

        DrawElementsIndirectCommand cmd{};
        cmd.count = 6;                  // 一个面 2 个三角形 6 个顶点
        cmd.instanceCount = slot.count;
        cmd.firstIndex = 0;
        cmd.baseVertex = 0;
        cmd.baseInstance = slot.offset;
        m_drawCommands.push_back(cmd);

        m_visibleInstanceCount += slot.count;
    }

    syncIndirectBuffer();
}

void ChunkManager::syncIndirectBuffer() {
    if (!m_indirectBuffer) return;
    if (m_drawCommands.empty()) return;

    GLsizeiptr needed = GLsizeiptr(m_drawCommands.size()) * sizeof(DrawElementsIndirectCommand);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
    if (needed > m_indirectBufferCapacityBytes) {
        m_indirectBufferCapacityBytes = needed * 2;
        glBufferData(GL_DRAW_INDIRECT_BUFFER, m_indirectBufferCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, needed, m_drawCommands.data());
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

bool ChunkManager::shouldChunkBeVisible(const glm::ivec2& chunkPos,
    const glm::ivec2& centerChunk) const {
    int dx = std::abs(chunkPos.x - centerChunk.x);
    int dz = std::abs(chunkPos.y - centerChunk.y);
    return dx <= m_renderRadius && dz <= m_renderRadius;
}

float ChunkManager::distanceToCamera(const glm::ivec2& chunkPos,
    const glm::vec3& cameraPos) const {
    glm::vec3 chunkCenter(
        chunkPos.x * Chunk::WIDTH + Chunk::WIDTH / 2.0f,
        Chunk::HEIGHT / 2.0f,
        chunkPos.y * Chunk::DEPTH + Chunk::DEPTH / 2.0f
    );
    return glm::distance(chunkCenter, cameraPos);
}

bool ChunkManager::setBlock(const glm::ivec3& worldPos, BlockType type) {
    glm::ivec2 chunkPos(
        static_cast<int>(std::floor(worldPos.x / (float)Chunk::WIDTH)),
        static_cast<int>(std::floor(worldPos.z / (float)Chunk::DEPTH))
    );
    Chunk* chunk = getChunk(chunkPos);
    if (!chunk) return false;

    glm::ivec3 localPos(
        ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH,
        worldPos.y,
        ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH
    );
    if (localPos.y < 0 || localPos.y >= Chunk::HEIGHT) return false;

    chunk->setBlockAndUpdate(localPos.x, localPos.y, localPos.z, type);
    return true;
}

BlockType ChunkManager::getBlockAt(const glm::ivec3& worldPos){
    glm::ivec2 chunkPos(
        static_cast<int>(std::floor(worldPos.x / (float)Chunk::WIDTH)),
        static_cast<int>(std::floor(worldPos.z / (float)Chunk::DEPTH))
    );
    Chunk* chunk = getChunk(chunkPos);
    if (!chunk) return BLOCK_AIR;

    glm::ivec3 localPos(
        ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH,
        worldPos.y,
        ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH
    );
    if (localPos.y < 0 || localPos.y >= Chunk::HEIGHT) return BLOCK_AIR;
    return chunk->getBlock(localPos.x, localPos.y, localPos.z);
}
