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
#include <shared_mutex>
#include <GLFW/glfw3.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace {
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
    m_blockReady.clear();
    m_pendingBlockData.clear();
    m_pendingMeshResults.clear();
    m_activeChunks.clear();
    m_sectionSlots.clear();
    m_drawCommands.clear();
    m_inFlight.clear();
    m_meshInFlight.clear();

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
    m_retainMargin = RuntimeConfig::get().retainMarginChunks;
    m_lastSaveCheckTime = glfwGetTime();
    m_currentCenterChunk = glm::ivec2(
        (int)std::floor(cameraPos.x / Chunk::WIDTH),
        (int)std::floor(cameraPos.z / Chunk::DEPTH)
    );

    const uint32_t initialInstances = std::max<uint32_t>(1u << 16,
        (uint32_t)((2 * renderRadius + 1) * (2 * renderRadius + 1) * 1024));
    m_arena.initialize(initialInstances);

    // 预分配 hash map 容量，防止 rehash 导致 BlockReadyEntry 地址变化
    // （MeshBuildInput 持有指向 BlockReadyEntry::blocks 的原始指针）
    int maxChunks = (2 * (m_renderRadius + m_retainMargin) + 1);
    maxChunks = maxChunks * maxChunks + 64; // 留余量
    m_loadedChunks.reserve(maxChunks);
    m_blockReady.reserve(maxChunks);
    m_inFlight.reserve(maxChunks);
    m_meshInFlight.reserve(maxChunks);
    m_sectionSlots.reserve(maxChunks * Chunk::SECTION_COUNT);

    glGenBuffers(1, &m_indirectBuffer);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_indirectBuffer);
    m_indirectBufferCapacityBytes = sizeof(DrawElementsIndirectCommand) * 256;
    glBufferData(GL_DRAW_INDIRECT_BUFFER, m_indirectBufferCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);

    glGenBuffers(1, &m_sectionBaseSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_sectionBaseSSBO);
    m_sectionBaseCapacityBytes = sizeof(glm::vec4) * 256;
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_sectionBaseCapacityBytes, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // 初始化光照缓存 GPU 资源
    m_lightCache.initGL();

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

    float camDist = glm::distance(m_lastVisCameraPos, m_camera->Position);
    float YawDelta = std::abs(m_lastVisCameraYaw - m_camera->Yaw);
    float YawDelta_w = std::fmod(YawDelta, 360.0f);// 处理角度 wrap-around
    float PitchDelta = std::abs(m_lastVisCameraPitch - m_camera->Pitch);
    if (camDist > 1.0f || PitchDelta > 0.1f || (YawDelta > 1.0 && YawDelta_w > 1.0)) {
        m_lastVisCameraPos = m_camera->Position;
        m_lastVisCameraYaw = m_camera->Yaw;
        m_lastVisCameraPitch = m_camera->Pitch;
        ++m_visGeneration;
    }

    // Task 1 集成：block data 进入 BLOCK_READY，通知邻居，触发 Task 2 检查
    integrateBlockData();
    // Task 2 集成：mesh 结果进入 m_loadedChunks
    integrateMeshResults();

    if (m_needChunkScan && (int)m_inFlight.size() < m_maxInflightRequests) {
        PROFILE_SCOPE("requestMissingChunks");
        requestMissingChunks();
    }

    rebuildDrawCommands();

    if (m_saveManager) {
        double now = glfwGetTime();
        float dt = (float)(now - m_lastSaveCheckTime);
        m_lastSaveCheckTime = now;

        m_autoSaveTimer += dt;
        if (m_autoSaveTimer >= (float)m_autoSaveIntervalSec) {
            doAutoSave();
            m_autoSaveTimer = 0.0f;
        }

        // 远距离卸载降频：每 UNLOAD_CHECK_INTERVAL_SEC 扫一次即可，
        // chunk 不会在一帧内从渲染半径冲到卸载半径外（边界在 render+UNLOAD_MARGIN 之外）。
        m_unloadTimer += dt;
        if (m_unloadTimer >= UNLOAD_CHECK_INTERVAL_SEC) {
            unloadDistantChunks();
            m_unloadTimer = 0.0f;
        }
    }
}

// ============================================================================
// Chunk 查询
// ============================================================================

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
    // BLOCK_READY 中的 chunk 有方块数据但无 mesh，仍返回 nullptr（调用方需要 Chunk*）
    return nullptr;
}

// 取某个 chunk 的 16 个 section BlockBox（数据 + 锁），填入 out（每个 shared_ptr +1 引用）。
// LOADED 从各 Section 取，BLOCK_READY 从 entry 取 —— 两者都是【同一份】数据源（无第二份快照）。
// 找到返回 true。out 里持有 shared_ptr 即保证 box 在调用方使用期间不被释放。
bool ChunkManager::getChunkBoxes(const glm::ivec2& chunkPos, ChunkBoxes& out) {
    ChunkKey key = chunkPosToKey(chunkPos);
    auto itBR = m_blockReady.find(key);
    if (itBR != m_blockReady.end()) {
        out = itBR->second.boxes;  // 拷 shared_ptr 数组，引用计数 +1
        return true;
    }
    auto itLoaded = m_loadedChunks.find(key);
    if (itLoaded != m_loadedChunks.end()) {
        for (int sy = 0; sy < CHUNK_SECTION_COUNT; ++sy) {
            out[sy] = itLoaded->second->getSectionBox(sy);
        }
        return true;
    }
    return false;
}

// 仅判断某 chunk 是否有方块数据（LOADED 或 BLOCK_READY）。
bool ChunkManager::hasBlockData(const glm::ivec2& chunkPos) const {
    ChunkKey key = chunkPosToKey(chunkPos);
    return m_blockReady.find(key) != m_blockReady.end()
        || m_loadedChunks.find(key) != m_loadedChunks.end();
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

std::vector<glm::ivec2> ChunkManager::getBlockReadyChunkPositions() const {
    std::vector<glm::ivec2> ret;
    ret.reserve(m_blockReady.size());
    for (auto& kv : m_blockReady) {
        int32_t cx = static_cast<int32_t>(kv.first >> 32);
        int32_t cz = static_cast<int32_t>(kv.first & 0xFFFFFFFFLL);
        ret.emplace_back(cx, cz);
    }
    return ret;
}

void ChunkManager::setTrackPromotions(bool v) {
    m_trackPromotions = v;
    if (!v) return;
    // 把当前已有的 block-ready / loaded chunk 一次性灌入晋升队列，
    // 让开启追踪之前就晋升的 chunk 也能走增量推送路径。
    m_promotedChunks.clear();
    m_promotedChunks.reserve(m_blockReady.size() + m_loadedChunks.size());
    for (auto& kv : m_blockReady) {
        int32_t cx = static_cast<int32_t>(kv.first >> 32);
        int32_t cz = static_cast<int32_t>(kv.first & 0xFFFFFFFFLL);
        m_promotedChunks.emplace_back(cx, cz);
    }
    for (auto& kv : m_loadedChunks) {
        m_promotedChunks.push_back(kv.second->getPosition());
    }
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
    std::cout << "Block-Ready: " << getBlockReadyCount() << std::endl;
    std::cout << "Mesh In-Flight: " << getMeshInFlightCount() << std::endl;
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

bool ChunkManager::isDataRelevant(const glm::ivec2& chunkPos, int margin) const {
    // 加载中心为空（客户端/单机）→ 退化为只看本机相机中心 + 渲染半径
    if (m_loadCenters.empty()) {
        int r = m_renderRadius + margin;
        int dx = std::abs(chunkPos.x - m_currentCenterChunk.x);
        int dz = std::abs(chunkPos.y - m_currentCenterChunk.y);
        return dx <= r && dz <= r;
    }
    // per-center：每个中心用自己的半径 + margin
    for (const auto& c : m_loadCenters) {
        int r = c.radius + margin;
        int dx = std::abs(chunkPos.x - c.center.x);
        int dz = std::abs(chunkPos.y - c.center.y);
        if (dx <= r && dz <= r) return true;
    }
    return false;
}

void ChunkManager::updateActiveChunks(const glm::vec3& cameraPos) {
    m_activeChunks.clear();
    m_drawListDirty = true;  // active 集合变化 → draw list 需重建

    for (int dx = -m_renderRadius; dx <= m_renderRadius; ++dx) {
        for (int dz = -m_renderRadius; dz <= m_renderRadius; ++dz) {
            glm::ivec2 chunkPos(m_currentCenterChunk.x + dx, m_currentCenterChunk.y + dz);
            if (Chunk* chunk = getChunk(chunkPos)) {
                m_activeChunks.push_back(chunk);
            }
        }
    }

    // 重新检查 m_blockReady 中进入活跃半径的区块，尝试投递 mesh
    // （玩家移动到新位置后，之前为远程客户端生成而跳过 mesh 的区块可能需要 mesh 了）
    for (auto& [key, entry] : m_blockReady) {
        int cx = (int32_t)(key >> 32);
        int cz = (int32_t)(key & 0xFFFFFFFFLL);
        glm::ivec2 cp(cx, cz);
        if (isWithinActiveRadius(cp, m_currentCenterChunk)) {
            checkAndSubmitMesh(cp);
        }
    }

    if ((int)m_inFlight.size() < m_maxInflightRequests) {
        requestMissingChunks();
    }

    evictFarChunkSlots();
}

void ChunkManager::evictFarChunkSlots() {
    PROFILE_SCOPE("evictFarChunkSlots");
    for (auto& kv : m_loadedChunks) {
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;
        glm::ivec2 cp = chunk->getPosition();
        if (isWithinEvictRadius(cp, m_currentCenterChunk)) continue;

        for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
            Section& sec = chunk->getSection(sy);
            ChunkArena::Slot slot = sec.getGpuSlot();
            if (!slot.valid()) continue;
            m_arena.free(slot);
            SectionKey key = makeSectionKey(cp.x, cp.y, sy);
            m_sectionSlots.erase(key);
            sec.notifyGpuSlotReleased();  // 内部置 m_dirty，重入界时强制全量重传
            chunk->markSectionDirty(sy);  // 同步脏掩码，重入 active 时 uploadPass 才会重传
            m_drawListDirty = true;  // 该 section 不再有 GPU slot → draw list 需重建
        }
    }
}

// ============================================================================
// Task 1 投递：requestMissingChunks / requestChunkLoad / forceChunkLoad / importChunkData
// ============================================================================

void ChunkManager::requestMissingChunks() {
    // 网络客户端：清理超时的 in-flight 条目，避免僵尸请求占满槽位
    if (m_networkClient) {
        double now = glfwGetTime();
        for (auto it = m_inFlight.begin(); it != m_inFlight.end(); ) {
            if (now - it->second > INFLIGHT_TIMEOUT_SEC) {
                int cx = (int32_t)(it->first >> 32);
                int cz = (int32_t)(it->first & 0xFFFFFFFFLL);
                printf("[ChunkManager] inflight timeout for (%d,%d) after %.1fs, re-requesting\n",
                    cx, cz, now - it->second);
                it = m_inFlight.erase(it);
            } else {
                ++it;
            }
        }
    }

    // 所有模式：踢出"玩家已跑远"的在途请求（超出 render + retain）。
    // 这些 chunk 即使生成回来也会被卸载逻辑回收，提前从 m_inFlight 移除可腾出槽位
    // 给真正需要的 chunk。用 retain 半径（而非 data 半径）做阈值，避免温存区边界抖动
    // 反复踢/补。注意：被踢后 worker 仍会算完并进 m_blockDone，主线程 drain 时若仍不相关
    // 会进 block-ready 由卸载逻辑统一处理（生成成本已发生，不浪费数据）。
    for (auto it = m_inFlight.begin(); it != m_inFlight.end(); ) {
        int cx = (int32_t)(it->first >> 32);
        int cz = (int32_t)(it->first & 0xFFFFFFFFLL);
        if (!isDataRelevant(glm::ivec2(cx, cz), m_retainMargin)) {
            it = m_inFlight.erase(it);
        } else {
            ++it;
        }
    }

    // 网络客户端：通过回调发送 CHUNK_REQUEST
    if (m_networkClient) {
        if (!m_onRequestChunk) return;
        const int loadR = m_renderRadius + DATA_MARGIN;
        bool foundMissing = false;
        for (int r = 0; r <= loadR; ++r) {
            if ((int)m_inFlight.size() >= m_maxInflightRequests) return;
            for (int dx = -r; dx <= r; ++dx) {
                int absDx = std::abs(dx);
                for (int dz = -r; dz <= r; ++dz) {
                    if (std::max(absDx, std::abs(dz)) != r) continue;
                    glm::ivec2 chunkPos(m_currentCenterChunk.x + dx, m_currentCenterChunk.y + dz);
                    if (getChunk(chunkPos)) continue;
                    ChunkKey key = chunkPosToKey(chunkPos);
                    if (m_blockReady.find(key) != m_blockReady.end()) continue;
                    if (m_inFlight.find(key) != m_inFlight.end()) continue;
                    if ((int)m_inFlight.size() >= m_maxInflightRequests) return;
                    foundMissing = true;
                    m_inFlight[key] = glfwGetTime();
                    m_onRequestChunk(chunkPos.x, chunkPos.y);
                }
            }
        }
        if (!foundMissing) m_needChunkScan = false;
        return;
    }

    // 本地模式（含 Host）：投递 Task 1（方块数据生成/加载）。
    // 阶段 B + per-center：围绕所有加载中心（Host + 远程玩家，各带自己的半径）补数据。
    //
    // 投递顺序 = 全局"由近及远"：外层环 r 从 0 递增，对每个 r 只处理"r 落在自己半径内"
    // 的中心的那一环。这样：
    //  - 整体上每个中心的近环都先于任何中心的远环投递（公平、玩家附近优先填满）；
    //  - 半径大的玩家的远环在大 r 时仍会被投递，不会被半径小的玩家饿死（其 ring 早已停发）。
    // 三道 guard（getChunk / m_blockReady / m_inFlight）天然去重，多中心重叠不会重复投递。

    // 加载中心列表：m_loadCenters 为空（单机/初始化）则退化为本机相机中心 + 本机渲染半径
    std::vector<LoadCenter> centers = m_loadCenters;
    if (centers.empty()) centers.push_back({ m_currentCenterChunk, m_renderRadius });

    // 全局最大环上界 = 各中心 (radius + DATA_MARGIN) 的最大值
    int maxLoadR = 0;
    for (const auto& c : centers) maxLoadR = std::max(maxLoadR, c.radius + DATA_MARGIN);

    bool foundMissing = false;
    for (int r = 0; r <= maxLoadR; ++r) {
        if ((int)m_inFlight.size() >= m_maxInflightRequests) {
            m_needChunkScan = true;  // 槽位满，下帧继续
            return;
        }
        for (const auto& c : centers) {
            const int centerLoadR = c.radius + DATA_MARGIN;
            if (r > centerLoadR) continue;   // 该中心半径已扫完，跳过它的这一远环
            for (int dx = -r; dx <= r; ++dx) {
                int absDx = std::abs(dx);
                for (int dz = -r; dz <= r; ++dz) {
                    if (std::max(absDx, std::abs(dz)) != r) continue;
                    glm::ivec2 chunkPos(c.center.x + dx, c.center.y + dz);
                    ChunkKey key = chunkPosToKey(chunkPos);
                    if (getChunk(chunkPos)) continue;
                    if (m_blockReady.find(key) != m_blockReady.end()) continue;
                    if (m_inFlight.find(key) != m_inFlight.end()) continue;
                    if ((int)m_inFlight.size() >= m_maxInflightRequests) {
                        m_needChunkScan = true;
                        return;
                    }
                    foundMissing = true;
                    requestChunkLoad(chunkPos);
                }
            }
        }
    }
    if (!foundMissing) m_needChunkScan = false;
}

void ChunkManager::requestChunkLoad(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) return;
    if (m_blockReady.find(key) != m_blockReady.end()) return;
    if (m_inFlight.find(key) != m_inFlight.end()) return;
    m_inFlight[key] = glfwGetTime();
    m_workerPool.submitBuild(chunkPos);
}

void ChunkManager::forceChunkLoad(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) return;
    if (m_blockReady.find(key) != m_blockReady.end()) return;
    if (m_inFlight.find(key) != m_inFlight.end()) return;
    m_inFlight[key] = glfwGetTime();
    m_workerPool.submitBuild(chunkPos);
}

void ChunkManager::submitNetworkChunkImport(int chunkX, int chunkZ,
                                            std::vector<uint8_t>&& serializedChunk) {
    glm::ivec2 pos(chunkX, chunkZ);
    ChunkKey key = chunkPosToKey(pos);

    // 已就绪则不必再投递（worker 解压是浪费）。整批晋升后的二次到达在这里就被挡掉；
    // 即便漏挡，integrateBlockData 也会再做一次同样的 dup-check（line ~559）。
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) return;
    if (m_blockReady.find(key) != m_blockReady.end()) return;

    // 标记 in-flight（与本地生成的 JOB_BUILD 共用 m_inFlight 语义）。worker 解压完产出
    // BlockDataResult 进 block 完成队列，integrateBlockData 会 erase 掉这个标记。
    m_inFlight[key] = glfwGetTime();
    m_workerPool.submitNetImport(chunkX, chunkZ, std::move(serializedChunk));
}

// ============================================================================
// Task 1 集成：BlockDataResult → BLOCK_READY
// ============================================================================

void ChunkManager::integrateBlockData() {
    PROFILE_SCOPE("integrateBlockData");
    {
        PROFILE_SCOPE("ibd.drain");
        auto results = m_workerPool.drainBlockData();
        if (!results.empty()) {
            Profiler::addCounter("ibd.resultCount", (int64_t)results.size());
            for (auto& r : results) m_pendingBlockData.push_back(std::move(r));
        }
    }
    if (m_pendingBlockData.empty()) return;

    int budget = MAX_BLOCK_INTEGRATE_PER_FRAME;
    while (budget > 0 && !m_pendingBlockData.empty()) {
        auto r = std::move(m_pendingBlockData.front());
        m_pendingBlockData.pop_front();

        ChunkKey key = chunkPosToKey(r->pos);
        m_inFlight.erase(key);

        // 已存在（loaded 或 block-ready）→ 跳过
        if (m_loadedChunks.find(key) != m_loadedChunks.end()) continue;
        if (m_blockReady.find(key) != m_blockReady.end()) continue;

        // ── 注册 Task 1 worker 扫描到的发光方块到光源注册表 ──
        auto& reg = LightSourceRegistry::instance();
        for (const auto& src : r->lightSources) {
            reg.addLight(src.pos, src);
        }

        // 进入 BLOCK_READY（直接 move worker 产出的 16 个 box + 区块内光照，零拷贝）
        BlockReadyEntry entry;
        entry.boxes = std::move(r->boxes);
        entry.lightSources = std::move(r->lightSources);
        entry.sectionLightData = std::move(r->sectionLightData);
        m_blockReady[key] = std::move(entry);

        notePromotedChunk(r->pos);
        notifyNeighborsBlockReady(r->pos);
        checkAndSubmitMesh(r->pos);

        --budget;
    }
    Profiler::addCounter("ibd.queueDepth", (int64_t)m_pendingBlockData.size());
}

// ============================================================================
// 邻居标记 & Task 2 触发
// ============================================================================

void ChunkManager::notifyNeighborsBlockReady(const glm::ivec2& pos) {
    // 4 方向邻居偏移
    static const glm::ivec2 offsets[4] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }  // +X, -X, +Z, -Z
    };
    // 邻居视角下的"我"的方向：oppDir[d]
    static const int oppDir[4] = { 1, 0, 3, 2 };

    for (int d = 0; d < 4; ++d) {
        glm::ivec2 nbPos = pos + offsets[d];
        ChunkKey nbKey = chunkPosToKey(nbPos);

        // 邻居在 BLOCK_READY 中 → 标记它"我这一侧已就绪"
        auto itBR = m_blockReady.find(nbKey);
        if (itBR != m_blockReady.end()) {
            itBR->second.neighborBlockReady |= (uint8_t)(1u << oppDir[d]);
            // 检查邻居是否可投递 Task 2
            checkAndSubmitMesh(nbPos);
        }

        // 邻居在 LOADED 中 → 无需操作（loaded 不需要 mesh rebuild）
    }

    // 更新自身的 neighborBlockReady：扫 4 方向，如果邻居有方块数据则置位
    auto itSelf = m_blockReady.find(chunkPosToKey(pos));
    if (itSelf != m_blockReady.end()) {
        for (int d = 0; d < 4; ++d) {
            glm::ivec2 nbPos = pos + offsets[d];
            if (hasBlockData(nbPos)) {
                itSelf->second.neighborBlockReady |= (uint8_t)(1u << d);
            }
        }
    }
}

void ChunkManager::checkAndSubmitMesh(const glm::ivec2& pos) {
    // 服务端模式：只对活跃半径内的区块投递 mesh
    // 为远程客户端生成的区块只需停留在 m_blockReady，客户端拿到数据后自行 mesh
    if (!m_networkClient && !isWithinActiveRadius(pos, m_currentCenterChunk)) return;

    ChunkKey key = chunkPosToKey(pos);

    // 自身必须在 BLOCK_READY
    auto it = m_blockReady.find(key);
    if (it == m_blockReady.end()) return;

    // 已 loaded（stale BlockReadyEntry 尚未清理）→ 不需要重复 mesh
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) return;

    // 已在 mesh 投递中
    if (m_meshInFlight.find(key) != m_meshInFlight.end()) return;

    // 检查 4 方向邻居是否都有方块数据
    static const glm::ivec2 offsets[4] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };

    for (int d = 0; d < 4; ++d) {
        if (!(it->second.neighborBlockReady & (uint8_t)(1u << d))) {
            // 这个方向还没就绪，double-check
            if (hasBlockData(pos + offsets[d])) {
                it->second.neighborBlockReady |= (uint8_t)(1u << d);
            } else {
                return; // 有方向未就绪，不能投递
            }
        }
    }

    // 所有 4 方向 + 自身都就绪 → 投递 Task 2
    submitMeshTask(pos);
}

// ── 边界光照层提取辅助函数 ─────────────────────────────────────────
// 从 sectionLightData（Task 1 区块内 BFS 结果，4096 uint32_t）中提取指定方向
// 的边界一层（16×16=256 个 uint32_t），用于传递给 Task 2 做跨边界 BFS。
// 方向约定（与 MeshBuildInput::neighborBoundaryLight 一致）：
//   d=0 (+X): 提取 x=0 面（邻居 LEFT 面）→ 布局 [y*16+z]
//   d=1 (-X): 提取 x=15 面（邻居 RIGHT 面）→ 布局 [y*16+z]
//   d=2 (+Z): 提取 z=0 面（邻居 BACK 面）→ 布局 [y*16+x]
//   d=3 (-Z): 提取 z=15 面（邻居 FRONT 面）→ 布局 [y*16+x]

namespace {
constexpr int BW = 16; // boundary width/height

void extractBoundaryLightLayer(const std::shared_ptr<SectionLightData>& sectionLight,
                                int sy, int dir, std::vector<uint32_t>& out) {
    out.clear();
    if (!sectionLight) return;
    out.resize(BW * BW, 0u);
    const auto& data = *sectionLight;

    for (int y = 0; y < BW; ++y) {
        if (dir <= 1) {
            int lx = (dir == 0) ? 0 : 15;
            for (int z = 0; z < BW; ++z) {
                int idx = (y * BW + z) * BW + lx;
                out[y * BW + z] = data[idx];
            }
        } else {
            int lz = (dir == 2) ? 0 : 15;
            for (int x = 0; x < BW; ++x) {
                int idx = (y * BW + lz) * BW + x;
                out[y * BW + x] = data[idx];
            }
        }
    }
}

void extractBoundaryLightFromCache(const SectionLightCache* cache,
                                     int sy, int dir, std::vector<uint32_t>& out) {
    out.clear();
    if (!cache || !cache->hasAnyLight()) return;
    out.resize(BW * BW, 0u);
    const uint32_t* raw = cache->rawData();

    for (int y = 0; y < BW; ++y) {
        if (dir <= 1) {
            int lx = (dir == 0) ? 0 : 15;
            for (int z = 0; z < BW; ++z) {
                int idx = (y * BW + z) * BW + lx;
                out[y * BW + z] = raw[idx];
            }
        } else {
            int lz = (dir == 2) ? 0 : 15;
            for (int x = 0; x < BW; ++x) {
                int idx = (y * BW + lz) * BW + x;
                out[y * BW + x] = raw[idx];
            }
        }
    }
}
} // namespace

void ChunkManager::submitMeshTask(const glm::ivec2& pos) {
    ChunkKey key = chunkPosToKey(pos);
    auto it = m_blockReady.find(key);
    if (it == m_blockReady.end()) return;
    if (m_meshInFlight.find(key) != m_meshInFlight.end()) return;

    constexpr int SEC_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;

    // 构建 MeshBuildInput
    MeshBuildInput input;
    input.pos = pos;
    // self：直接共享 block-ready entry 的 16 个 box（worker 把它们装入生成的 Section）
    input.self = it->second.boxes;

    // self 光照：move Task 1 产出的区块内 BFS 结果
    input.selfLightData = std::move(it->second.sectionLightData);

    static const glm::ivec2 offsets[4] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };
    // 邻居：拷出 16 个 box 的 shared_ptr（引用计数 +1）。即使邻居随后被卸载，
    // worker 持有的这份 shared_ptr 也保证 box 存活到 mesh 读取结束，不会悬挂。
    for (int d = 0; d < 4; ++d) {
        getChunkBoxes(pos + offsets[d], input.neighbors[d]);
    }

    // ── 提取 4 邻居的边界光照层 ──
    // 优先从 block-ready（Task 1 完成的区块内 BFS），其次从 loaded chunk 的光照缓存。
    for (int d = 0; d < 4; ++d) {
        glm::ivec2 nbPos = pos + offsets[d];
        ChunkKey nbKey = chunkPosToKey(nbPos);

        // 尝试从 block-ready entry 读取
        auto itBR = m_blockReady.find(nbKey);
        if (itBR != m_blockReady.end()) {
            for (int sy = 0; sy < SEC_COUNT; ++sy) {
                extractBoundaryLightLayer(itBR->second.sectionLightData[sy], sy, d,
                                           input.neighborBoundaryLight[d][sy]);
            }
            continue;
        }

        // 尝试从已加载 chunk 的光照缓存读取
        auto itLoaded = m_loadedChunks.find(nbKey);
        if (itLoaded != m_loadedChunks.end()) {
            for (int sy = 0; sy < SEC_COUNT; ++sy) {
                SectionKey secKey = makeSectionKey(nbPos.x, nbPos.y, sy);
                const SectionLightCache* cache = m_lightCache.get(secKey);
                if (cache && cache->hasAnyLight()) {
                    extractBoundaryLightFromCache(cache, sy, d,
                                                   input.neighborBoundaryLight[d][sy]);
                }
            }
        }
    }

    m_meshInFlight.insert(key);
    m_workerPool.submitMeshBuild(input);
}

// ============================================================================
// Task 2 集成：ChunkBuildResult → m_loadedChunks
// ============================================================================

void ChunkManager::integrateMeshResults() {
    PROFILE_SCOPE("integrateMeshResults");
    {
        PROFILE_SCOPE("imr.drain");
        auto results = m_workerPool.drainMeshResults();
        if (!results.empty()) {
            Profiler::addCounter("imr.resultCount", (int64_t)results.size());
            for (auto& r : results) m_pendingMeshResults.push_back(std::move(r));
        }
    }
    if (m_pendingMeshResults.empty()) return;

    int budget = MAX_MESH_INTEGRATE_PER_FRAME;
    while (budget > 0 && !m_pendingMeshResults.empty()) {
        auto r = std::move(m_pendingMeshResults.front());
        m_pendingMeshResults.pop_front();

        ChunkKey key = chunkPosToKey(r->pos);
        m_meshInFlight.erase(key);

        if (m_loadedChunks.find(key) != m_loadedChunks.end()) continue;

        loadMeshResult(*r);

        --budget;
    }
    Profiler::addCounter("imr.queueDepth", (int64_t)m_pendingMeshResults.size());
}

void ChunkManager::loadMeshResult(ChunkBuildResult& result) {
    glm::ivec2 pos = result.pos;
    ChunkKey key = chunkPosToKey(pos);

    auto chunk = std::make_unique<Chunk>(pos, this);
    // adoptSections 把 worker 产出的 Section（已持有 self 的 BlockBox）move 进 chunk。
    // 方块数据的所有权从 block-ready entry 经 worker 一路 move 到这里，全程同一份 box。
    chunk->adoptSections(std::move(result.sections));

    // block-ready entry 的使命完成，清除（它持有的 box shared_ptr 释放，引用计数交给 Section）
    m_blockReady.erase(key);

    // 若该 chunk 在 block-ready 期间被网络改动过（待存盘），把脏标记交接给新 Chunk，
    // 由 loaded chunk 的常规存盘路径接管。
    if (m_blockReadyDirty.erase(key) > 0) {
        chunk->markSaveDirty();
    }

    Chunk* raw = chunk.get();
    m_loadedChunks[key] = std::move(chunk);

    // 记录晋升事件，供服务端增量推送（替代每帧全量扫描）。
    // loaded 数据比 block-ready 更"新"（含玩家/网络改动），所以晋升到 loaded 时
    // 也重新入队，pushChunks 会优先用 loaded 序列化覆盖之前推过的 block-ready 版本。
    notePromotedChunk(pos);

    // 连通邻居指针
    linkNeighbors(raw);

    // ── 应用 Task 2 产出的完整光照数据到 LightCacheManager ──
    // sectionLightData 为 shared_ptr<SectionLightData>，直接 memcpy 到 LightCache。
    bool chunkHasAnyLight = false;
    for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
        if (!result.sectionLightData[sy]) continue;
        SectionKey secKey = makeSectionKey(pos.x, pos.y, sy);
        SectionLightCache* cache = m_lightCache.getOrCreate(secKey);
        cache->writeRawData(result.sectionLightData[sy]->data());
        cache->setHasLight(true);
        cache->dirty = true;
        chunkHasAnyLight = true;
    }

    // 在活跃半径内 → 加入 active 列表
    if (isWithinActiveRadius(pos, m_currentCenterChunk)) {
        m_activeChunks.push_back(raw);
        m_drawListDirty = true;  // 新 chunk 进入 active → draw list 需重建
    }

    // ── 通知已加载邻居：新 chunk 的光照可能跨边界影响它们 ──
    // 触发增量重传播使邻居边界光照保持正确。
    if (chunkHasAnyLight) {
        for (int d = 0; d < 4; ++d) {
            Chunk* nb = raw->m_neighbors[d];
            if (nb && nb->isMeshReady()) {
                // 在边界取一个代表位置，触发增量更新
                // 该位置周围的 propagateRegion 会从 LightSourceRegistry（含本 chunk 的光源）重传播
                glm::ivec2 nbPos = nb->getPosition();
                int midY = Chunk::HEIGHT / 2;
                int bx = pos.x * Chunk::WIDTH;
                int bz = pos.y * Chunk::DEPTH;
                // 选取本 chunk 边界中间的一个点
                if (d == 0)      notifyLightChange(glm::ivec3(bx + Chunk::WIDTH - 1, midY, bz + Chunk::DEPTH / 2));
                else if (d == 1) notifyLightChange(glm::ivec3(bx, midY, bz + Chunk::DEPTH / 2));
                else if (d == 2) notifyLightChange(glm::ivec3(bx + Chunk::WIDTH / 2, midY, bz + Chunk::DEPTH - 1));
                else             notifyLightChange(glm::ivec3(bx + Chunk::WIDTH / 2, midY, bz));
            }
        }
    }

    // 新 loaded chunk 出现 → 重新检查周围 BLOCK_READY chunk 是否可投递 Task 2
    // （它们之前可能缺这个方向）
    static const glm::ivec2 offsets[4] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };
    for (int d = 0; d < 4; ++d) {
        glm::ivec2 nbPos = pos + offsets[d];
        ChunkKey nbKey = chunkPosToKey(nbPos);
        auto itNbrBR = m_blockReady.find(nbKey);
        if (itNbrBR != m_blockReady.end()) {
            // 标记该方向已就绪
            static const int oppDir[4] = { 1, 0, 3, 2 };
            itNbrBR->second.neighborBlockReady |= (uint8_t)(1u << oppDir[d]);
            checkAndSubmitMesh(nbPos);
        }
    }
}

void ChunkManager::linkNeighbors(Chunk* newChunk) {
    glm::ivec2 p = newChunk->getPosition();

    auto findLoaded = [this](const glm::ivec2& q) -> Chunk* {
        return getChunk(q);
    };

    Chunk* xp = findLoaded(glm::ivec2(p.x + 1, p.y));
    Chunk* xn = findLoaded(glm::ivec2(p.x - 1, p.y));
    Chunk* zp = findLoaded(glm::ivec2(p.x, p.y + 1));
    Chunk* zn = findLoaded(glm::ivec2(p.x, p.y - 1));

    newChunk->m_neighbors[0] = xp;
    newChunk->m_neighbors[1] = xn;
    newChunk->m_neighbors[2] = zp;
    newChunk->m_neighbors[3] = zn;

    if (xp) xp->m_neighbors[1] = newChunk;
    if (xn) xn->m_neighbors[0] = newChunk;
    if (zp) zp->m_neighbors[3] = newChunk;
    if (zn) zn->m_neighbors[2] = newChunk;
}

// ============================================================================
// 渲染指令构建 & GPU 上传
// ============================================================================

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

    int uploadBudget = m_maxUploadsPerFrame;
    int uploadedCount = 0;

    {
        PROFILE_SCOPE("rdc.uploadPass");
        // 只遍历 active chunk（渲染半径内、会被实际绘制的）。原先遍历整个 m_loadedChunks
        // （范围到卸载半径，约 2000+ chunk × 16 section）每帧空扫。
        // 进一步：用 chunk 的脏掩码（getDirtySectionMask）跳过整块干净 chunk，
        // 稳态零脏时本 pass 每个 chunk 只做一次位运算判 0，整体接近 0 开销。
        // 非 active 的脏 section 不会被绘制，dirty 标记+掩码位都保留，待 chunk 进入
        // active 半径后由本 pass 上传，仅延迟 1 帧，肉眼无感。
        for (Chunk* chunk : m_activeChunks) {
            uint32_t dirtyMask = chunk->getDirtySectionMask();
            if (dirtyMask == 0) continue;          // 整块干净 → 一次位运算跳过
            if (!chunk->isMeshReady()) continue;
            glm::ivec2 cp = chunk->getPosition();
            while (dirtyMask) {
                int sy = lowestBitIndex(dirtyMask);
                dirtyMask &= dirtyMask - 1u;
                Section& s = chunk->getSection(sy);
                if (!s.isDirty()) {                 // 掩码是保守超集，实际已干净 → 清位
                    chunk->clearSectionDirtyBit(sy);
                    continue;
                }
                int before = uploadBudget;
                uploadSection(cp.x, cp.y, sy, s, uploadBudget);
                if (before != uploadBudget) {
                    ++uploadedCount;
                    chunk->clearSectionDirtyBit(sy);  // 上传成功 → 清掩码位
                }
                if (uploadBudget <= 0) break;
            }
            if (uploadBudget <= 0) break;
        }
    }
    Profiler::addCounter("rdc.uploadCount", uploadedCount);

    // 有 section 上传 → 可见 slot 的 count/offset 可能变了，draw list 需重建。
    if (uploadedCount > 0) m_drawListDirty = true;

    // draw command 缓存：相机未移动（visGeneration 不变）且可见集合未变（!dirty）时，
    // m_drawCommands / m_sectionBases / GPU buffer 与上一帧完全相同，直接复用，
    // 跳过遍历全部 active chunk 的 cull pass 与 GPU 重传。这是稳态静止帧的主要省时点。
    if (!m_drawListDirty && m_visGeneration == m_lastBuiltVisGeneration) {
        // 复用上一帧结果：m_visibleInstanceCount / m_drawCommands 保持不变。
        Profiler::addCounter("rdc.drawCmdCount", (int64_t)m_drawCommands.size());
        Profiler::addCounter("rdc.visibleInstances", (int64_t)m_visibleInstanceCount);
        Profiler::addCounter("rdc.rebuildSkipped", 1);
        return;
    }
    Profiler::addCounter("rdc.rebuildSkipped", 0);

    m_drawCommands.clear();
    m_sectionBases.clear();
    m_visibleInstanceCount = 0;

    {
        PROFILE_SCOPE("rdc.cullPass");
        const auto& cfg = RuntimeConfig::get();
        const bool detailed = cfg.profileDetailed;  // 细分计时/计数总开关（默认关 → 零开销）
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

            // 细分计时仅在 detailed 时调 steady_clock::now()（否则每 chunk 4 次时钟读是纯开销）
            std::chrono::steady_clock::time_point t0, t1, t2, t3;
            if (detailed) t0 = std::chrono::steady_clock::now();
            bool visible = chunk->isChunkPotentiallyVisible(cam);
            if (detailed) {
                t1 = std::chrono::steady_clock::now();
                coarseVisUs += std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            }
            if (!visible) { ++chunkCoarseCull; continue; }

            uint32_t mask = chunk->getVisibleSectionMask(cam, cameraSectionY, maxDownSections, pPlanes, detailed);
            if (detailed) {
                t2 = std::chrono::steady_clock::now();
                getMaskUs += std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();
            }

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
            if (detailed) {
                t3 = std::chrono::steady_clock::now();
                emitCmdUs += std::chrono::duration_cast<std::chrono::microseconds>(t3 - t2).count();
            }
        }

        // 细分计时/计数仅在 detailed 开启时输出（显式指定父为 "rdc.cullPass" 以缩进）。
        if (detailed) {
            Profiler::addSample("rdc.cull.coarseVis", "rdc.cullPass", coarseVisUs);
            Profiler::addSample("rdc.cull.getMask", "rdc.cullPass", getMaskUs);
            Profiler::addSample("rdc.cull.emitCmd", "rdc.cullPass", emitCmdUs);
            Profiler::addCounter("rdc.chunkTotal", chunkTotal);
            Profiler::addCounter("rdc.chunkCoarseCull", chunkCoarseCull);
        }
    }

    Profiler::addCounter("rdc.drawCmdCount", (int64_t)m_drawCommands.size());
    Profiler::addCounter("rdc.visibleInstances", (int64_t)m_visibleInstanceCount);

    {
        PROFILE_SCOPE("rdc.syncGpuBuffers");
        syncIndirectBuffer();
        syncSectionBaseSSBO();
    }

    // 重建完成：记录本次基于的 visGeneration，清除脏标记。
    m_lastBuiltVisGeneration = m_visGeneration;
    m_drawListDirty = false;
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

// ============================================================================
// 方块操作
// ============================================================================

bool ChunkManager::setBlock(const glm::ivec3& worldPos, BlockState state) {
    // 网络会话中：用户发起的方块修改不直接本地生效，而是交给 sink
    // （客户端发请求 / 服务端权威应用+广播）。非会话（单机）时 sink 为空，直接本地应用。
    // 注意：sink 内部最终通过 applyBlockChange→applyLocalSetBlock 落地，不会回到本函数，
    // 因此无递归。
    if (m_blockChangeSink) {
        m_blockChangeSink(worldPos, state);
        return true;
    }
    return applyLocalSetBlock(worldPos, state);
}

bool ChunkManager::applyLocalSetBlock(const glm::ivec3& worldPos, BlockState state) {
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

    // 读取旧方块状态（用于光源变动检测）
    BlockState oldState = chunk->getBlock(localPos.x, localPos.y, localPos.z);

    chunk->setBlockAndUpdate(localPos.x, localPos.y, localPos.z, state);

    // ── 光源注册：方块变动时直接更新光源注册表 ──
    if (oldState.type() != state.type()) {
        auto& reg = LightSourceRegistry::instance();

        // 旧方块是发光方块 → 移除光源
        BlockProperties oldProps = GetBlockProperties(oldState.type());
        if (oldProps.emissive > 0.0f) {
            reg.removeLight(worldPos);
        }

        // 新方块是发光方块 → 添加光源
        BlockProperties newProps = GetBlockProperties(state.type());
        if (newProps.emissive > 0.0f) {
            LightSource src = getBlockLightDef(state.type(), worldPos);
            if (src.intensity > 0.0f) {
                reg.addLight(worldPos, src);
            }
        }

        // 触发增量光照重传播
        notifyLightChange(worldPos);

        // 通知其他子系统（存档标记等）
        if (m_onBlockChanged) {
            m_onBlockChanged(worldPos, oldState, state);
        }
    }

    return true;
}

BlockState ChunkManager::getBlockAt(const glm::ivec3& worldPos) {
    glm::ivec2 chunkPos(
        (int)std::floor(worldPos.x / (float)Chunk::WIDTH),
        (int)std::floor(worldPos.z / (float)Chunk::DEPTH)
    );
    if (worldPos.y < 0 || worldPos.y >= Chunk::HEIGHT) return BlockState{};

    int lx = ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH;
    int lz = ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH;
    int sy = worldPos.y / Section::HEIGHT;   // 所属 section
    int ly = worldPos.y % Section::HEIGHT;   // section 内局部 y

    // 查询 loaded 或 block-ready 中的方块数据（碰撞检测需要 block-ready 的方块）。
    // 主线程内串行访问，与玩家修改不并发；与 worker 的读锁是读读共享，无需加锁。
    ChunkKey key = chunkPosToKey(chunkPos);
    auto itBR = m_blockReady.find(key);
    if (itBR != m_blockReady.end()) {
        const auto& box = itBR->second.boxes[sy];
        if (!box) return BlockState{};
        return box->blocks[(ly * Chunk::DEPTH + lz) * Chunk::WIDTH + lx];
    }
    auto itLoaded = m_loadedChunks.find(key);
    if (itLoaded != m_loadedChunks.end()) {
        return itLoaded->second->getBlock(lx, worldPos.y, lz);
    }
    return BlockState{};
}

bool ChunkManager::applyBlockChange(const glm::ivec3& worldPos, BlockState state) {
    glm::ivec2 chunkPos(
        (int)std::floor(worldPos.x / (float)Chunk::WIDTH),
        (int)std::floor(worldPos.z / (float)Chunk::DEPTH)
    );
    if (worldPos.y < 0 || worldPos.y >= Chunk::HEIGHT) return false;

    ChunkKey key = chunkPosToKey(chunkPos);

    // 1. LOADED：走完整本地应用路径（改面 + 标脏 + GPU 增量 patch）。
    //    注意用 applyLocalSetBlock 而非 setBlock，避免再次进入 sink 造成回环。
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) {
        return applyLocalSetBlock(worldPos, state);
    }

    // 2. BLOCK_READY：没有 mesh，直接改对应 section 的 box
    auto itBR = m_blockReady.find(key);
    if (itBR != m_blockReady.end()) {
        int lx = ((worldPos.x % Chunk::WIDTH) + Chunk::WIDTH) % Chunk::WIDTH;
        int lz = ((worldPos.z % Chunk::DEPTH) + Chunk::DEPTH) % Chunk::DEPTH;
        int sy = worldPos.y / Section::HEIGHT;
        int ly = worldPos.y % Section::HEIGHT;
        auto& box = itBR->second.boxes[sy];
        if (!box) return false;
        {
            std::unique_lock<std::shared_mutex> lk(box->mutex);
            box->blocks[(ly * Chunk::DEPTH + lz) * Chunk::WIDTH + lx] = state;
        }
        // 仅服务端需要持久化；客户端 m_saveManager 为空，记了也不会落盘。
        if (m_saveManager) m_blockReadyDirty.insert(key);
        return true;
    }

    // 3. 不存在：丢弃（正常情况下服务端不会发给未加载该 chunk 的客户端）
    return false;
}

// ============================================================================
// 存档
// ============================================================================

void ChunkManager::setSaveManager(ChunkSaveManager* sm) {
    m_saveManager = sm;
    m_workerPool.setSaveManager(sm);
}


void ChunkManager::saveChunkToDisk(Chunk* chunk) {
    if (!m_saveManager || !chunk) return;

    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    constexpr int W = Chunk::WIDTH;
    constexpr int D = Chunk::DEPTH;

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
    saveDirtyBlockReadyChunks();
}

void ChunkManager::saveBlockReadyChunkToDisk(ChunkKey key, const ChunkBoxes& boxes) {
    if (!m_saveManager) return;
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    constexpr int W = Chunk::WIDTH;
    constexpr int D = Chunk::DEPTH;
    auto buf = std::make_unique<BlockState[]>(VOL);
    BlockState* dst = buf.get();

    for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
        int baseY = sy * Section::HEIGHT;
        const auto& box = boxes[sy];
        if (!box) {
            // 该 section 无数据：填空气
            for (int y = 0; y < Section::HEIGHT; ++y)
                for (int z = 0; z < D; ++z)
                    for (int x = 0; x < W; ++x)
                        dst[((baseY + y) * D + z) * W + x] = BlockState{};
            continue;
        }
        std::shared_lock<std::shared_mutex> lk(box->mutex);
        const BlockState* src = box->blocks.data();
        for (int y = 0; y < Section::HEIGHT; ++y)
            for (int z = 0; z < D; ++z)
                for (int x = 0; x < W; ++x)
                    dst[((baseY + y) * D + z) * W + x] =
                        src[(y * D + z) * W + x];
    }

    int32_t cx = static_cast<int32_t>(key >> 32);
    int32_t cz = static_cast<int32_t>(key & 0xFFFFFFFFLL);
    m_saveManager->saveChunk(glm::ivec2(cx, cz), dst);
}

void ChunkManager::saveDirtyBlockReadyChunks() {
    if (!m_saveManager || m_blockReadyDirty.empty()) return;

    std::vector<ChunkKey> done;
    for (ChunkKey key : m_blockReadyDirty) {
        auto it = m_blockReady.find(key);
        if (it == m_blockReady.end()) {
            // 已不在 block-ready（可能已 loaded，脏标记已交接，或已卸载）→ 移除
            done.push_back(key);
            continue;
        }
        saveBlockReadyChunkToDisk(key, it->second.boxes);
        done.push_back(key);
    }

    for (ChunkKey key : done) m_blockReadyDirty.erase(key);
}

void ChunkManager::unloadDistantChunks() {
    if (!m_saveManager) return;
    PROFILE_SCOPE("unloadDistantChunks");

    // 阶段 B：卸载判定改为"对任一玩家都不数据相关"（isDataRelevant），
    // 而非仅本机相机。这样远程玩家正站着的 chunk（Host 看不到）不会被误卸。
    bool removedAny = false;

    // --- 1. loaded chunk ---
    std::vector<glm::ivec2> toRemove;
    for (auto& kv : m_loadedChunks) {
        Chunk* chunk = kv.second.get();
        if (!chunk) continue;
        glm::ivec2 cp = chunk->getPosition();
        if (!isDataRelevant(cp, m_retainMargin)) {
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
        // 移除光源注册 + 清理光照缓存
        unregisterChunkLightSources(cp);
        it->second->unload();
        m_loadedChunks.erase(it);
        removedAny = true;
        if (m_onChunkUnloaded) m_onChunkUnloaded(cp.x, cp.y);
    }

    // --- 2. block-ready chunk（阶段 B 新增：代客保管的远程玩家 chunk 也要卸载）---
    // 跳过正在 mesh 投递中的（worker 还共享着 box，且 Task 2 结果回来要找归宿）。
    std::vector<ChunkKey> brRemove;
    for (auto& kv : m_blockReady) {
        ChunkKey key = kv.first;
        int32_t cx = static_cast<int32_t>(key >> 32);
        int32_t cz = static_cast<int32_t>(key & 0xFFFFFFFFLL);
        glm::ivec2 cp(cx, cz);
        if (m_meshInFlight.count(key)) continue;       // 过渡态，正在 mesh
        if (isDataRelevant(cp, m_retainMargin)) continue;
        // 卸载前若被改动过（在待存盘集合）则落盘
        if (m_blockReadyDirty.count(key)) {
            saveBlockReadyChunkToDisk(key, kv.second.boxes);
            m_blockReadyDirty.erase(key);
        }
        brRemove.push_back(key);
    }
    for (ChunkKey key : brRemove) {
        int32_t cx = static_cast<int32_t>(key >> 32);
        int32_t cz = static_cast<int32_t>(key & 0xFFFFFFFFLL);
        // 移除光源注册 + 清理光照缓存
        unregisterChunkLightSources(glm::ivec2(cx, cz));
        m_blockReady.erase(key);
        removedAny = true;
        if (m_onChunkUnloaded) {
            m_onChunkUnloaded(cx, cz);
        }
    }

    if (removedAny) {
        m_needChunkScan = true;
        m_drawListDirty = true;  // 有 chunk 卸载 → draw list 需重建（保险）
    }
}

void ChunkManager::saveAllDirtyChunks() {
    if (!m_saveManager) return;
    doAutoSave();
}

// ── 光源传播 ──────────────────────────────────────────────────────

void ChunkManager::notifyLightChange(const glm::ivec3& worldPos) {
    m_pendingLightChanges.push_back(worldPos);
}

void ChunkManager::updateLighting() {
    // ── 增量光照传播（主线程，仅处理方块变动引发的局部重传播）────
    // 区块生成时的初始光照由 Task 1（区块内 BFS）+ Task 2（跨边界 BFS）在 worker
    // 线程完成，主线程不再做全量重建。
    if (!m_pendingLightChanges.empty()) {
        auto blockQuery = [this](const glm::ivec3& pos) -> BlockState {
            if (pos.y < 0 || pos.y >= Chunk::HEIGHT) {
                return BlockState{ BLOCK_STONE, ORIENT_NONE };
            }
            glm::ivec2 cp(
                (int)std::floor((float)pos.x / (float)Chunk::WIDTH),
                (int)std::floor((float)pos.z / (float)Chunk::DEPTH)
            );
            if (!hasBlockData(cp)) {
                return BlockState{ BLOCK_STONE, ORIENT_NONE };
            }
            return getBlockAt(pos);
        };
        auto cacheGetter = [this](uint64_t sectionKey) -> SectionLightCache* {
            return m_lightCache.getOrCreate(sectionKey);
        };

        LightSourceRegistry& reg = LightSourceRegistry::instance();
        constexpr float kRadius = LightSourceRegistry::kMaxLightRadius;
        std::unordered_set<uint64_t> processed;
        for (const auto& pos : m_pendingLightChanges) {
            glm::ivec3 q(pos.x / 8, pos.y / 8, pos.z / 8);
            uint64_t key = (uint64_t)(uint32_t)q.x
                         | ((uint64_t)(uint32_t)q.y << 22)
                         | ((uint64_t)(uint32_t)q.z << 44);
            if (processed.count(key)) continue;
            processed.insert(key);
            LightPropagation::propagateRegion(pos, kRadius, reg, blockQuery, cacheGetter);
        }
        m_pendingLightChanges.clear();
    }

    // ── 上传脏光照缓存到 GPU ─────────────────────────────────────
    int R = m_renderRadius;
    auto camPos = m_lastVisCameraPos;
    int camSecX = (int)std::floor(camPos.x / 16.0f);
    int camSecY = glm::clamp((int)std::floor(camPos.y / 16.0f), 0, Chunk::SECTION_COUNT - 1);
    int camSecZ = (int)std::floor(camPos.z / 16.0f);

    glm::ivec3 secMin(camSecX - R, 0, camSecZ - R);
    glm::ivec3 secMax(camSecX + R, Chunk::SECTION_COUNT - 1, camSecZ + R);

    m_lightCache.uploadToGPU(secMin, secMax);
}

// ── 光源注册：扫描 chunk 内所有方块，注册发光方块 ──────────────

void ChunkManager::registerChunkLightSources(Chunk* chunk) {
    if (!chunk) return;
    auto& reg = LightSourceRegistry::instance();
    glm::ivec2 cp = chunk->getPosition();

    for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
        const Section& sec = chunk->getSection(sy);
        const BlockState* data = sec.stateData();
        if (!data) continue;
        int baseY = sy * Section::HEIGHT;
        for (int y = 0; y < Section::HEIGHT; ++y) {
            for (int z = 0; z < Section::DEPTH; ++z) {
                for (int x = 0; x < Section::WIDTH; ++x) {
                    BlockState state = data[(y * Section::DEPTH + z) * Section::WIDTH + x];
                    BlockProperties props = GetBlockProperties(state.type());
                    if (props.emissive > 0.0f) {
                        glm::ivec3 worldPos(
                            cp.x * Chunk::WIDTH + x,
                            baseY + y,
                            cp.y * Chunk::DEPTH + z
                        );
                        LightSource src = getBlockLightDef(state.type(), worldPos);
                        if (src.intensity > 0.0f) {
                            reg.addLight(worldPos, src);
                        }
                    }
                }
            }
        }
    }
}

void ChunkManager::unregisterChunkLightSources(const glm::ivec2& chunkPos) {
    auto& reg = LightSourceRegistry::instance();
    reg.removeLightsInChunk(chunkPos.x, chunkPos.y);

    // 清理该 chunk 所有 section 的光照缓存
    for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
        m_lightCache.remove(makeSectionKey(chunkPos.x, chunkPos.y, sy));
    }
}
