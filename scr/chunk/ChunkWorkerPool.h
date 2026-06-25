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

// Task 1 输出：仅方块数据，无 mesh
struct BlockDataResult {
    glm::ivec2 pos;
    static constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    std::unique_ptr<BlockState[]> blocks; // VOL elements
};

// Task 2 输入：自身 + 4 横向邻居的方块数据（只读）
struct MeshBuildInput {
    glm::ivec2 pos;
    const BlockState* selfBlocks;        // VOL elements, owned by BlockReadyEntry
    const BlockState* neighborBlocks[4]; // ±X/±Z, may be nullptr if missing
};

// Task 2 输出：含完整可见面（内部 + 全部边界）的 section 数组
struct ChunkBuildResult {
    glm::ivec2 pos;
    static constexpr int SECTION_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;
    std::array<Section, SECTION_COUNT> sections;
};

class ChunkWorkerPool {
public:
    ChunkWorkerPool();
    ~ChunkWorkerPool();

    // 启动 N 个 worker 线程；generator 必须在调用方生命周期内有效
    void start(const TerrainGenerator* generator, int numThreads);
    void stop();

    // Task 1：生成/加载方块数据
    void submitBuild(const glm::ivec2& pos);

    // Task 2：生成完整可见面（含边界）
    void submitMeshBuild(const MeshBuildInput& input);

    // 主线程每帧调用：拿出已完成的 Task 1 结果
    std::vector<BlockDataResult> drainBlockData();
    // 主线程每帧调用：拿出已完成的 Task 2 结果
    std::vector<ChunkBuildResult> drainMeshResults();

    // 当前队列待处理任务数
    int pendingCount() const { return m_pending.load(std::memory_order_relaxed); }

    // 设置存档管理器指针（用于 buildOne 中尝试从磁盘加载）
    void setSaveManager(const class ChunkSaveManager* sm) { m_saveManager = sm; }

private:
    void workerMain();
    void buildOne(const glm::ivec2& pos, BlockDataResult& out) const;
    void meshBuildOne(const MeshBuildInput& in, ChunkBuildResult& out) const;

    enum JobKind : uint8_t { JOB_BUILD = 0, JOB_MESH = 1 };

    struct Job {
        JobKind kind;
        glm::ivec2 pos{ 0, 0 };
        MeshBuildInput meshInput{};
    };

    const TerrainGenerator* m_generator = nullptr;
    const class ChunkSaveManager* m_saveManager = nullptr;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{ false };

    // 任务队列
    std::mutex m_jobMutex;
    std::condition_variable m_jobCV;
    std::deque<Job> m_jobs;

    // 完成队列：Task 1 (block data) / Task 2 (mesh)
    std::mutex m_blockDoneMutex;
    std::deque<std::unique_ptr<BlockDataResult>> m_blockDone;
    std::mutex m_meshDoneMutex;
    std::deque<std::unique_ptr<ChunkBuildResult>> m_meshDone;

    std::atomic<int> m_pending{ 0 };
};
