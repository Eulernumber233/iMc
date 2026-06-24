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

    // 投递一个 import 任务（任务3）：跳过地形生成，直接用预填充的 buffer 构建 chunk。
    // buffer 大小为 CHUNK_VOLUME，所有权转移给 worker。
    void submitImport(const glm::ivec2& pos, std::unique_ptr<BlockState[]> buffer);

    // 主线程每帧调用：拿出所有已完成的 build 结果。
    std::vector<ChunkBuildResult> drainCompleted();
    // 主线程每帧调用：拿出所有已完成的 stitch 结果。
    std::vector<StitchResult> drainStitchResults();

    // 当前队列待处理 + 正在跑的总数（仅 build 任务计数，stitch 由 chunk 状态隐式跟踪）
    int pendingCount() const { return m_pending.load(std::memory_order_relaxed); }

    // 设置存档管理器指针（用于 buildOne 中尝试从磁盘加载）
    void setSaveManager(const class ChunkSaveManager* sm) { m_saveManager = sm; }

private:
    void workerMain();
    void buildOne(const glm::ivec2& pos, ChunkBuildResult& out) const;
    void importOne(const glm::ivec2& pos, BlockState* buffer, ChunkBuildResult& out) const;
    void stitchOne(Chunk* a, Chunk* b, uint8_t dirAtoB, StitchResult& out) const;

    enum JobKind : uint8_t { JOB_BUILD = 0, JOB_STITCH = 1, JOB_IMPORT = 2 };
    struct Job {
        JobKind kind;
        // BUILD 字段
        glm::ivec2 pos{ 0, 0 };
        // STITCH 字段
        Chunk* a = nullptr;
        Chunk* b = nullptr;
        uint8_t dirAtoB = 0;
        // IMPORT 字段
        std::unique_ptr<BlockState[]> importBuffer;
    };

    const TerrainGenerator* m_generator = nullptr;
    const class ChunkSaveManager* m_saveManager = nullptr;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{ false };

    // 任务队列：所有 worker 共享一把锁取任务 + 投递任务。
    std::mutex m_jobMutex;
    std::condition_variable m_jobCV;
    std::deque<Job> m_jobs;

    // 完成结果：build / stitch 各一把锁。
    //
    // ChunkBuildResult 含 4 个 Section，每个 Section 内 m_blocks 是 std::array<BlockType, 4096>，
    // 即 4KB 字节数组（不是指针，move 也要 memcpy）。直接 push_back ChunkBuildResult 会让锁内
    // 完成 16KB 拷贝 + 4 个 hash map 的内部指针交换 —— profiler 实测多 worker 时锁内最高耗时 17ms。
    //
    // 改用 unique_ptr：worker 在锁外 new 完整结果（堆分配在 worker 自己上下文），锁内只往 deque
    // push 一个指针。持锁时间稳定在 ns 级。
    std::mutex m_buildDoneMutex;
    std::deque<std::unique_ptr<ChunkBuildResult>> m_buildDone;
    std::mutex m_stitchDoneMutex;
    std::deque<StitchResult> m_stitchDone;

    std::atomic<int> m_pending{ 0 };
};
