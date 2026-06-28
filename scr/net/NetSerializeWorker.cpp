#include "NetSerializeWorker.h"
#include "NetChunkSync.h"  // ChunkSyncFormat::FLAG_HAS_DATA
#include "../chunk/BlockType.h"
#include "../chunk/Section.h"
#include "../chunk/ChunkDimensions.h"
#include "lz4.h"
#include <array>
#include <shared_mutex>
#include <cstdio>
#include <cstring>

NetSerializeWorker::~NetSerializeWorker() {
    stop();
}

void NetSerializeWorker::start() {
    if (m_running.load()) return;
    m_stop.store(false);
    m_running.store(true);
    m_thread = std::thread(&NetSerializeWorker::workerMain, this);
}

void NetSerializeWorker::stop() {
    if (!m_running.load()) return;
    {
        std::lock_guard<std::mutex> lk(m_jobMutex);
        m_stop.store(true);
    }
    m_jobCV.notify_all();
    if (m_thread.joinable()) m_thread.join();
    m_running.store(false);

    // 清空残留任务/结果（线程已 join，无并发）
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
            m_jobCV.wait(lk, [this] { return m_stop.load() || !m_jobs.empty(); });
            if (m_stop.load() && m_jobs.empty()) return;
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
