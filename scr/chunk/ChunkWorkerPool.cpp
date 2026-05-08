#include "ChunkWorkerPool.h"
#include "Chunk.h"
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
    //if (numThreads > 8) numThreads = 8;

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
        std::lock_guard<std::mutex> lk(m_buildDoneMutex);
        m_buildDone.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_stitchDoneMutex);
        m_stitchDone.clear();
    }
    m_pending.store(0);
}

void ChunkWorkerPool::submit(const glm::ivec2& pos) {
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        Job j; 
        j.kind = JOB_BUILD; 
        j.pos = pos;
        m_jobs.push_back(std::move(j));
    }
    m_pending.fetch_add(1, std::memory_order_relaxed);
    m_jobCV.notify_one();
}

void ChunkWorkerPool::submitStitch(Chunk* a, Chunk* b, uint8_t dirAtoB) {
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        Job j; 
        j.kind = JOB_STITCH; 
        j.a = a; 
        j.b = b; 
        j.dirAtoB = dirAtoB;
        // 平等排队 —— stitch / build 不抢插队优先级；前面把 stitch push_front 反而会让
        // build 长期排不到位，玩家高速移动时新区块迟迟不显示。
        m_jobs.push_back(std::move(j));
    }
    m_jobCV.notify_one();
}

std::vector<ChunkBuildResult> ChunkWorkerPool::drainCompleted() {
    // 持锁只做 deque 的 O(1) swap；锁外解 unique_ptr、组装 vector。
    std::deque<std::unique_ptr<ChunkBuildResult>> tmp;
    {
        std::lock_guard<std::mutex> lk(m_buildDoneMutex);
        tmp.swap(m_buildDone);
    }
    std::vector<ChunkBuildResult> out;
    out.reserve(tmp.size());
    for (auto& p : tmp) out.push_back(std::move(*p));
    return out;
}

std::vector<StitchResult> ChunkWorkerPool::drainStitchResults() {
    std::deque<StitchResult> tmp;
    {
        std::lock_guard<std::mutex> lk(m_stitchDoneMutex);
        tmp.swap(m_stitchDone);
    }
    std::vector<StitchResult> out;
    out.reserve(tmp.size());
    for (auto& r : tmp) out.push_back(r);
    return out;
}

void ChunkWorkerPool::workerMain() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_jobMutex);
            m_jobCV.wait(lk, [this] { return m_stop.load() || !m_jobs.empty(); });
            if (m_stop.load() && m_jobs.empty()) return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }

        if (job.kind == JOB_BUILD) {
            // 在锁外堆分配 ChunkBuildResult（4×4KB 数据 + hash map 等），
            // worker 自己的上下文做完整构造；锁内只 push 一个指针。
            auto result = std::make_unique<ChunkBuildResult>();
            result->pos = job.pos;
            buildOne(job.pos, *result);

            {
                std::lock_guard<std::mutex> lk(m_buildDoneMutex);
                m_buildDone.push_back(std::move(result));
            }
            m_pending.fetch_sub(1, std::memory_order_relaxed);
        } else {
            StitchResult sr;
            stitchOne(job.a, job.b, job.dirAtoB, sr);
            {
                std::lock_guard<std::mutex> lk(m_stitchDoneMutex);
                m_stitchDone.push_back(sr);
            }
            // stitch 不计入 pendingCount（pendingCount 只跟 build 配额有关）
        }
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

void ChunkWorkerPool::stitchOne(Chunk* a, Chunk* b, uint8_t dirAtoB, StitchResult& out) const {
    // 直接调 Chunk::stitchWithNeighbor —— 双向修改 a / b 的边界 mesh。
    // 调用方保证 a / b 在投递期间始终在 m_pendingChunks 中，主线程不会触碰，无需加锁。
    BlockFace face = (BlockFace)dirAtoB;
    a->stitchWithNeighbor(b, face);
    a->refreshNonEmptyMask();
    b->refreshNonEmptyMask();

    // dirB = 反向
    uint8_t dirB = 0;
    switch (face) {
    case RIGHT: dirB = LEFT; break;
    case LEFT:  dirB = RIGHT; break;
    case FRONT: dirB = BACK; break;
    case BACK:  dirB = FRONT; break;
    default: dirB = 0; break;
    }

    out.posA = a->getPosition();
    out.posB = b->getPosition();
    out.dirA = dirAtoB;
    out.dirB = dirB;
}
