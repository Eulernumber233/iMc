#include "ChunkWorkerPool.h"
#include "Chunk.h"
#include "../generate/TerrainGenerator.h"
#include "../save/ChunkSaveManager.h"
#include <iostream>
#include <cstring>

ChunkWorkerPool::ChunkWorkerPool() = default;

ChunkWorkerPool::~ChunkWorkerPool() {
    stop();
}

void ChunkWorkerPool::start(const TerrainGenerator* generator, int numThreads) {
    if (!m_workers.empty()) return;
    m_generator = generator;
    m_stop.store(false);

    if (numThreads <= 0) numThreads = 2;

    m_workers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        m_workers.emplace_back([this] { this->workerMain(); });
    }
}

void ChunkWorkerPool::stop() {
    if (m_workers.empty()) return;
    m_stop.store(true);
    m_jobCV.notify_all();
    for (auto& t : m_workers) {
        if (t.joinable()) t.join();
    }
    m_workers.clear();

    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobs.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_blockDoneMutex);
        m_blockDone.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_meshDoneMutex);
        m_meshDone.clear();
    }
    m_pending.store(0);
}

void ChunkWorkerPool::submitBuild(const glm::ivec2& pos) {
    Job j;
    j.kind = JOB_BUILD;
    j.pos = pos;
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobs.push_back(std::move(j));
    }
    m_pending.fetch_add(1, std::memory_order_relaxed);
    m_jobCV.notify_one();
}

void ChunkWorkerPool::submitMeshBuild(const MeshBuildInput& input) {
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        Job j;
        j.kind = JOB_MESH;
        j.meshInput = input;
        m_jobs.push_back(std::move(j));
    }
    m_jobCV.notify_one();
}

std::vector<BlockDataResult> ChunkWorkerPool::drainBlockData() {
    std::deque<std::unique_ptr<BlockDataResult>> tmp;
    {
        std::lock_guard<std::mutex> lk(m_blockDoneMutex);
        tmp.swap(m_blockDone);
    }
    std::vector<BlockDataResult> out;
    out.reserve(tmp.size());
    for (auto& p : tmp) out.push_back(std::move(*p));
    return out;
}

std::vector<ChunkBuildResult> ChunkWorkerPool::drainMeshResults() {
    std::deque<std::unique_ptr<ChunkBuildResult>> tmp;
    {
        std::lock_guard<std::mutex> lk(m_meshDoneMutex);
        tmp.swap(m_meshDone);
    }
    std::vector<ChunkBuildResult> out;
    out.reserve(tmp.size());
    for (auto& p : tmp) out.push_back(std::move(*p));
    return out;
}

void ChunkWorkerPool::workerMain() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_jobMutex);
            m_jobCV.wait(lk, [this] { return m_stop.load() || !m_jobs.empty(); });
            if (m_stop.load() && m_jobs.empty()) return;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        if (job.kind == JOB_BUILD) {
            auto result = std::make_unique<BlockDataResult>();
            result->pos = job.pos;
            buildOne(job.pos, *result);
            {
                std::lock_guard<std::mutex> lk(m_blockDoneMutex);
                m_blockDone.push_back(std::move(result));
            }
            m_pending.fetch_sub(1, std::memory_order_relaxed);
        } else {
            // JOB_MESH
            auto result = std::make_unique<ChunkBuildResult>();
            result->pos = job.meshInput.pos;
            meshBuildOne(job.meshInput, *result);
            {
                std::lock_guard<std::mutex> lk(m_meshDoneMutex);
                m_meshDone.push_back(std::move(result));
            }
        }
    }
}

// ============================================================================
// Task 1：生成/加载方块数据（不构建 mesh）
// ============================================================================

void ChunkWorkerPool::buildOne(const glm::ivec2& pos, BlockDataResult& out) const {
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    out.blocks = std::make_unique<BlockState[]>(VOL);

    // 先尝试从存档加载，失败则走地形生成
    bool loadedFromDisk = false;
    if (m_saveManager) {
        loadedFromDisk = m_saveManager->loadChunk(pos, out.blocks.get());
    }
    if (!loadedFromDisk) {
        m_generator->fillChunkBuffer(out.blocks.get(), pos);
    }
}

// ============================================================================
// Task 2：完整可见面生成（内部 + 全部边界）
// ============================================================================

void ChunkWorkerPool::meshBuildOne(const MeshBuildInput& in, ChunkBuildResult& out) const {
    constexpr int W = ChunkConstants::CHUNK_WIDTH;
    constexpr int H = ChunkConstants::CHUNK_HEIGHT;
    constexpr int D = ChunkConstants::CHUNK_DEPTH;
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    constexpr int SEC_H = Section::HEIGHT;
    constexpr int SEC_VOL = Section::VOLUME;

    // 将自身方块数据拷贝到各 section
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        out.sections[sy].setCoords(in.pos.x, in.pos.y, sy);
        BlockState* dst = out.sections[sy].stateData();
        for (int y = 0; y < SEC_H; ++y) {
            int worldY = sy * SEC_H + y;
            for (int z = 0; z < D; ++z) {
                for (int x = 0; x < W; ++x) {
                    dst[(y * Section::DEPTH + z) * Section::WIDTH + x] =
                        in.selfBlocks[(worldY * D + z) * W + x];
                }
            }
        }
    }

    // 计算每个 section 的可见性（含垂直邻居 + 横向邻居）
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        const Section* above = (sy + 1 < ChunkBuildResult::SECTION_COUNT)
            ? &out.sections[sy + 1] : nullptr;
        const Section* below = (sy > 0) ? &out.sections[sy - 1] : nullptr;

        // 内部 + 垂直边界
        out.sections[sy].rebuildVisibilityInternal(above, below);

        // 横向边界：用邻居方块数据补充
        Section& sec = out.sections[sy];
        int baseY = sy * SEC_H;

        // +X 边界 (x = W-1, face = RIGHT)
        if (in.neighborBlocks[0]) {
            for (int y = 0; y < SEC_H; ++y) {
                int worldY = baseY + y;
                for (int z = 0; z < D; ++z) {
                    // 邻居在 (+1,0) 方向的 (0, worldY, z) 处
                    BlockState nb = in.neighborBlocks[0][(worldY * D + z) * W + 0];
                    sec.updateFaceWithNeighbor(W - 1, y, z, RIGHT, nb);
                }
            }
        }

        // -X 边界 (x = 0, face = LEFT)
        if (in.neighborBlocks[1]) {
            for (int y = 0; y < SEC_H; ++y) {
                int worldY = baseY + y;
                for (int z = 0; z < D; ++z) {
                    BlockState nb = in.neighborBlocks[1][(worldY * D + z) * W + (W - 1)];
                    sec.updateFaceWithNeighbor(0, y, z, LEFT, nb);
                }
            }
        }

        // +Z 边界 (z = D-1, face = FRONT)
        if (in.neighborBlocks[2]) {
            for (int y = 0; y < SEC_H; ++y) {
                int worldY = baseY + y;
                for (int x = 0; x < W; ++x) {
                    BlockState nb = in.neighborBlocks[2][(worldY * D + 0) * W + x];
                    sec.updateFaceWithNeighbor(x, y, D - 1, FRONT, nb);
                }
            }
        }

        // -Z 边界 (z = 0, face = BACK)
        if (in.neighborBlocks[3]) {
            for (int y = 0; y < SEC_H; ++y) {
                int worldY = baseY + y;
                for (int x = 0; x < W; ++x) {
                    BlockState nb = in.neighborBlocks[3][(worldY * D + (D - 1)) * W + x];
                    sec.updateFaceWithNeighbor(x, y, 0, BACK, nb);
                }
            }
        }
    }
}
