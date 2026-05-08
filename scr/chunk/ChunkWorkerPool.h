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


// sections 已计算过 section 内 + 垂直邻居 section 的可见性，
// 但横向跨 chunk 的边界面默认全部不可见，由主线程缝合。
struct ChunkBuildResult {
    glm::ivec2 pos;
    static constexpr int SECTION_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;
    std::array<Section, SECTION_COUNT> sections;
};

class ChunkWorkerPool {
public:
    ChunkWorkerPool();
    ~ChunkWorkerPool();

    // 启动 N 个 worker 线程；generator 必须在调用方生命周期内有效，且其 fillChunkBuffer 是线程安全的
    void start(const TerrainGenerator* generator, int numThreads);
    void stop();

    // 投递一个 chunk 生成任务
    void submit(const glm::ivec2& pos);

    // 主线程每帧调用：拿出所有已完成的结果。返回的 vector 已被取走，pool 内部清空。
    std::vector<ChunkBuildResult> drainCompleted();

    // 当前队列待处理 + 正在跑的总数
    int pendingCount() const { return m_pending.load(std::memory_order_relaxed); }

private:
    void workerMain();
    void buildOne(const glm::ivec2& pos, ChunkBuildResult& out) const;

    const TerrainGenerator* m_generator = nullptr;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{ false };

    std::mutex m_jobMutex;
    std::condition_variable m_jobCV;
    std::deque<glm::ivec2> m_jobs;

    std::mutex m_doneMutex;
    std::vector<ChunkBuildResult> m_done;

    std::atomic<int> m_pending{ 0 };
};
