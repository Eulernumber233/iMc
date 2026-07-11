#pragma once
#include "../core.h"
#include "BlockType.h"
#include "BlockBox.h"
#include "ChunkDimensions.h"
#include "Section.h"
#include "../light/LightSource.h"
#include "../light/LightCache.h"  // SectionLightData / ChunkLightData 类型
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <glm/glm.hpp>

class TerrainGenerator;
class Chunk;

// 每个 chunk 的 section 数量
static constexpr int CHUNK_SECTION_COUNT =
    ChunkConstants::CHUNK_HEIGHT / ChunkConstants::SECTION_HEIGHT;

// ChunkBoxes 定义见 BlockBox.h

// Task 1 输出：方块数据（无光照）
struct BlockDataResult {
    glm::ivec2 pos;
    ChunkBoxes boxes;    // 16 个 section 的方块数据 + 锁（shared_ptr）
};

// Task 2 输入：自身 + 4 横向邻居的 BlockBox（shared_ptr，仅拷指针）
struct MeshBuildInput {
    glm::ivec2 pos;
    ChunkBoxes self;            // 自身方块数据（shared_ptr 共享）
    ChunkBoxes neighbors[4];    // 邻居方块数据（缺失方向各 ptr 为 nullptr）
};

// Task 2 输出：含完整可见面的 section 数组（纯几何，不含光照）
struct ChunkBuildResult {
    glm::ivec2 pos;
    static constexpr int SECTION_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;
    std::array<Section, SECTION_COUNT> sections;
};

// ── Task 3 光照 BFS ──────────────────────────────────────────────────

// Task 3 输入：中心区块 + 8 个 Moore 邻居的 BlockBox（shared_ptr 共享，仅拷指针）
struct LightBuildInput {
    glm::ivec2 pos;
    ChunkBoxes self;              // 中心区块的 16 个 box
    ChunkBoxes neighbors[8];      // 8 个 Moore 邻居的 box（缺失方向各 ptr 为 nullptr）
};

// Task 3 输出：中心区块的完整光照（3×3 区域完整 BFS）
struct LightBuildResult {
    glm::ivec2 pos;
    ChunkLightData sectionLightData; // 最终光照（shared_ptr 共享）
    // 中心区块 per-section 光源位置（从 9 区块扫描而来）
    std::array<std::shared_ptr<std::vector<uint16_t>>, CHUNK_SECTION_COUNT> sectionLightSources;
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

    // 网络导入（客户端）：把单个 chunk 的「已序列化+LZ4 压缩」字节（含 chunkX/Z 头 + 各 section）
    // 投递到 worker，由 worker 解压 + 切片成 16 个 BlockBox，产出 BlockDataResult 进 block 完成队列，
    // 与 JOB_BUILD 同构——主线程 drainBlockData / integrateBlockData 直接复用，无需单独集成路径。
    void submitNetImport(int chunkX, int chunkZ, std::vector<uint8_t>&& serialized);

    // Task 3：光照 BFS（仅当 8 个 Moore 邻居都已 loaded 时投递）
    void submitLightBuild(LightBuildInput&& input);

    // 主线程每帧调用：拿出已完成的 Task 1 结果
    std::vector<std::unique_ptr<BlockDataResult>> drainBlockData();
    // 主线程每帧调用：拿出已完成的 Task 2 结果
    std::vector<std::unique_ptr<ChunkBuildResult>> drainMeshResults();
    // 主线程每帧调用：拿出已完成的 Task 3 结果
    std::vector<std::unique_ptr<LightBuildResult>> drainLightResults();

    // 当前队列待处理任务数（Task 1/2/网络导入，不含 Task 3）
    int pendingCount() const { return m_pending.load(std::memory_order_relaxed); }

    // 设置存档管理器指针（用于 buildOne 中尝试从磁盘加载）
    void setSaveManager(const class ChunkSaveManager* sm) { m_saveManager = sm; }

private:
    void workerMain();
    void buildOne(const glm::ivec2& pos, BlockDataResult& out) const;
    void meshBuildOne(const MeshBuildInput& in, ChunkBuildResult& out) const;
    // Task 3：从 3×3 区块做一次完整 BFS，产出中心 chunk 的光照
    static void lightBuildOne(const LightBuildInput& in, LightBuildResult& out);
    // 网络导入：解压 serialized → 切片成 16 个 box
    static void netImportOne(const glm::ivec2& pos, const std::vector<uint8_t>& serialized,
                             BlockDataResult& out);

    enum JobKind : uint8_t { JOB_BUILD = 0, JOB_MESH = 1, JOB_NET_IMPORT = 2, JOB_LIGHT = 3 };

    struct Job {
        JobKind kind;
        glm::ivec2 pos{ 0, 0 };
        MeshBuildInput meshInput{};
        std::vector<uint8_t> netData;  // JOB_NET_IMPORT：单 chunk 的序列化字节
        // 注：JOB_LIGHT 走独立队列 m_lightJobs，不经过本结构体
    };

    const TerrainGenerator* m_generator = nullptr;
    const class ChunkSaveManager* m_saveManager = nullptr;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_stop{ false };

    // 常规任务队列（Task 1 / Task 2 / 网络导入）
    std::mutex m_jobMutex;
    std::condition_variable m_jobCV;
    std::deque<Job> m_jobs;

    // Task 3 独立队列：单 worker 消费（通过 m_lightJobRunning 互斥）
    std::mutex m_lightJobMutex;
    std::deque<LightBuildInput> m_lightJobs;
    std::atomic<bool> m_lightJobRunning{ false };

    // 完成队列：Task 1 (block data) / Task 2 (mesh) / Task 3 (light)
    std::mutex m_blockDoneMutex;
    std::deque<std::unique_ptr<BlockDataResult>> m_blockDone;
    std::mutex m_meshDoneMutex;
    std::deque<std::unique_ptr<ChunkBuildResult>> m_meshDone;
    std::mutex m_lightDoneMutex;
    std::deque<std::unique_ptr<LightBuildResult>> m_lightDone;

    std::atomic<int> m_pending{ 0 };
};
