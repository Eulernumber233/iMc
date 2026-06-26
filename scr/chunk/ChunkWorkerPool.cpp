#include "ChunkWorkerPool.h"
#include "Chunk.h"
#include "../generate/TerrainGenerator.h"
#include "../save/ChunkSaveManager.h"
#include <iostream>
#include <cstring>
#include <shared_mutex>

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

    // 地形生成器 / 存档接口按「整 chunk 连续 buffer」工作（布局 (worldY*D+z)*W+x）。
    // 先填这个临时大 buffer，再切片进 16 个 section BlockBox。
    // 切片发生在 worker 线程，不占主线程；之后 Task 2 全程零拷贝共享这些 box。
    auto tmp = std::make_unique<BlockState[]>(VOL);

    bool loadedFromDisk = false;
    if (m_saveManager) {
        loadedFromDisk = m_saveManager->loadChunk(pos, tmp.get());
    }
    if (!loadedFromDisk) {
        m_generator->fillChunkBuffer(tmp.get(), pos);
    }

    splitChunkBufferToBoxes(tmp.get(), out.boxes);
}

// ============================================================================
// Task 2：完整可见面生成（内部 + 全部边界）
// ============================================================================

namespace {
    // 从邻居 section 的 BlockBox 持读锁拷出一个横向边界面（一层）。
    // 与玩家修改该 box 的写锁互斥；与其他 worker 的读共享。
    // 输出布局：RIGHT/LEFT 按 [y*DEPTH+z]；FRONT/BACK 按 [y*WIDTH+x]。
    void copyBoundaryLayerFromBox(const std::shared_ptr<BlockBox>& box,
                                  BlockFace face, BlockState* out) {
        constexpr int W = ChunkConstants::CHUNK_WIDTH;
        constexpr int D = ChunkConstants::CHUNK_DEPTH;
        constexpr int H = ChunkConstants::SECTION_HEIGHT;
        auto sidx = [](int x, int y, int z) { return (y * D + z) * W + x; };

        std::shared_lock<std::shared_mutex> lk(box->mutex);
        const auto& b = box->blocks;
        switch (face) {
        case RIGHT: // x = W-1 平面
            for (int y = 0; y < H; ++y)
                for (int z = 0; z < D; ++z) out[y * D + z] = b[sidx(W - 1, y, z)];
            break;
        case LEFT:  // x = 0 平面
            for (int y = 0; y < H; ++y)
                for (int z = 0; z < D; ++z) out[y * D + z] = b[sidx(0, y, z)];
            break;
        case FRONT: // z = D-1 平面
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) out[y * W + x] = b[sidx(x, y, D - 1)];
            break;
        case BACK:  // z = 0 平面
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) out[y * W + x] = b[sidx(x, y, 0)];
            break;
        default: break;
        }
    }
} // namespace

void ChunkWorkerPool::meshBuildOne(const MeshBuildInput& in, ChunkBuildResult& out) const {
    constexpr int W = ChunkConstants::CHUNK_WIDTH;
    constexpr int D = ChunkConstants::CHUNK_DEPTH;
    constexpr int SEC_H = Section::HEIGHT;

    // self 数据：直接把 Task 1 产出的 16 个 BlockBox 共享给生成出来的 Section（零拷贝）。
    // self 在本 chunk LOADED 前玩家无法触碰，故此处无需加锁。
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        out.sections[sy].setCoords(in.pos.x, in.pos.y, sy);
        out.sections[sy].setBox(in.self[sy]);
    }

    // 邻居边界层临时缓冲：横向边界一层 = HEIGHT(=16) × {WIDTH 或 DEPTH}(=16) = 256 格。
    // 由 copyBoundaryLayerLocked 持邻居读锁填充：RIGHT/LEFT 按 [y*DEPTH+z]，FRONT/BACK 按 [y*WIDTH+x]。
    BlockState layer[SEC_H * (W > D ? W : D)];

    // 计算每个 section 的可见性（含垂直邻居 + 横向邻居）
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        const Section* above = (sy + 1 < ChunkBuildResult::SECTION_COUNT)
            ? &out.sections[sy + 1] : nullptr;
        const Section* below = (sy > 0) ? &out.sections[sy - 1] : nullptr;

        // 内部 + 垂直边界
        out.sections[sy].rebuildVisibilityInternal(above, below);

        Section& sec = out.sections[sy];

        // +X 边界 (我 x=W-1 的 RIGHT 面 ←→ 邻居[0] x=0 的 LEFT 面)
        if (in.neighbors[0][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[0][sy], LEFT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int z = 0; z < D; ++z)
                    sec.updateFaceWithNeighbor(W - 1, y, z, RIGHT, layer[y * D + z]);
        }
        // -X 边界 (我 x=0 的 LEFT 面 ←→ 邻居[1] x=W-1 的 RIGHT 面)
        if (in.neighbors[1][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[1][sy], RIGHT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int z = 0; z < D; ++z)
                    sec.updateFaceWithNeighbor(0, y, z, LEFT, layer[y * D + z]);
        }
        // +Z 边界 (我 z=D-1 的 FRONT 面 ←→ 邻居[2] z=0 的 BACK 面)
        if (in.neighbors[2][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[2][sy], BACK, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int x = 0; x < W; ++x)
                    sec.updateFaceWithNeighbor(x, y, D - 1, FRONT, layer[y * W + x]);
        }
        // -Z 边界 (我 z=0 的 BACK 面 ←→ 邻居[3] z=D-1 的 FRONT 面)
        if (in.neighbors[3][sy]) {
            copyBoundaryLayerFromBox(in.neighbors[3][sy], FRONT, layer);
            for (int y = 0; y < SEC_H; ++y)
                for (int x = 0; x < W; ++x)
                    sec.updateFaceWithNeighbor(x, y, 0, BACK, layer[y * W + x]);
        }
    }
}
