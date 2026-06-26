#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <deque>
#include <queue>
#include <glm/glm.hpp>
#include "BlockType.h"
#include "ChunkArena.h"
#include "ChunkWorkerPool.h"
#include "../Camera.h"
#include <mutex>

using ChunkKey = int64_t;
using SectionKey = uint64_t; // (chunkX, chunkZ, sectionY) packed

// 与 OpenGL 4.3 的 DrawElementsIndirectCommand 二进制布局一致
struct DrawElementsIndirectCommand {
    GLuint count;
    GLuint instanceCount;
    GLuint firstIndex;
    GLint  baseVertex;
    GLuint baseInstance;
};

class Chunk;
class TerrainGenerator;
class Section;
class ChunkSaveManager;

// BLOCK_READY 状态：方块数据就绪，等待邻居完成以投递 Task 2
struct BlockReadyEntry {
    ChunkBoxes boxes;               // 16 个 section 的方块数据 + 锁（见 BlockBox.h）
    uint8_t neighborBlockReady = 0; // 4-bit: bit[i]=1 表示 m_neighbors[i] 方向已完成 Task 1
};

class ChunkManager {
public:
    ChunkManager(uint64_t seed);
    ~ChunkManager();

    void initialize(int renderRadius, const glm::vec3& cameraPos);
    void update(std::shared_ptr<Camera> camera);

    const std::vector<DrawElementsIndirectCommand>& getDrawCommands() const {
        return m_drawCommands;
    }
    GLuint getArenaVBO() const { return m_arena.getVBO(); }
    GLuint getIndirectBuffer() const { return m_indirectBuffer; }
    GLuint getSectionBaseSSBO() const { return m_sectionBaseSSBO; }
    int getVisibleInstanceCount() const { return m_visibleInstanceCount; }

    Chunk* getChunk(const glm::ivec2& chunkPos);
    Chunk* getChunk(const int x, const int z);
    // 查找 loaded 中的区块（有完整 mesh）
    Chunk* getChunkAnyState(const glm::ivec2& chunkPos);
    // 取某 chunk 的 16 个 section BlockBox（数据 + 锁），填入 out。BLOCK_READY 或 LOADED 均可。
    // 找到返回 true；out 持有 shared_ptr 保证使用期间 box 不被释放。
    bool getChunkBoxes(const glm::ivec2& chunkPos, ChunkBoxes& out);
    // 仅判断某 chunk 是否有方块数据（BLOCK_READY 或 LOADED）。
    bool hasBlockData(const glm::ivec2& chunkPos) const;
    const std::vector<Chunk*>& getActiveChunks() const { return m_activeChunks; }
    std::vector<glm::ivec2> getActiveChunkPositions() const;
    void setRenderRadius(int radius);

    std::vector<glm::ivec2> getLoadedChunkPositions() const;
    int getLoadedChunkCount() const { return (int)m_loadedChunks.size(); }
    int getActiveChunkCount() const { return (int)m_activeChunks.size(); }
    int getInFlightCount() const { return (int)m_inFlight.size(); }
    int getBlockReadyCount() const { return (int)m_blockReady.size(); }
    int getMeshInFlightCount() const { return (int)m_meshInFlight.size(); }
    void printStats() const;

    std::shared_ptr<TerrainGenerator> getTerrainGenerator() { return m_generator; }

    // 放置方块
    bool setBlock(const glm::ivec3& worldPos, BlockState state);
    BlockState getBlockAt(const glm::ivec3& worldPos);

    static SectionKey makeSectionKey(int chunkX, int chunkZ, int sectionY);

    uint32_t getVisibilityGeneration() const { return m_visGeneration; }

    // 网络导入：从网络接收整个 chunk 的方块数据（绕过地形生成）
    void importChunkData(int chunkX, int chunkZ,
                         std::unique_ptr<BlockState[]> blockBuffer);

    // 网络客户端模式
    void setNetworkClient(bool v) { m_networkClient = v; }
    bool isNetworkClient() const { return m_networkClient; }

    // 网络 chunk 请求回调（客户端向服务端请求 chunk）
    void setNetworkChunkRequester(std::function<void(int, int)> fn) {
        m_onRequestChunk = std::move(fn);
    }

    // 强制加载 chunk（服务端按需响应 CHUNK_REQUEST）
    void forceChunkLoad(const glm::ivec2& chunkPos);

    // 存档管理
    void setSaveManager(ChunkSaveManager* sm);
    void saveAllDirtyChunks();

private:
    std::shared_ptr<TerrainGenerator> m_generator;
    std::shared_ptr<Camera> m_camera;

    // === 区块状态容器 ===

    // LOADED：完整 mesh，可渲染可交互
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>> m_loadedChunks;

    // BLOCK_READY：方块数据就绪，等待邻居完成以投递 Task 2
    std::unordered_map<ChunkKey, BlockReadyEntry> m_blockReady;

    // Task 1 进行中（地形生成/磁盘加载/网络请求）— value 为请求时间戳
    std::unordered_map<ChunkKey, double> m_inFlight;

    // Task 2 进行中（mesh 构建）
    std::unordered_set<ChunkKey> m_meshInFlight;

    // "活跃" chunk：玩家附近、需要渲染的已 loaded chunk
    std::vector<Chunk*> m_activeChunks;

    // Worker 池
    ChunkWorkerPool m_workerPool;

    // Task 1 结果队列（从 worker drain 出来，按帧配额消化）
    std::deque<BlockDataResult> m_pendingBlockData;
    // Task 2 结果队列
    std::deque<ChunkBuildResult> m_pendingMeshResults;

    // 渲染数据缓冲区管理
    ChunkArena m_arena;
    std::unordered_map<SectionKey, ChunkArena::Slot> m_sectionSlots;

    // 渲染指令
    std::vector<DrawElementsIndirectCommand> m_drawCommands;
    GLuint m_indirectBuffer = 0;
    GLsizeiptr m_indirectBufferCapacityBytes = 0;
    int m_visibleInstanceCount = 0;

    // Section base SSBO
    std::vector<glm::vec4> m_sectionBases;
    GLuint m_sectionBaseSSBO = 0;
    GLsizeiptr m_sectionBaseCapacityBytes = 0;

    int m_renderRadius;
    glm::ivec2 m_currentCenterChunk;

    // GPU slot 逐出半径余量（hysteresis 避免边界来回抖动）
    static constexpr int EVICT_MARGIN_CHUNKS = 2;

    // 方块数据预加载余量：让 renderRadius 边缘的 chunk 有外侧邻居提供方块数据
    static constexpr int BLOCK_PRELOAD_MARGIN = 1;

    // 卸载半径余量
    static constexpr int UNLOAD_MARGIN_CHUNKS = 6;

    // 每帧最多处理多少个 Task 1 / Task 2 结果（分摊多 worker 同时完成的尖峰）
    static constexpr int MAX_BLOCK_INTEGRATE_PER_FRAME = 8;
    static constexpr int MAX_MESH_INTEGRATE_PER_FRAME = 4;

    // 网络请求超时（秒），超时后允许重新请求
    static constexpr double INFLIGHT_TIMEOUT_SEC = 5.0;

    // 从 RuntimeConfig 读
    int m_maxUploadsPerFrame = 8;
    int m_maxInflightRequests = 64;
    int m_autoSaveIntervalSec = 60;

    // 存档
    ChunkSaveManager* m_saveManager = nullptr;
    double m_lastSaveCheckTime = 0.0;
    float  m_autoSaveTimer = 0.0f;

    // 网络客户端
    bool m_networkClient = false;

    // 网络请求回调
    std::function<void(int, int)> m_onRequestChunk;

    // 是否需要扫描缺失 chunk
    bool m_needChunkScan = true;

    // 可见性缓存版本号
    uint32_t m_visGeneration = 1;
    glm::vec3 m_lastVisCameraPos{ 0.0f };
    float m_lastVisCameraYaw = 9999.0f;
    float m_lastVisCameraPitch = 99999.0f;

    ChunkKey chunkPosToKey(const glm::ivec2& pos) const;
    void updateActiveChunks(const glm::vec3& cameraPos);

    // Task 1 投递
    void requestChunkLoad(const glm::ivec2& chunkPos);
    void requestMissingChunks();

    // 主线程集成
    void integrateBlockData();
    void integrateMeshResults();

    // 邻居标记：chunk 完成 Task 1 后，通知4邻居，检查是否可投递 Task 2
    void notifyNeighborsBlockReady(const glm::ivec2& pos);
    void checkAndSubmitMesh(const glm::ivec2& pos);
    void submitMeshTask(const glm::ivec2& pos);

    // 将 Task 2 结果装入 Chunk 并放入 m_loadedChunks
    void loadMeshResult(ChunkBuildResult& result);

    void rebuildDrawCommands();
    void uploadSection(int chunkX, int chunkZ, int sectionY, Section& section, int& uploadBudget);
    void releaseSectionSlot(SectionKey key);
    void syncIndirectBuffer();
    void syncSectionBaseSSBO();

    bool isWithinActiveRadius(const glm::ivec2& chunkPos,
        const glm::ivec2& centerChunk) const;
    bool isWithinEvictRadius(const glm::ivec2& chunkPos,
        const glm::ivec2& centerChunk) const;
    bool isWithinUnloadRadius(const glm::ivec2& chunkPos,
        const glm::ivec2& centerChunk) const;

    void evictFarChunkSlots();
    void unloadDistantChunks();
    void saveChunkToDisk(Chunk* chunk);
    void doAutoSave();

    void linkNeighbors(Chunk* newChunk);
};
