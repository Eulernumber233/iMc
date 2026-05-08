#include "ChunkManager.h"
#include "Chunk.h"
#include "Section.h"
#include "../generate/TerrainGenerator.h"
#include "../RuntimeConfig.h"
#include "../Profiler.h"
#include <iostream>
#include <algorithm>
#include <thread>
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

ChunkManager::ChunkManager(unsigned int seed)
    : m_renderRadius(8) {
    m_generator = std::make_shared<TerrainGenerator>();
    m_generator->setSeed(seed);
}

ChunkManager::~ChunkManager() {
    m_workerPool.stop();

    m_loadedChunks.clear();
    m_pendingChunks.clear();
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
    }

    // 把已完成的 build worker 结果接管为 pending chunk
    integrateBuiltChunks();
    // 把已完成的 stitch worker 结果应用到 pending chunk 的状态位，必要时晋升到 loaded
    integrateStitchResults();

    // 补投递：初始化或一次性大批量请求时 m_maxInflightRequests 限流会丢一部分；
    // 完成的 chunk 腾出 in-flight 配额后，每帧扫一次半径内缺失的 chunk 把它们补上。
    if ((int)m_inFlight.size() < m_maxInflightRequests) {
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
            SectionKey key = makeSectionKey(cp.x, cp.y, sy);
            auto it = m_sectionSlots.find(key);
            if (it == m_sectionSlots.end()) continue;
            m_arena.free(it->second);
            m_sectionSlots.erase(it);
            // 清掉 section 的增量状态：再次入活跃半径时下次 upload 自动走全量 reupload 重建 slot
            chunk->getSection(sy).notifyGpuSlotReleased();
        }
    }
}

void ChunkManager::requestMissingChunks() {
    // 优先靠近相机中心：用曼哈顿距离逐圈扩散；扫描到 stitch 预加载半径以填外圈陪练。
    const int loadR = m_renderRadius + STITCH_PRELOAD_MARGIN;
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
                requestChunkLoad(chunkPos);
            }
        }
    }
}

void ChunkManager::requestChunkLoad(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    if (m_loadedChunks.find(key) != m_loadedChunks.end()) return;
    if (m_pendingChunks.find(key) != m_pendingChunks.end()) return;
    if (m_inFlight.find(key) != m_inFlight.end()) return;
    m_inFlight.insert(key);
    m_workerPool.submit(chunkPos);
}

Chunk* ChunkManager::getPendingChunk(const glm::ivec2& chunkPos) {
    auto it = m_pendingChunks.find(chunkPosToKey(chunkPos));
    return it != m_pendingChunks.end() ? it->second.get() : nullptr;
}

void ChunkManager::integrateBuiltChunks() {
    PROFILE_SCOPE("integrateBuiltChunks");
    auto results = m_workerPool.drainCompleted();
    for (auto& r : results) {
        ChunkKey key = chunkPosToKey(r.pos);
        m_inFlight.erase(key);

        // 已存在 → 丢弃（重复投递或竞态）。
        if (m_loadedChunks.find(key) != m_loadedChunks.end()) continue;
        if (m_pendingChunks.find(key) != m_pendingChunks.end()) continue;

        auto chunk = std::make_unique<Chunk>(r.pos, this);
        chunk->adoptSections(std::move(r.sections));

        Chunk* raw = chunk.get();
        m_pendingChunks[key] = std::move(chunk);
        // 邻居图只在 pending 内部连：worker stitch 任务只能涉及 pending chunk。
        linkNeighbors(raw);

        // 投递自己 4 方向能投的 stitch；同时让已存在的邻居重新检查（它们之前可能因为
        // self 不存在而漏投，self 入场后那条边现在能投了）。trySubmitStitchJobs 内部
        // 会检查双方 stitch 状态，幂等，不会重复投递。
        trySubmitStitchJobs(raw);
        for (int d = 0; d < 4; ++d) {
            if (Chunk* nb = raw->m_neighbors[d]) {
                trySubmitStitchJobs(nb);
            }
        }
    }
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

    if (xp) xp->m_neighbors[1] = newChunk;
    if (xn) xn->m_neighbors[0] = newChunk;
    if (zp) zp->m_neighbors[3] = newChunk;
    if (zn) zn->m_neighbors[2] = newChunk;
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
        if (!nb) continue;
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
    auto results = m_workerPool.drainStitchResults();
    if (results.empty()) return;

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

    // 再扫一遍把 4 方向都 done 的 chunk 晋升到 loaded。
    for (const auto& sr : results) {
        promoteIfReady(sr.posA);
        promoteIfReady(sr.posB);
    }
}

void ChunkManager::promoteIfReady(const glm::ivec2& chunkPos) {
    auto it = m_pendingChunks.find(chunkPosToKey(chunkPos));
    if (it == m_pendingChunks.end()) return;
    Chunk* raw = it->second.get();
    if (!raw->allStitchDone()) return;

    // 4 方向都 stitch 完毕 → 晋升。邻居指针保留不动（它们指向的 pending chunk 在它们
    // 各自晋升时也不会改 m_neighbors，所以 loaded chunk 之间的邻居图不连续是允许的：
    // loaded chunk 不再使用 m_neighbors 做 stitch；玩家交互路径会通过 ChunkManager
    // 的 setBlock/getBlockAt 主动按坐标查 loaded map 找邻居）。
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

    auto it = m_sectionSlots.find(key);
    ChunkArena::Slot oldSlot = (it != m_sectionSlots.end()) ? it->second : ChunkArena::Slot{};

    // 增量上传条件：
    //   1) 已有有效 slot
    //   2) section 没有发出全量重建信号（rebuild / compact / adopt 不走增量）
    //   3) 有脏 index 可写
    //   4) 新的 instanceData 长度仍 <= slot.capacity（即不需要升 size class）
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
        // patch 内部会更新 s.count 到 newCount，回写 map
        m_sectionSlots[key] = s;
    } else {
        ChunkArena::Slot newSlot = m_arena.reupload(oldSlot,
            data.empty() ? nullptr : data.data(),
            (uint32_t)data.size());

        if (data.empty()) {
            if (oldSlot.valid()) m_sectionSlots.erase(key);
        } else if (newSlot.valid()) {
            m_sectionSlots[key] = newSlot;
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

    // 第一遍：evict 半径内（active 半径 + hysteresis）的脏 section 上传。
    // - 跨 chunk stitch 可能改到 active 半径外 1 格的 chunk mesh，所以扫描范围必须比 active 半径大。
    // - 走出 evict 半径的 chunk 已经被 evictFarChunkSlots 释放了 GPU slot 并标记为 fullRebuildPending；
    //   只要它们没回到 evict 半径内，就不上传，避免每帧重新分配 slot。
    // - 脏 section 可能是"刚被改成空"，也需要走一遍上传以释放 slot —— 所以这里不能用 nonEmptyMask
    //   提前剪枝，必须按 dirty 标志扫。
    for (auto& kv : m_loadedChunks) {
        Chunk* chunk = kv.second.get();
        if (!chunk->isMeshReady()) continue;
        glm::ivec2 cp = chunk->getPosition();
        if (!isWithinEvictRadius(cp, m_currentCenterChunk)) continue;
        for (int sy = 0; sy < Chunk::SECTION_COUNT; ++sy) {
            Section& s = chunk->getSection(sy);
            if (!s.isDirty()) continue;
            uploadSection(cp.x, cp.y, sy, s, uploadBudget);
            if (uploadBudget <= 0) break;
        }
        if (uploadBudget <= 0) break;
    }

    // 第二遍：按 active chunk -> per-section 视锥/距离剔除生成 indirect 命令
    for (Chunk* chunk : m_activeChunks) {
        if (!chunk->isMeshReady()) continue;

        uint32_t mask = chunk->getNonEmptyMask();
        if (mask == 0) continue;   // 整 chunk 没有任何可见面

        if (!chunk->isChunkPotentiallyVisible(m_camera)) continue;

        // 只遍历 mask 置位的 section
        while (mask) {
            int sy = lowestBitIndex(mask);
            mask &= mask - 1u;   // 清掉最低置位

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

            // 与命令同序追加 section base：shader 通过 gl_DrawID 拿到 (chunkX*16, sectionY*16, chunkZ*16)
            glm::ivec2 cp = chunk->getPosition();
            m_sectionBases.emplace_back(
                float(cp.x * Chunk::WIDTH),
                float(sy * Section::HEIGHT),
                float(cp.y * Chunk::DEPTH),
                0.0f);

            m_visibleInstanceCount += slot.count;
        }
    }

    syncIndirectBuffer();
    syncSectionBaseSSBO();
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
