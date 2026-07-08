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

// Task 1 输出：方块数据 + 光源列表 + 区块内光照（无 mesh）
// 光照用 shared_ptr<SectionLightData> 共享，与 BlockBox 的 shared_ptr 模式一致，避免拷贝。
struct BlockDataResult {
    glm::ivec2 pos;
    ChunkBoxes boxes;                             // 16 个 section 的方块数据 + 锁（shared_ptr）
    std::vector<LightSource> lightSources;        // Task 1 扫描到的发光方块（供主线程注册）
    ChunkLightData sectionLightData;              // Task 1 区块内 BFS 结果（shared_ptr 共享）
};

// Task 2 输入：自身 + 4 横向邻居的 BlockBox（shared_ptr）+ 光照数据
// 光照和方块数据均用 shared_ptr 共享，拷贝 MeshBuildInput 仅拷贝指针（与 ChunkBoxes 一致）。
struct MeshBuildInput {
    glm::ivec2 pos;
    ChunkBoxes self;            // 自身方块数据（shared_ptr 共享）
    ChunkBoxes neighbors[4];    // 邻居方块数据（缺失方向各 ptr 为 nullptr）
    ChunkLightData selfLightData; // Task 1 区块内 BFS 结果（shared_ptr 共享）
    // 邻居边界光照层：neighborBoundaryLight[d][sy] = 邻居 d 的 section sy 边界 16×16 RGBA8
    // +X→邻居 x=0 面：[y*16+z]；-X→邻居 x=15 面：[y*16+z]；
    // +Z→邻居 z=0 面：[y*16+x]；-Z→邻居 z=15 面：[y*16+x]。
    // 每个边界层仅 256 个 uint32_t（1KB），4×16=64 层共 64KB，体量小故保留值拷贝。
    std::array<std::array<std::vector<uint32_t>, CHUNK_SECTION_COUNT>, 4> neighborBoundaryLight;
};

// Task 2 输出：含完整可见面的 section 数组 + 每 section 的最终光照（shared_ptr 共享）
struct ChunkBuildResult {
    glm::ivec2 pos;
    static constexpr int SECTION_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;
    std::array<Section, SECTION_COUNT> sections;
    ChunkLightData sectionLightData; // Task 2 产出最终光照（shared_ptr 共享）
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

    // 主线程每帧调用：拿出已完成的 Task 1 结果（unique_ptr 避免 move 大对象）
    std::vector<std::unique_ptr<BlockDataResult>> drainBlockData();
    // 主线程每帧调用：拿出已完成的 Task 2 结果
    std::vector<std::unique_ptr<ChunkBuildResult>> drainMeshResults();

    // 当前队列待处理任务数
    int pendingCount() const { return m_pending.load(std::memory_order_relaxed); }

    // 设置存档管理器指针（用于 buildOne 中尝试从磁盘加载）
    void setSaveManager(const class ChunkSaveManager* sm) { m_saveManager = sm; }

private:
    void workerMain();
    void buildOne(const glm::ivec2& pos, BlockDataResult& out) const;
    void meshBuildOne(const MeshBuildInput& in, ChunkBuildResult& out) const;
    // 网络导入：解压 serialized → 切片成 16 个 box。失败（数据损坏）时 out.boxes 全 nullptr。
    static void netImportOne(const glm::ivec2& pos, const std::vector<uint8_t>& serialized,
                             BlockDataResult& out);

    enum JobKind : uint8_t { JOB_BUILD = 0, JOB_MESH = 1, JOB_NET_IMPORT = 2 };

    struct Job {
        JobKind kind;
        glm::ivec2 pos{ 0, 0 };
        MeshBuildInput meshInput{};
        std::vector<uint8_t> netData;  // JOB_NET_IMPORT：单 chunk 的序列化字节
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
