#include "NetSerializeWorker.h"
#include "NetChunkSync.h"  // ChunkSyncFormat::FLAG_HAS_DATA
#include "../chunk/BlockType.h"
#include "../chunk/Section.h"
#include "../chunk/ChunkDimensions.h"
#include "lz4.h"
#include <array>
#include <shared_mutex>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <cstring>

int NetSerializeWorker::maxThreads() {
    unsigned hc = std::thread::hardware_concurrency();
    int byCores = (hc > 1) ? (int)hc - 1 : 1;
    return (std::min)(4, (std::max)(1, byCores));  // 序列化非主导 CPU，上限保守取 4
}

NetSerializeWorker::~NetSerializeWorker() {
    stop();
}

void NetSerializeWorker::spawnThread() {
    // 调用方持 m_threadsMutex。线程 detach：缩容时 worker 自行退出，无需 join 记账；
    // stop() 通过等 m_aliveThreads 归零来保证全部退出。
    m_aliveThreads.fetch_add(1, std::memory_order_relaxed);
    std::thread(&NetSerializeWorker::workerMain, this).detach();
}

void NetSerializeWorker::start() {
    if (m_running.load()) return;
    m_stop.store(false);
    m_running.store(true);
    m_targetThreads.store(1);
    {
        std::lock_guard<std::mutex> lk(m_threadsMutex);
        spawnThread();  // 初始 1 个线程
    }
}

void NetSerializeWorker::setThreadCount(int desired) {
    if (!m_running.load()) return;
    desired = (std::max)(1, (std::min)(desired, maxThreads()));
    m_targetThreads.store(desired, std::memory_order_relaxed);

    std::lock_guard<std::mutex> lk(m_threadsMutex);
    // 扩容：立即补足存活线程到 desired。
    while (m_aliveThreads.load(std::memory_order_relaxed) < desired) {
        spawnThread();
    }
    // 缩容：仅降低目标，多余线程在 workerMain 下一轮唤醒后自行退出。唤醒它们去检查。
    m_jobCV.notify_all();
}

void NetSerializeWorker::stop() {
    if (!m_running.load()) return;
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_stop.store(true);
    }
    m_jobCV.notify_all();

    // 等所有 detach 的 worker 退出（它们退出前会把 m_aliveThreads 减 1）。
    // worker 不持长锁，退出极快；这里轮询等待至归零。
    while (m_aliveThreads.load(std::memory_order_acquire) > 0) {
        m_jobCV.notify_all();
        std::this_thread::yield();
    }
    m_running.store(false);

    // 清空残留任务/结果（线程已全部退出，无并发）
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobs.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_doneMutex);
        m_done.clear();
    }
}

void NetSerializeWorker::submit(Job&& job) {
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_jobs.push_back(std::move(job));
    }
    m_jobCV.notify_one();
}

void NetSerializeWorker::drainResults(std::vector<Result>& out, size_t maxResults) {
    std::lock_guard<std::mutex> lk(m_doneMutex);
    size_t n = (std::min)(maxResults, m_done.size());
    for (size_t i = 0; i < n; ++i) {
        out.push_back(std::move(m_done.front()));
        m_done.pop_front();
    }
}

size_t NetSerializeWorker::pendingCount() const {
    std::lock_guard<std::mutex> lk(m_jobMutex);
    return m_jobs.size();
}

void NetSerializeWorker::workerMain() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(m_jobMutex);
            m_jobCV.wait(lk, [this] {
                return m_stop.load() || !m_jobs.empty()
                    || m_aliveThreads.load(std::memory_order_relaxed)
                       > m_targetThreads.load(std::memory_order_relaxed);
            });
            if (m_stop.load() && m_jobs.empty()) break;

            // 缩容：存活数超目标 → 本线程退出（仅在没有积压时退，优先把队列清干净）。
            // 用 CAS 保证只有真正"多余"的那一个减计数退出，避免多线程同时误退到少于目标。
            if (m_jobs.empty()) {
                int alive = m_aliveThreads.load(std::memory_order_relaxed);
                int target = m_targetThreads.load(std::memory_order_relaxed);
                if (alive > target &&
                    m_aliveThreads.compare_exchange_strong(alive, alive - 1)) {
                    return;  // 已减计数，退出（detach 线程，无需 join）
                }
                continue;  // 被 setThreadCount 误唤醒但无活可干，回去等
            }

            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        Result res;
        res.chunkX = job.chunkX;
        res.chunkZ = job.chunkZ;
        res.targets = std::move(job.targets);
        serialize(job, res.payload);

        // 空 payload（无 section 数据，理论上不会发生）仍照常回传，主线程会跳过空发送
        {
            std::lock_guard<std::mutex> lk(m_doneMutex);
            m_done.push_back(std::move(res));
        }
    }

    // 因 stop 退出的线程也要减计数（缩容退出已在上面减过并 return）。
    m_aliveThreads.fetch_sub(1, std::memory_order_release);
}

void NetSerializeWorker::serialize(const Job& job, std::vector<uint8_t>& out) {
    // 与 NetChunkSync::serializeChunkFromBlocks 完全一致的格式，以便客户端
    // deserializeAndImport 无需改动。
    const int chunkX = job.chunkX;
    const int chunkZ = job.chunkZ;
    const ChunkBoxes& boxes = job.boxes;

    out.push_back(static_cast<uint8_t>(chunkX & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>(chunkZ & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 24) & 0xFF));

    size_t numSectionsPos = out.size();
    out.push_back(0);
    uint8_t numSections = 0;

    static constexpr int SECTION_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;
    static constexpr int SEC_VOL = Section::VOLUME;
    static constexpr int BLOCK_SIZE = SEC_VOL * sizeof(BlockState);
    static constexpr int MAX_COMPRESSED = LZ4_COMPRESSBOUND(BLOCK_SIZE);

    std::vector<uint8_t> compressBuf(MAX_COMPRESSED);

    for (int sy = 0; sy < SECTION_COUNT; ++sy) {
        if (!boxes[sy]) {
            // 该 section 无数据，按全空气处理
            out.push_back(static_cast<uint8_t>(sy));
            out.push_back(0);
            ++numSections;
            continue;
        }
        // 持读锁拷一份 section 数据（与玩家改方块的写锁互斥，与其他读共享）
        std::array<BlockState, SEC_VOL> snapshot;
        {
            std::shared_lock<std::shared_mutex> lk(boxes[sy]->mutex);
            snapshot = boxes[sy]->blocks;
        }
        const BlockState* sectionBlocks = snapshot.data();

        bool allAir = true;
        for (int i = 0; i < SEC_VOL; ++i) {
            if (sectionBlocks[i].type() != BLOCK_AIR) {
                allAir = false;
                break;
            }
        }

        if (allAir) {
            out.push_back(static_cast<uint8_t>(sy));
            out.push_back(0);
            ++numSections;
            continue;
        }

        int srcSize = static_cast<int>(SEC_VOL * sizeof(BlockState));
        int compressedSize = LZ4_compress_default(
            reinterpret_cast<const char*>(sectionBlocks),
            reinterpret_cast<char*>(compressBuf.data()),
            srcSize,
            static_cast<int>(compressBuf.size()));

        if (compressedSize <= 0 || compressedSize > 0xFFFF) {
            fprintf(stderr, "[NetSerializeWorker] LZ4 compress failed for (%d,%d) sy=%d\n",
                chunkX, chunkZ, sy);
            continue;
        }

        out.push_back(static_cast<uint8_t>(sy));
        out.push_back(ChunkSyncFormat::FLAG_HAS_DATA);
        uint16_t dataLen = static_cast<uint16_t>(compressedSize);
        out.push_back(static_cast<uint8_t>(dataLen & 0xFF));
        out.push_back(static_cast<uint8_t>((dataLen >> 8) & 0xFF));
        out.insert(out.end(), compressBuf.data(), compressBuf.data() + compressedSize);

        ++numSections;
    }

    out[numSectionsPos] = numSections;
}
