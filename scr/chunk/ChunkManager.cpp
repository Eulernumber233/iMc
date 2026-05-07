#include "ChunkManager.h"
#include "Chunk.h"
#include "Section.h"
#include "../generate/TerrainGenerator.h"
#include <iostream>
#include <algorithm>
#include <thread>

ChunkManager::ChunkManager(unsigned int seed)
    : m_renderRadius(8) {
    m_generator = std::make_shared<TerrainGenerator>();
    m_generator->setSeed(seed);
}

ChunkManager::~ChunkManager() {
    m_workerPool.stop();

    m_loadedChunks.clear();
    m_activeChunks.clear();
    m_sectionSlots.clear();
    m_drawCommands.clear();
    m_inFlight.clear();

    if (m_indirectBuffer) {
        glDeleteBuffers(1, &m_indirectBuffer);
        m_indirectBuffer = 0;
    }
    m_arena.shutdown();
}

SectionKey ChunkManager::makeSectionKey(int chunkX, int chunkZ, int sectionY) {
    uint64_t ux = (uint32_t)chunkX & 0xFFFFFFu;
    uint64_t uz = (uint32_t)chunkZ & 0xFFFFFFu;
    uint64_t uy = (uint32_t)sectionY & 0xFFu;
    return (ux << 32) | (uz << 8) | uy;
}

void ChunkManager::initialize(int renderRadius, const glm::vec3& cameraPos) {
    m_renderRadius = renderRadius;
    m_currentCenterChunk = glm::ivec2(
        (int)std::floor(cameraPos.x / Chunk::WIDTH),
        (int)std::floor(cameraPos.z / Chunk::DEPTH)
    );

    const uint32_t initialInstances = std::max<uint32_t>(1u << 16,
        (uint32_t)((2 * renderRadius + 1) * (2 * renderRadius + 1) * 1024));
    m_arena.initialize(initialInstances);

    glGenBuffers(1, &m_indirectBuffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
    m_indirectBufferCapacityBytes = sizeof(DrawElementsIndirectCommand) * 256;
    glBufferData(GL_DRAW_INDIRECT_BUFFER, m_indirectBufferCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    int hw = (int)std::thread::hardware_concurrency();
    int n = std::max(2, std::min(8, hw - 2));
    m_workerPool.start(m_generator.get(), n);

    updateActiveChunks(cameraPos);
}

void ChunkManager::update(std::shared_ptr<Camera> camera) {
    m_camera = camera;
    glm::ivec2 cameraChunk(
        (int)std::floor(m_camera->Position.x / Chunk::WIDTH),
        (int)std::floor(m_camera->Position.z / Chunk::DEPTH)
    );
    if (cameraChunk != m_currentCenterChunk) {
        m_currentCenterChunk = cameraChunk;
        updateActiveChunks(m_camera->Position);
    }

    // 把已完成的 worker result 接管进来（push 进 m_activeChunks）
    integrateBuiltChunks();

    // 补投递：初始化或一次性大批量请求时 MAX_INFLIGHT_REQUESTS 限流会丢一部分；
    // 完成的 chunk 腾出 in-flight 配额后，每帧扫一次半径内缺失的 chunk 把它们补上。
    if ((int)m_inFlight.size() < MAX_INFLIGHT_REQUESTS) {
        requestMissingChunks();
    }

    rebuildDrawCommands();
}

Chunk* ChunkManager::getChunk(const glm::ivec2& chunkPos) {
    auto it = m_loadedChunks.find(chunkPosToKey(chunkPos));
    return it != m_loadedChunks.end() ? it->second.get() : nullptr;
}

Chunk* ChunkManager::getChunk(int x, int z) {
    return getChunk(glm::ivec2(x, z));
}

std::vector<glm::ivec2> ChunkManager::getActiveChunkPositions() const {
    std::vector<glm::ivec2> ret;
    ret.reserve(m_activeChunks.size());
    for (auto chunk : m_activeChunks) ret.push_back(chunk->getPosition());
    return ret;
}

void ChunkManager::setRenderRadius(int radius) {
    if (radius > 0 && radius != m_renderRadius) {
        m_renderRadius = radius;
        updateActiveChunks(glm::vec3(
            m_currentCenterChunk.x * Chunk::WIDTH + Chunk::WIDTH / 2.0f,
            Chunk::HEIGHT / 2.0f,
            m_currentCenterChunk.y * Chunk::DEPTH + Chunk::DEPTH / 2.0f
        ));
    }
}

void ChunkManager::printStats() const {
    std::cout << "=== Chunk Manager Stats ===" << std::endl;
    std::cout << "Render Radius: " << m_renderRadius << std::endl;
    std::cout << "Loaded Chunks: " << getLoadedChunkCount() << std::endl;
    std::cout << "Active Chunks: " << getActiveChunkCount() << std::endl;
    std::cout << "Visible Instances: " << m_visibleInstanceCount << std::endl;
    std::cout << "InFlight: " << m_inFlight.size()
              << " / WorkerPending: " << m_workerPool.pendingCount() << std::endl;
    std::cout << "Arena: " << m_arena.getInUse() << " / " << m_arena.getCapacity()
              << " | freeBlocks=" << m_arena.getFreeBlockCount()
              << " largestFree=" << m_arena.getLargestFreeBlock() << std::endl;
    std::cout << "===========================" << std::endl;
}

ChunkKey ChunkManager::chunkPosToKey(const glm::ivec2& pos) const {
    return ((int64_t)(uint32_t)pos.x << 32) | (int64_t)(uint32_t)pos.y;
}

bool ChunkManager::isWithinActiveRadius(const glm::ivec2& chunkPos,
                                        const glm::ivec2& centerChunk) const {
    int dx = std::abs(chunkPos.x - centerChunk.x);
    int dz = std::abs(chunkPos.y - centerChunk.y);
    return dx <= m_renderRadius && dz <= m_renderRadius;
}

void ChunkManager::updateActiveChunks(const glm::vec3& cameraPos) {
    m_activeChunks.clear();

    for (int dx = -m_renderRadius; dx <= m_renderRadius; ++dx) {
        for (int dz = -m_renderRadius; dz <= m_renderRadius; ++dz) {
            glm::ivec2 chunkPos(m_currentCenterChunk.x + dx, m_currentCenterChunk.y + dz);

            Chunk* chunk = getChunk(chunkPos);
            if (chunk) {
                m_activeChunks.push_back(chunk);
            } else {
                if ((int)m_inFlight.size() < MAX_INFLIGHT_REQUESTS) {
                    requestChunkLoad(chunkPos);
                }
            }
        }
    }
}

void ChunkManager::requestMissingChunks() {
    // 优先靠近相机中心：用曼哈顿距离逐圈扩散
    for (int r = 0; r <= m_renderRadius; ++r) {
        if ((int)m_inFlight.size() >= MAX_INFLIGHT_REQUESTS) return;
        for (int dx = -r; dx <= r; ++dx) {
            int absDx = std::abs(dx);
            for (int dz = -r; dz <= r; ++dz) {
                if (std::max(absDx, std::abs(dz)) != r) continue; // 只取最外圈
                glm::ivec2 chunkPos(m_currentCenterChunk.x + dx, m_currentCenterChunk.y + dz);
                if (getChunk(chunkPos)) continue;
                if (m_inFlight.find(chunkPosToKey(chunkPos)) != m_inFlight.end()) continue;
                if ((int)m_inFlight.size() >= MAX_INFLIGHT_REQUESTS) return;
                requestChunkLoad(chunkPos);
            }
        }
    }
}

void ChunkManager::requestChunkLoad(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) return;
    if (m_inFlight.find(key) != m_inFlight.end()) return;
    m_inFlight.insert(key);
    m_workerPool.submit(chunkPos);
}

void ChunkManager::integrateBuiltChunks() {
    auto results = m_workerPool.drainCompleted();
    for (auto& r : results) {
        ChunkKey key = chunkPosToKey(r.pos);
        m_inFlight.erase(key);

        // 已存在（极少情况：重复投递）→ 丢弃。
        if (m_loadedChunks.find(key) != m_loadedChunks.end()) continue;

        auto chunk = std::make_unique<Chunk>(r.pos, this);
        chunk->adoptSections(std::move(r.sections));

        Chunk* raw = chunk.get();
        m_loadedChunks[key] = std::move(chunk);
        linkNeighbors(raw);
        raw->stitchHorizontalNeighbors();

        // 仍在玩家半径内才加入活跃列表
        if (isWithinActiveRadius(r.pos, m_currentCenterChunk)) {
            m_activeChunks.push_back(raw);
        }
    }
}

void ChunkManager::linkNeighbors(Chunk* newChunk) {
    glm::ivec2 p = newChunk->getPosition();
    Chunk* xp = getChunk(p.x + 1, p.y);
    Chunk* xn = getChunk(p.x - 1, p.y);
    Chunk* zp = getChunk(p.x, p.y + 1);
    Chunk* zn = getChunk(p.x, p.y - 1);

    newChunk->m_neighbors[0] = xp;
    newChunk->m_neighbors[1] = xn;
    newChunk->m_neighbors[2] = zp;
    newChunk->m_neighbors[3] = zn;

    if (xp) xp->m_neighbors[1] = newChunk;
    if (xn) xn->m_neighbors[0] = newChunk;
    if (zp) zp->m_neighbors[3] = newChunk;
    if (zn) zn->m_neighbors[2] = newChunk;
}

void ChunkManager::releaseSectionSlot(SectionKey key) {
    auto it = m_sectionSlots.find(key);
    if (it == m_sectionSlots.end()) return;
    m_arena.free(it->second);
    m_sectionSlots.erase(it);
}

void ChunkManager::uploadSection(int chunkX, int chunkZ, int sectionY,
                                 Section& section, int& uploadBudget) {
    if (uploadBudget <= 0) return;

    SectionKey key = makeSectionKey(chunkX, chunkZ, sectionY);
    const auto& data = section.getInstanceData();

    auto it = m_sectionSlots.find(key);
    ChunkArena::Slot oldSlot = (it != m_sectionSlots.end()) ? it->second : ChunkArena::Slot{};
    ChunkArena::Slot newSlot = m_arena.reupload(oldSlot,
        data.empty() ? nullptr : data.data(),
        (uint32_t)data.size());

    if (data.empty()) {
        if (oldSlot.valid()) m_sectionSlots.erase(key);
    } else if (newSlot.valid()) {
        m_sectionSlots[key] = newSlot;
    }

    section.clearDirty();
    --uploadBudget;
}

void ChunkManager::rebuildDrawCommands() {
    m_drawCommands.clear();
    m_visibleInstanceCount = 0;

    int uploadBudget = MAX_UPLOADS_PER_FRAME;

    // 第一遍：所有已加载 chunk 的脏 section 都尝试上传。
    // 跨 chunk stitch 可能改到当前不渲染的 chunk 的 mesh，必须先把数据传上去。
    for (auto& kv : m_loadedChunks) {
        Chunk* chunk = kv.second.get();
        if (!chunk->isMeshReady()) continue;
        for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
            Section& s = chunk->getSection(sy);
            if (s.isDirty()) {
                glm::ivec2 cp = chunk->getPosition();
                uploadSection(cp.x, cp.y, sy, s, uploadBudget);
                if (uploadBudget <= 0) break;
            }
        }
        if (uploadBudget <= 0) break;
    }

    // 第二遍：按 active chunk -> per-section 视锥/距离剔除生成 indirect 命令
    for (Chunk* chunk : m_activeChunks) {
        if (!chunk->isMeshReady()) continue;
        if (!chunk->isChunkPotentiallyVisible(m_camera)) continue;

        for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
            if (!chunk->isSectionVisible(sy, m_camera)) continue;

            SectionKey key = makeSectionKey(chunk->getPosition().x,
                                            chunk->getPosition().y, sy);
            auto it = m_sectionSlots.find(key);
            if (it == m_sectionSlots.end()) continue;
            const ChunkArena::Slot& slot = it->second;
            if (slot.count == 0) continue;

            DrawElementsIndirectCommand cmd{};
            cmd.count = 6;
            cmd.instanceCount = slot.count;
            cmd.firstIndex = 0;
            cmd.baseVertex = 0;
            cmd.baseInstance = slot.offset;
            m_drawCommands.push_back(cmd);
            m_visibleInstanceCount += slot.count;
        }
    }

    syncIndirectBuffer();
}

void ChunkManager::syncIndirectBuffer() {
    if (!m_indirectBuffer || m_drawCommands.empty()) return;
    GLsizeiptr needed = (GLsizeiptr)m_drawCommands.size() * sizeof(DrawElementsIndirectCommand);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
    if (needed > m_indirectBufferCapacityBytes) {
        m_indirectBufferCapacityBytes = needed * 2;
        glBufferData(GL_DRAW_INDIRECT_BUFFER, m_indirectBufferCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, needed, m_drawCommands.data());
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
}

bool ChunkManager::setBlock(const glm::ivec3& worldPos, BlockType type) {
    glm::ivec2 chunkPos(
        (int)std::floor(worldPos.x / (float)Chunk::WIDTH),
        (int)std::floor(worldPos.z / (float)Chunk::DEPTH)
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

BlockType ChunkManager::getBlockAt(const glm::ivec3& worldPos) {
    glm::ivec2 chunkPos(
        (int)std::floor(worldPos.x / (float)Chunk::WIDTH),
        (int)std::floor(worldPos.z / (float)Chunk::DEPTH)
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
