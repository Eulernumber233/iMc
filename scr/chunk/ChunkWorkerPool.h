#pragma once
#include "../core.h"
#include "BlockType.h"
#include "ChunkDimensions.h"
#include "Section.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <glm/glm.hpp>

class TerrainGenerator;
class Chunk;


// sections 已计算过 section 内 + 垂直邻居 section 的可见性，
// 但横向跨 chunk 的边界面默认全部不可见，由主线程缝合。
struct ChunkBuildResult {
    glm::ivec2 pos;
    static constexpr int SECTION_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;
    std::array<Section, SECTION_COUNT> sections;
};

// 跨 chunk 的横向缝合任务结果。worker 端已经直接修改过 a / b 两个 chunk 的 section mesh
// （安全前提：a / b 都在 ChunkManager::m_pendingChunks 中，主线程不会读写它们）。
// 这里只回传两个 chunk 的标识，让主线程更新 stitch state 位。
struct StitchResult {
    glm::ivec2 posA;
    glm::ivec2 posB;
    // dirA: a 视角下的方向（RIGHT/LEFT/FRONT/BACK），dirB 是它的反向
    uint8_t dirA;
    uint8_t dirB;
};

class ChunkWorkerPool {
public:
    ChunkWorkerPool();
    ~ChunkWorkerPool();

    // 启动 N 个 worker 线程；generator 必须在调用方生命周期内有效，且其 fillChunkBuffer 是线程安全的
    void start(const TerrainGenerator* generator, int numThreads);
    void stop();

    // 投递一个 chunk 生成任务（任务1）
    void submit(const glm::ivec2& pos);

    // 投递一个 stitch 任务（任务2）：在 worker 上把 a 与 b 在 dirAtoB 方向缝合。
    // 调用方必须保证：在该任务返回之前，a / b 不会被主线程触碰
    // （即两个 chunk 都在 m_pendingChunks 中）。
    void submitStitch(Chunk* a, Chunk* b, uint8_t dirAtoB);

    // 主线程每帧调用：拿出所有已完成的 build 结果。
    std::vector<ChunkBuildResult> drainCompleted();
    // 主线程每帧调用：拿出所有已完成的 stitch 结果。
    std::vector<StitchResult> drainStitchResults();

    // 当前队列待处理 + 正在跑的总数（仅 build 任务计数，stitch 由 chunk 状态隐式跟踪）
    int pendingCount() const { return m_pending.load(std::memory_order_relaxed); }

private:
    void workerMain();
    void buildOne(const glm::ivec2& pos, ChunkBuildResult& out) const;
    void stitchOne(Chunk* a, Chunk* b, uint8_t dirAtoB, StitchResult& out) const;

    enum JobKind : uint8_t { JOB_BUILD = 0, JOB_STITCH = 1 };
    struct Job {
        JobKind kind;
        // BUILD 字段
        glm::ivec2 pos{ 0, 0 };
        // STITCH 字段
        Chunk* a = nullptr;
        Chunk* b = nullptr;
        uint8_t dirAtoB = 0;
    };

    const TerrainGenerator* m_generator = nullptr;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{ false };

    std::mutex m_jobMutex;
    std::condition_variable m_jobCV;
    std::deque<Job> m_jobs;

    std::mutex m_doneMutex;
    std::vector<ChunkBuildResult> m_done;

    std::mutex m_stitchDoneMutex;
    std::vector<StitchResult> m_stitchDone;

    std::atomic<int> m_pending{ 0 };
};
