#include "ChunkManager.h"
#include "Chunk.h"
#include "Section.h"
#include "../generate/TerrainGenerator.h"
#include "../RuntimeConfig.h"
#include "../save/ChunkSaveManager.h"
#include "../Profiler.h"
#include <array>
#include <iostream>
#include <algorithm>
#include <thread>
#include <GLFW/glfw3.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {
    // 找到 v 中最低位的 1 的索引（v 必须非零）
    inline int lowestBitIndex(uint32_t v) {
#ifdef _MSC_VER
        unsigned long idx;
        _BitScanForward(&idx, v);
        return (int)idx;
#else
        return __builtin_ctz(v);
#endif
    }
}

ChunkManager::ChunkManager(uint64_t seed)
    : m_renderRadius(8) {
    m_generator = std::make_shared<TerrainGenerator>();
    m_generator->setSeed(seed);
}

ChunkManager::~ChunkManager() {
    m_workerPool.stop();

    m_loadedChunks.clear();
    m_pendingChunks.clear();
    m_pendingIntegrate.clear();
    m_activeChunks.clear();
    m_sectionSlots.clear();
    m_drawCommands.clear();
    m_inFlight.clear();

    if (m_indirectBuffer) {
        glDeleteBuffers(1, &m_indirectBuffer);
        m_indirectBuffer = 0;
    }
    if (m_sectionBaseSSBO) {
        glDeleteBuffers(1, &m_sectionBaseSSBO);
        m_sectionBaseSSBO = 0;
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
    m_maxUploadsPerFrame = RuntimeConfig::get().maxUploadsPerFrame;
    m_maxInflightRequests = RuntimeConfig::get().maxInflightRequests;
    m_autoSaveIntervalSec = RuntimeConfig::get().autoSaveIntervalSec;
    m_lastSaveCheckTime = glfwGetTime();
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

    // SectionBases SSBO：与 indirect buffer 命令一一对应，shader 用 gl_DrawID 索引
    glGenBuffers(1, &m_sectionBaseSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_sectionBaseSSBO);
    m_sectionBaseCapacityBytes = sizeof(glm::vec4) * 256;
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_sectionBaseCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    int n = RuntimeConfig::get().workerThreads;
    if (n <= 0) {
        int hw = (int)std::thread::hardware_concurrency();
        n = std::max(2, std::min(8, hw - 2));
    }
    m_workerPool.start(m_generator.get(), n);

    updateActiveChunks(cameraPos);
}

void ChunkManager::update(std::shared_ptr<Camera> camera) {
    PROFILE_SCOPE("ChunkManager::update");
    m_camera = camera;
    glm::ivec2 cameraChunk(
        (int)std::floor(m_camera->Position.x / Chunk::WIDTH),
        (int)std::floor(m_camera->Position.z / Chunk::DEPTH)
    );
    if (cameraChunk != m_currentCenterChunk) {
        m_currentCenterChunk = cameraChunk;
        updateActiveChunks(m_camera->Position);
        m_needChunkScan = true;
    }

    // 相机移动 >1m 或 FOV 变化时递增可见性版本号，各 chunk 用它替代各自 distance 比较
    float camDist = glm::distance(m_lastVisCameraPos, m_camera->Position);
    float fovDelta = std::abs(m_lastVisCameraFOV - m_camera->FOV);
    if (camDist > 1.0f || fovDelta > 0.1f) {
        m_lastVisCameraPos = m_camera->Position;
        m_lastVisCameraFOV = m_camera->FOV;
        ++m_visGeneration;
    }

    integrateBuiltChunks();
    integrateStitchResults();

    // 补投递：m_needChunkScan 避免半径内全部就位后每帧空扫 361 个位置
    if (m_needChunkScan && (int)m_inFlight.size() < m_maxInflightRequests) {
        PROFILE_SCOPE("requestMissingChunks");
        requestMissingChunks();
    }

    rebuildDrawCommands();

    // 自动保存定时器 + 远距离区块卸载
    if (m_saveManager) {
        double now = glfwGetTime();
        float dt = (float)(now - m_lastSaveCheckTime);
        m_lastSaveCheckTime = now;

        m_autoSaveTimer += dt;
        if (m_autoSaveTimer >= (float)m_autoSaveIntervalSec) {
            doAutoSave();
            m_autoSaveTimer = 0.0f;
        }

        unloadDistantChunks();
    }
}

Chunk* ChunkManager::getChunk(const glm::ivec2& chunkPos) {
    auto it = m_loadedChunks.find(chunkPosToKey(chunkPos));
    return it != m_loadedChunks.end() ? it->second.get() : nullptr;
}

Chunk* ChunkManager::getChunk(int x, int z) {
    return getChunk(glm::ivec2(x, z));
}

Chunk* ChunkManager::getChunkAnyState(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    auto it = m_loadedChunks.find(key);
    if (it != m_loadedChunks.end()) return it->second.get();
    auto it2 = m_pendingChunks.find(key);
    if (it2 != m_pendingChunks.end()) return it2->second.get();
    return nullptr;
}

std::vector<glm::ivec2> ChunkManager::getActiveChunkPositions() const {
    std::vector<glm::ivec2> ret;
    ret.reserve(m_activeChunks.size());
    for (auto chunk : m_activeChunks) ret.push_back(chunk->getPosition());
    return ret;
}

std::vector<glm::ivec2> ChunkManager::getLoadedChunkPositions() const {
    std::vector<glm::ivec2> ret;
    ret.reserve(m_loadedChunks.size());
    for (auto& kv : m_loadedChunks) ret.push_back(kv.second->getPosition());
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

bool ChunkManager::isWithinEvictRadius(const glm::ivec2& chunkPos,
    const glm::ivec2& centerChunk) const {
    int dx = std::abs(chunkPos.x - centerChunk.x);
    int dz = std::abs(chunkPos.y - centerChunk.y);
    int r = m_renderRadius + EVICT_MARGIN_CHUNKS;
    return dx <= r && dz <= r;
}

void ChunkManager::updateActiveChunks(const glm::vec3& cameraPos) {
    m_activeChunks.clear();

    // 1) active 半径内：已 loaded 的进 active 列表
    for (int dx = -m_renderRadius; dx <= m_renderRadius; ++dx) {
        for (int dz = -m_renderRadius; dz <= m_renderRadius; ++dz) {
            glm::ivec2 chunkPos(m_currentCenterChunk.x + dx, m_currentCenterChunk.y + dz);
            if (Chunk* chunk = getChunk(chunkPos)) {
                m_activeChunks.push_back(chunk);
            }
        }
    }

    // 2) 投递缺失的 build：从中心向外按 Chebyshev 距离逐圈扩散，确保由近到远加载。
    //    扫描范围扩展到 stitch 预加载半径，外圈陪练用于让边缘 chunk 凑齐 4 邻居。
    if ((int)m_inFlight.size() < m_maxInflightRequests) {
        requestMissingChunks();
    }

    // 把走出 evict 半径的 chunk 的 GPU slot 释放掉（CPU 数据保留）
    evictFarChunkSlots();
}

void ChunkManager::evictFarChunkSlots() {
    PROFILE_SCOPE("evictFarChunkSlots");
    for (auto& kv : m_loadedChunks) {
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;
        glm::ivec2 cp = chunk->getPosition();
        if (isWithinEvictRadius(cp, m_currentCenterChunk)) continue;

        // 走出 evict 半径 → 释放该 chunk 所有 section 的 GPU slot
        for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
            Section& sec = chunk->getSection(sy);
            ChunkArena::Slot slot = sec.getGpuSlot();
            if (!slot.valid()) continue;
            m_arena.free(slot);
            SectionKey key = makeSectionKey(cp.x, cp.y, sy);
            m_sectionSlots.erase(key);
            sec.notifyGpuSlotReleased(); // 同时清除 section 内缓存的 slot
        }
    }
}

void ChunkManager::requestMissingChunks() {
    // 网络客户端：不进行地形生成，chunk 由 importChunkData 提供
    if (m_networkClient) return;

    // 优先靠近相机中心：用曼哈顿距离逐圈扩散；扫描到 stitch 预加载半径以填外圈陪练。
    const int loadR = m_renderRadius + STITCH_PRELOAD_MARGIN;
    bool foundMissing = false;
    for (int r = 0; r <= loadR; ++r) {
        if ((int)m_inFlight.size() >= m_maxInflightRequests) return;
        for (int dx = -r; dx <= r; ++dx) {
            int absDx = std::abs(dx);
            for (int dz = -r; dz <= r; ++dz) {
                if (std::max(absDx, std::abs(dz)) != r) continue; // 只取最外圈
                glm::ivec2 chunkPos(m_currentCenterChunk.x + dx, m_currentCenterChunk.y + dz);
                if (getChunk(chunkPos) || getPendingChunk(chunkPos)) continue;
                if (m_inFlight.find(chunkPosToKey(chunkPos)) != m_inFlight.end()) continue;
                if ((int)m_inFlight.size() >= m_maxInflightRequests) return;
                foundMissing = true;
                requestChunkLoad(chunkPos);
            }
        }
    }
    // 全部圈扫完没有缺口 → 下次不必再扫，等相机移动或 chunk 卸载再复位
    if (!foundMissing) m_needChunkScan = false;
}

void ChunkManager::requestChunkLoad(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) return;
    if (m_pendingChunks.find(key) != m_pendingChunks.end()) return;
    if (m_inFlight.find(key) != m_inFlight.end()) return;
    m_inFlight.insert(key);
    m_workerPool.submit(chunkPos);
}

void ChunkManager::importChunkData(int chunkX, int chunkZ,
                                   std::unique_ptr<BlockState[]> blockBuffer) {
    glm::ivec2 pos(chunkX, chunkZ);
    ChunkKey key = chunkPosToKey(pos);

    // 跳过已存在或已在加载的 chunk
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) {
        printf("[ChunkManager] importChunkData: (%d,%d) already loaded, skip\n", chunkX, chunkZ);
        return;
    }
    if (m_pendingChunks.find(key) != m_pendingChunks.end()) {
        printf("[ChunkManager] importChunkData: (%d,%d) already pending, skip\n", chunkX, chunkZ);
        return;
    }
    if (m_inFlight.find(key) != m_inFlight.end()) {
        printf("[ChunkManager] importChunkData: (%d,%d) already in-flight, skip\n", chunkX, chunkZ);
        return;
    }

    m_inFlight.insert(key);
    m_workerPool.submitImport(pos, std::move(blockBuffer));
    printf("[ChunkManager] importChunkData: (%d,%d) submitted, inflight=%zu\n",
        chunkX, chunkZ, m_inFlight.size());
}

Chunk* ChunkManager::getPendingChunk(const glm::ivec2& chunkPos) {
    auto it = m_pendingChunks.find(chunkPosToKey(chunkPos));
    return it != m_pendingChunks.end() ? it->second.get() : nullptr;
}

void ChunkManager::integrateBuiltChunks() {
    PROFILE_SCOPE("integrateBuiltChunks");
    {
        PROFILE_SCOPE("ibc.drainCompleted");
        auto results = m_workerPool.drainCompleted();
        if (!results.empty()) {
            Profiler::addCounter("ibc.resultCount", (int64_t)results.size());
            for (auto& r : results) m_pendingIntegrate.push_back(std::move(r));
        }
    }
    if (m_pendingIntegrate.empty()) return;

    int budget = MAX_INTEGRATE_PER_FRAME;
    while (budget > 0 && !m_pendingIntegrate.empty()) {
        ChunkBuildResult r = std::move(m_pendingIntegrate.front());
        m_pendingIntegrate.pop_front();

        ChunkKey key = chunkPosToKey(r.pos);
        m_inFlight.erase(key);

        // 已存在 → 丢弃（重复投递或竞态），不消耗 budget。
        if (m_loadedChunks.find(key) != m_loadedChunks.end()) continue;
        if (m_pendingChunks.find(key) != m_pendingChunks.end()) continue;

        auto chunk = std::make_unique<Chunk>(r.pos, this);
        {
            PROFILE_SCOPE("ibc.adoptSections");
            chunk->adoptSections(std::move(r.sections));
        }

        Chunk* raw = chunk.get();
        m_pendingChunks[key] = std::move(chunk);
        {
            PROFILE_SCOPE("ibc.linkNeighbors");
            linkNeighbors(raw);
        }

        {
            PROFILE_SCOPE("ibc.trySubmitStitchJobs");
            trySubmitStitchJobs(raw);
            for (int d = 0; d < 4; ++d) {
                if (Chunk* nb = raw->m_neighbors[d]) {
                    trySubmitStitchJobs(nb);
                }
            }
        }
        --budget;
    }
    Profiler::addCounter("ibc.queueDepth", (int64_t)m_pendingIntegrate.size());
}

void ChunkManager::linkNeighbors(Chunk* newChunk) {
    glm::ivec2 p = newChunk->getPosition();
    // 邻居指针对 pending / loaded 都连通：玩家交互路径（setBlockAndUpdate）需要跨 chunk
    // 路由到任何已存在的邻居。stitch 任务的发起逻辑由 trySubmitStitchJobs 单独管控，只对
    // pending↔pending 投递，所以指向 loaded 邻居不会触发 worker 修改 loaded mesh。
    auto findAny = [this](const glm::ivec2& q) -> Chunk* {
        if (Chunk* c = getPendingChunk(q)) return c;
        return getChunk(q); // loaded
        };
    Chunk* xp = findAny(glm::ivec2(p.x + 1, p.y));
    Chunk* xn = findAny(glm::ivec2(p.x - 1, p.y));
    Chunk* zp = findAny(glm::ivec2(p.x, p.y + 1));
    Chunk* zn = findAny(glm::ivec2(p.x, p.y - 1));

    newChunk->m_neighbors[0] = xp;
    newChunk->m_neighbors[1] = xn;
    newChunk->m_neighbors[2] = zp;
    newChunk->m_neighbors[3] = zn;

    // 连接邻居时，若对端之前因无邻居而提前标了 stitch done，现在邻居出现了，
    // 需要复位该方向的 done（只复位仍在 pending 中的 chunk，loaded 不参与 stitch）。
    if (xp) {
        if (xp->isStitchDone(1) && m_pendingChunks.find(chunkPosToKey(xp->getPosition())) != m_pendingChunks.end()) {
            xp->m_stitchDone &= ~(1u << 1);
        }
        xp->m_neighbors[1] = newChunk;
    }
    if (xn) {
        if (xn->isStitchDone(0) && m_pendingChunks.find(chunkPosToKey(xn->getPosition())) != m_pendingChunks.end()) {
            xn->m_stitchDone &= ~(1u << 0);
        }
        xn->m_neighbors[0] = newChunk;
    }
    if (zp) {
        if (zp->isStitchDone(3) && m_pendingChunks.find(chunkPosToKey(zp->getPosition())) != m_pendingChunks.end()) {
            zp->m_stitchDone &= ~(1u << 3);
        }
        zp->m_neighbors[3] = newChunk;
    }
    if (zn) {
        if (zn->isStitchDone(2) && m_pendingChunks.find(chunkPosToKey(zn->getPosition())) != m_pendingChunks.end()) {
            zn->m_stitchDone &= ~(1u << 2);
        }
        zn->m_neighbors[2] = newChunk;
    }
}

void ChunkManager::trySubmitStitchJobs(Chunk* chunk) {
    // 仅当 self 在 pending 时调用。4 方向逐个看：邻居存在且也在 pending、双方该边
    // 都未 done 且未 pending → 投递。loaded 邻居跳过（loaded 不再参与 stitch；
    // 不变量是 chunk 进入 loaded 时它和所有邻居的 stitch 都已经完成）。
    //
    // 互斥：每个 chunk 同一时刻只能参与一个 stitch 任务（m_stitchBusy）。
    // 因为 stitchWithNeighbor 会改 self 与 nb 的 mesh，两个 worker 同时改同一 chunk
    // 会产生数据竞争。busy 由 self 和 nb 共同决定，stitch 完成时主线程清掉双方 busy
    // 并再次调用 trySubmitStitchJobs，把剩余方向的边继续推进。
    static constexpr int oppDir[4] = { 1, 0, 3, 2 };
    static const BlockFace dirToFace[4] = { RIGHT, LEFT, FRONT, BACK };

    if (chunk->isStitchBusy()) return;

    for (int d = 0; d < 4; ++d) {
        if (chunk->isStitchDone(d) || chunk->isStitchPending(d)) continue;
        Chunk* nb = chunk->m_neighbors[d];
        if (!nb) {
            // 无邻居：网络客户端可能在已加载区域边缘，或邻居尚未导入。
            // 暂不处理（也不标 done），等邻居出现后 linkNeighbors 会连通。
            continue;
        }
        // loaded 邻居：见函数注释，按 done 处理。
        if (m_loadedChunks.find(chunkPosToKey(nb->getPosition())) != m_loadedChunks.end()) {
            chunk->markStitchDone(d);
            continue;
        }
        int od = oppDir[d];
        if (nb->isStitchDone(od) || nb->isStitchPending(od)) continue;
        if (nb->isStitchBusy()) continue; // 邻居正在被别的 stitch 任务占用

        chunk->markStitchPending(d);
        nb->markStitchPending(od);
        chunk->setStitchBusy(true);
        nb->setStitchBusy(true);
        m_workerPool.submitStitch(chunk, nb, (uint8_t)dirToFace[d]);
        return; // self 已经 busy，剩下的方向得等本次完成才能投
    }
}

void ChunkManager::integrateStitchResults() {
    PROFILE_SCOPE("integrateStitchResults");
    std::vector<StitchResult> results;
    {
        PROFILE_SCOPE("isr.drainStitch");
        results = m_workerPool.drainStitchResults();
    }
    if (results.empty()) return;
    Profiler::addCounter("isr.resultCount", (int64_t)results.size());

    static constexpr int faceToDir[6] = { 0, 1, 2, 3, -1, -1 }; // RIGHT/LEFT/FRONT/BACK -> 0..3

    for (const auto& sr : results) {
        Chunk* a = getPendingChunk(sr.posA);
        Chunk* b = getPendingChunk(sr.posB);
        // 不变量：pending chunk 在 stitch 完成前不会被晋升或销毁，所以这两个查找一定命中。
        // 防御性跳过仅为容灾。
        if (!a || !b) continue;
        int dA = faceToDir[sr.dirA];
        int dB = faceToDir[sr.dirB];
        if (dA < 0 || dB < 0) continue;
        a->markStitchDone(dA);
        b->markStitchDone(dB);
        // 清除 busy，恢复 a/b 接受新 stitch 投递。
        a->setStitchBusy(false);
        b->setStitchBusy(false);
    }

    {
        PROFILE_SCOPE("isr.repush");
        // 推进剩余方向：busy 解除后双方都可能再投一条新边。同时本次涉及的 chunk 还可能
        // 让它们的"次邻居"具备投递条件（次邻居的某方向曾因为对端 busy 跳过）。
        for (const auto& sr : results) {
            Chunk* a = getPendingChunk(sr.posA);
            Chunk* b = getPendingChunk(sr.posB);
            if (a) {
                trySubmitStitchJobs(a);
                for (int d = 0; d < 4; ++d) if (Chunk* nb = a->m_neighbors[d]) {
                    if (m_pendingChunks.find(chunkPosToKey(nb->getPosition())) != m_pendingChunks.end())
                        trySubmitStitchJobs(nb);
                }
            }
            if (b) {
                trySubmitStitchJobs(b);
                for (int d = 0; d < 4; ++d) if (Chunk* nb = b->m_neighbors[d]) {
                    if (m_pendingChunks.find(chunkPosToKey(nb->getPosition())) != m_pendingChunks.end())
                        trySubmitStitchJobs(nb);
                }
            }
        }
    }

    {
        PROFILE_SCOPE("isr.promote");
        // 再扫一遍把 4 方向都 done 的 chunk 晋升到 loaded。
        for (const auto& sr : results) {
            promoteIfReady(sr.posA);
            promoteIfReady(sr.posB);
        }
    }
}

void ChunkManager::promoteIfReady(const glm::ivec2& chunkPos) {
    auto it = m_pendingChunks.find(chunkPosToKey(chunkPos));
    if (it == m_pendingChunks.end()) return;
    Chunk* raw = it->second.get();

    // 检查 4 方向是否全部 stitch done。
    // 网络客户端模式下，无邻居的方向也视作完成（外侧全空气，打开边界面即可）。
    static const BlockFace dirToFace[4] = { RIGHT, LEFT, FRONT, BACK };
    bool ready = true;
    for (int d = 0; d < 4; ++d) {
        if (!raw->isStitchDone(d) && raw->m_neighbors[d] != nullptr) {
            ready = false;
            break;
        }
    }
    if (!ready) return;

    // 无邻居的方向：打开边界面（外侧视为全空气）
    for (int d = 0; d < 4; ++d) {
        if (!raw->isStitchDone(d) && raw->m_neighbors[d] == nullptr) {
            raw->openBoundaryFace(dirToFace[d]);
            raw->markStitchDone(d);
        }
    }

    if (!raw->allStitchDone()) return;  // 安全断言，理论上不会到此

    // 4 方向都 stitch 完毕 → 晋升。
    auto node = m_pendingChunks.extract(it);
    ChunkKey key = chunkPosToKey(chunkPos);
    m_loadedChunks[key] = std::move(node.mapped());
    if (isWithinActiveRadius(chunkPos, m_currentCenterChunk)) {
        m_activeChunks.push_back(raw);
    }
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
    const auto& dirtyIdx = section.getDirtyIndices();

    ChunkArena::Slot oldSlot = section.getGpuSlot();

    bool canIncremental =
        oldSlot.valid()
        && !section.fullRebuildPending()
        && !dirtyIdx.empty()
        && (uint32_t)data.size() <= oldSlot.capacity;

    if (canIncremental) {
        ChunkArena::Slot s = oldSlot;
        m_arena.patch(s, data.data(),
            dirtyIdx.data(), (uint32_t)dirtyIdx.size(),
            (uint32_t)data.size());
        m_sectionSlots[key] = s;
        section.setGpuSlot(s);
    }
    else {
        ChunkArena::Slot newSlot = m_arena.reupload(oldSlot,
            data.empty() ? nullptr : data.data(),
            (uint32_t)data.size());

        if (data.empty()) {
            if (oldSlot.valid()) m_sectionSlots.erase(key);
            section.setGpuSlot(ChunkArena::Slot{});
        }
        else if (newSlot.valid()) {
            m_sectionSlots[key] = newSlot;
            section.setGpuSlot(newSlot);
        }
    }

    section.clearDirty();
    --uploadBudget;
}

void ChunkManager::rebuildDrawCommands() {
    PROFILE_SCOPE("rebuildDrawCommands");
    m_drawCommands.clear();
    m_sectionBases.clear();
    m_visibleInstanceCount = 0;

    int uploadBudget = m_maxUploadsPerFrame;
    int uploadedCount = 0;

    {
        PROFILE_SCOPE("rdc.uploadPass");
        // 第一遍：evict 半径内的脏 section 上传。
        for (auto& kv : m_loadedChunks) {
            Chunk* chunk = kv.second.get();
            if (!chunk->isMeshReady()) continue;
            glm::ivec2 cp = chunk->getPosition();
            if (!isWithinEvictRadius(cp, m_currentCenterChunk)) continue;
            for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
                Section& s = chunk->getSection(sy);
                if (!s.isDirty()) continue;
                int before = uploadBudget;
                uploadSection(cp.x, cp.y, sy, s, uploadBudget);
                if (before != uploadBudget) ++uploadedCount;
                if (uploadBudget <= 0) break;
            }
            if (uploadBudget <= 0) break;
        }
    }
    Profiler::addCounter("rdc.uploadCount", uploadedCount);

    {
        PROFILE_SCOPE("rdc.cullPass");
        const auto& cfg = RuntimeConfig::get();
        const Camera* cam = m_camera.get();
        int cameraSectionY = (int)(cam->Position.y / Section::HEIGHT);
        int maxDownSections = cfg.verticalCullRatio > 0.0f
            ? (int)(m_renderRadius * cfg.verticalCullRatio)
            : 0;

        std::array<glm::vec4, 6> frustumPlanes;
        const std::array<glm::vec4, 6>* pPlanes = nullptr;
        if (cam->FrustumCullingEnabled) {
            frustumPlanes = cam->GetFrustumPlanes();
            pPlanes = &frustumPlanes;
        }

        int chunkTotal = 0, chunkCoarseCull = 0;
        int64_t coarseVisUs = 0, getMaskUs = 0, emitCmdUs = 0;

        for (Chunk* chunk : m_activeChunks) {
            if (!chunk->isMeshReady()) continue;
            ++chunkTotal;

            auto t0 = std::chrono::steady_clock::now();
            bool visible = chunk->isChunkPotentiallyVisible(cam);
            auto t1 = std::chrono::steady_clock::now();
            coarseVisUs += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            if (!visible) { ++chunkCoarseCull; continue; }

            uint32_t mask = chunk->getVisibleSectionMask(cam, cameraSectionY, maxDownSections, pPlanes);
            auto t2 = std::chrono::steady_clock::now();
            getMaskUs += std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

            while (mask) {
                int sy = lowestBitIndex(mask);
                mask &= mask - 1u;

                const ChunkArena::Slot& slot = chunk->getSection(sy).getGpuSlot();
                if (!slot.valid() || slot.count == 0) continue;

                DrawElementsIndirectCommand cmd{};
                cmd.count = 6;
                cmd.instanceCount = slot.count;
                cmd.firstIndex = 0;
                cmd.baseVertex = 0;
                cmd.baseInstance = slot.offset;
                m_drawCommands.push_back(cmd);

                glm::ivec2 cp = chunk->getPosition();
                m_sectionBases.emplace_back(
                    float(cp.x * Chunk::WIDTH),
                    float(sy * Section::HEIGHT),
                    float(cp.y * Chunk::DEPTH),
                    0.0f);

                m_visibleInstanceCount += slot.count;
            }
            auto t3 = std::chrono::steady_clock::now();
            emitCmdUs += std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
        }

        Profiler::addSample("rdc.cull.coarseVis", coarseVisUs);
        Profiler::addSample("rdc.cull.getMask", getMaskUs);
        Profiler::addSample("rdc.cull.emitCmd", emitCmdUs);
        Profiler::addCounter("rdc.chunkTotal", chunkTotal);
        Profiler::addCounter("rdc.chunkCoarseCull", chunkCoarseCull);
    } // rdc.cullPass

    Profiler::addCounter("rdc.drawCmdCount", (int64_t)m_drawCommands.size());
    Profiler::addCounter("rdc.visibleInstances", (int64_t)m_visibleInstanceCount);

    {
        PROFILE_SCOPE("rdc.syncGpuBuffers");
        syncIndirectBuffer();
        syncSectionBaseSSBO();
    }
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

void ChunkManager::syncSectionBaseSSBO() {
    if (!m_sectionBaseSSBO || m_sectionBases.empty()) return;
    GLsizeiptr needed = (GLsizeiptr)m_sectionBases.size() * sizeof(glm::vec4);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_sectionBaseSSBO);
    if (needed > m_sectionBaseCapacityBytes) {
        m_sectionBaseCapacityBytes = needed * 2;
        glBufferData(GL_SHADER_STORAGE_BUFFER, m_sectionBaseCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, needed, m_sectionBases.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

bool ChunkManager::setBlock(const glm::ivec3& worldPos, BlockState state) {
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

    chunk->setBlockAndUpdate(localPos.x, localPos.y, localPos.z, state);
    return true;
}

BlockState ChunkManager::getBlockAt(const glm::ivec3& worldPos) {
    glm::ivec2 chunkPos(
        (int)std::floor(worldPos.x / (float)Chunk::WIDTH),
        (int)std::floor(worldPos.z / (float)Chunk::DEPTH)
    );
    Chunk* chunk = getChunk(chunkPos);
    if (!chunk) return BlockState{};
    glm::ivec3 localPos(
        ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH,
        worldPos.y,
        ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH
    );
    if (localPos.y < 0 || localPos.y >= Chunk::HEIGHT) return BlockState{};
    return chunk->getBlock(localPos.x, localPos.y, localPos.z);
}

void ChunkManager::setSaveManager(ChunkSaveManager* sm) {
    m_saveManager = sm;
    m_workerPool.setSaveManager(sm);
}

// ====================== 存档 ======================

bool ChunkManager::isWithinUnloadRadius(const glm::ivec2& chunkPos,
    const glm::ivec2& centerChunk) const {
    int dx = std::abs(chunkPos.x - centerChunk.x);
    int dz = std::abs(chunkPos.y - centerChunk.y);
    int r = m_renderRadius + UNLOAD_MARGIN_CHUNKS;
    return dx <= r && dz <= r;
}

void ChunkManager::saveChunkToDisk(Chunk* chunk) {
    if (!m_saveManager || !chunk) return;

    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    constexpr int W = Chunk::WIDTH;
    constexpr int D = Chunk::DEPTH;

    // 拼装 flat buffer：(y * D + z) * W + x
    auto buf = std::make_unique<BlockState[]>(VOL);
    BlockState* dst = buf.get();
    for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
        const Section& sec = chunk->getSection(sy);
        const BlockState* src = sec.stateData();
        int baseY = sy * Section::HEIGHT;
        for (int y = 0; y < Section::HEIGHT; ++y) {
            for (int z = 0; z < D; ++z) {
                for (int x = 0; x < W; ++x) {
                    dst[(baseY + y) * D * W + z * W + x] =
                        src[(y * Section::DEPTH + z) * Section::WIDTH + x];
                }
            }
        }
    }

    m_saveManager->saveChunk(chunk->getPosition(), dst);
    chunk->clearSaveDirty();
}

void ChunkManager::doAutoSave() {
    if (!m_saveManager) return;
    PROFILE_SCOPE("doAutoSave");
    int saved = 0;
    for (auto& kv : m_loadedChunks) {
        Chunk* c = kv.second.get();
        if (c && c->isSaveDirty()) {
            saveChunkToDisk(c);
            ++saved;
        }
    }
}

void ChunkManager::unloadDistantChunks() {
    if (!m_saveManager) return;
    PROFILE_SCOPE("unloadDistantChunks");

    std::vector<glm::ivec2> toRemove;
    for (auto& kv : m_loadedChunks) {
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;
        glm::ivec2 cp = chunk->getPosition();
        if (!isWithinUnloadRadius(cp, m_currentCenterChunk)) {
            if (chunk->isSaveDirty()) {
                saveChunkToDisk(chunk);
            }
            toRemove.push_back(cp);
        }
    }

    for (auto& cp : toRemove) {
        ChunkKey key = chunkPosToKey(cp);
        auto it = m_loadedChunks.find(key);
        if (it == m_loadedChunks.end()) continue;
        it->second->unload();  // 清理邻居指针
        m_loadedChunks.erase(it);
    }

    if (!toRemove.empty()) m_needChunkScan = true;
}

void ChunkManager::saveAllDirtyChunks() {
    if (!m_saveManager) return;
    doAutoSave();
    // 也保存 pending 中的脏 chunk
    for (auto& kv : m_pendingChunks) {
        Chunk* c = kv.second.get();
        if (c && c->isSaveDirty()) {
            saveChunkToDisk(c);
        }
    }
}
