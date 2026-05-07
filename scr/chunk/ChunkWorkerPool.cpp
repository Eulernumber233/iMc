#include "ChunkWorkerPool.h"
#include "../generate/TerrainGenerator.h"
#include <iostream>

ChunkWorkerPool::ChunkWorkerPool() = default;

ChunkWorkerPool::~ChunkWorkerPool() {
    stop();
}

void ChunkWorkerPool::start(const TerrainGenerator* generator, int numThreads) {
    if (!m_workers.empty()) return;
    m_generator = generator;
    m_stop.store(false);

    if (numThreads <= 0) numThreads = 2;
    if (numThreads > 8) numThreads = 8;

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
        std::lock_guard<std::mutex> lk(m_doneMutex);
        m_done.clear();
    }
    m_pending.store(0);
}

void ChunkWorkerPool::submit(const glm::ivec2& pos) {
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobs.push_back(pos);
    }
    m_pending.fetch_add(1, std::memory_order_relaxed);
    m_jobCV.notify_one();
}

std::vector<ChunkBuildResult> ChunkWorkerPool::drainCompleted() {
    std::vector<ChunkBuildResult> out;
    std::lock_guard<std::mutex> lk(m_doneMutex);
    out.swap(m_done);
    return out;
}

void ChunkWorkerPool::workerMain() {
    while (true) {
        glm::ivec2 pos;
        {
            std::unique_lock<std::mutex> lk(m_jobMutex);
            m_jobCV.wait(lk, [this] { return m_stop.load() || !m_jobs.empty(); });
            if (m_stop.load() && m_jobs.empty()) return;
            pos = m_jobs.front();
            m_jobs.pop_front();
        }

        ChunkBuildResult result;
        result.pos = pos;
        buildOne(pos, result);

        {
            std::lock_guard<std::mutex> lk(m_doneMutex);
            m_done.push_back(std::move(result));
        }
        m_pending.fetch_sub(1, std::memory_order_relaxed);
    }
}

void ChunkWorkerPool::buildOne(const glm::ivec2& pos, ChunkBuildResult& out) const {
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    constexpr int W = ChunkConstants::CHUNK_WIDTH;
    constexpr int H = ChunkConstants::CHUNK_HEIGHT;
    constexpr int D = ChunkConstants::CHUNK_DEPTH;

    BlockType buf[VOL];
    m_generator->fillChunkBuffer(buf, pos);

    // 把 buffer 切到 4 个 section
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        out.sections[sy].setCoords(pos.x, pos.y, sy);
        BlockType* dst = out.sections[sy].blockData();
        for (int y = 0; y < Section::HEIGHT; ++y) {
            int worldY = sy * Section::HEIGHT + y;
            for (int z = 0; z < D; ++z) {
                for (int x = 0; x < W; ++x) {
                    dst[(y * Section::DEPTH + z) * Section::WIDTH + x] =
                        buf[(worldY * D + z) * W + x];
                }
            }
        }
    }

    // 计算每个 section 的可见性（含垂直邻居 section；横向边界默认不可见）
    for (int sy = 0; sy < ChunkBuildResult::SECTION_COUNT; ++sy) {
        const Section* above = (sy + 1 < ChunkBuildResult::SECTION_COUNT)
            ? &out.sections[sy + 1] : nullptr;
        const Section* below = (sy > 0) ? &out.sections[sy - 1] : nullptr;
        out.sections[sy].rebuildVisibilityInternal(above, below);
    }
}
