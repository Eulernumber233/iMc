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
#include "../light/LightSource.h"
#include "../light/LightCache.h"
#include "../light/LightPropagation.h"
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
    std::vector<LightSource> lightSources; // Task 1 worker 扫描到的发光方块（主线程注册后用）
    ChunkLightData sectionLightData; // Task 1 区块内 BFS 结果（shared_ptr 共享）
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
    int getRenderRadius() const { return m_renderRadius; }

    std::vector<glm::ivec2> getLoadedChunkPositions() const;
    // block-ready（有数据未 mesh）的 chunk 坐标，供服务端按需向远程玩家增量推送。
    std::vector<glm::ivec2> getBlockReadyChunkPositions() const;

    // 增量推送事件队列（服务端网络同步用）：每当一个 chunk 晋升到 BLOCK_READY 或
    // LOADED 状态时记录其坐标。NetChunkSync::pushChunks 每帧 swap 取走整批，
    // 替代过去每帧全量扫描 m_loadedChunks/m_blockReady 的 O(chunk数) 开销。
    // 仅服务端会消费；非网络会话该队列始终为空（晋升点判断 m_trackPromotions）。
    // 开启时会把当前已有的 block-ready / loaded chunk 一次性灌入队列，
    // 使开启前就晋升的 chunk 也能被增量推送（不依赖 pushAllChunks 兜底 block-ready）。
    void setTrackPromotions(bool v);
    // O(1) swap 取走本批晋升事件（取走后清空内部队列）。
    void drainPromotedChunks(std::vector<glm::ivec2>& out) {
        out.clear();
        out.swap(m_promotedChunks);
    }
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

    // ── 光源管理 ──────────────────────────────────────────────────
    // 光照缓存管理器（每 section 的 RGB 光照值，供 GPU 延迟光照采样）
    LightCacheManager& getLightCache() { return m_lightCache; }
    const LightCacheManager& getLightCache() const { return m_lightCache; }

    // 方块变动后触发增量光照更新（仅重传播 affected 区域）
    void notifyLightChange(const glm::ivec3& worldPos);

    // 每帧调用：处理待定的光照变化并上传到 GPU
    void updateLighting();

    // 区块加载后扫描注册其内部发光方块（emissive > 0），并触发重传播
    void registerChunkLightSources(class Chunk* chunk);

    // 区块卸载前移除其光源注册并清理光照缓存
    void unregisterChunkLightSources(const glm::ivec2& chunkPos);

    // 网络统一点修改入口：把一次方块改动应用到本地权威数据。
    //  - chunk 在 LOADED：走 setBlock 改面 + 标脏（GPU 增量 patch 自动跟上）
    //  - chunk 在 BLOCK_READY：直接改对应 box（持写锁），并记入待存盘集合
    //  - chunk 不存在：丢弃（服务端不会把改动发给没加载该 chunk 的客户端）
    // 返回 true 表示已应用（loaded 或 block-ready 命中）。
    bool applyBlockChange(const glm::ivec3& worldPos, BlockState state);

    // 用户发起方块修改的重定向 sink。设置后，setBlock() 不再本地生效，而是转交 sink
    // （网络会话：客户端发请求 / 服务端应用+广播）。单机模式不设置，setBlock 直接本地应用。
    void setBlockChangeSink(std::function<void(const glm::ivec3&, BlockState)> fn) {
        m_blockChangeSink = std::move(fn);
    }

    static SectionKey makeSectionKey(int chunkX, int chunkZ, int sectionY);

    uint32_t getVisibilityGeneration() const { return m_visGeneration; }

    // 网络导入（客户端）：把单 chunk 的「已序列化+LZ4 压缩」字节投递到 worker 线程，
    // 由 worker 解压 + 切片成 BlockBox（绕过地形生成），产出走 block 完成队列、与本地生成
    // 同一条 integrateBlockData 路径落入 m_blockReady。主线程在此只做一次 in-flight 标记 +
    // 入队，解压这件重活不再占主线程（CHUNK_DATA 高频/批量到达时尤其受益）。
    void submitNetworkChunkImport(int chunkX, int chunkZ,
                                  std::vector<uint8_t>&& serializedChunk);

    // 网络客户端模式
    void setNetworkClient(bool v) { m_networkClient = v; }
    bool isNetworkClient() const { return m_networkClient; }

    // 网络 chunk 请求回调（客户端向服务端请求 chunk）
    void setNetworkChunkRequester(std::function<void(int, int)> fn) {
        m_onRequestChunk = std::move(fn);
    }

    // chunk 卸载回调（服务端用：通知 NetChunkSync 清除该 chunk 的 per-peer 已推送记录，
    // 使玩家再次靠近时能重新推送最新数据）。
    void setChunkUnloadedCallback(std::function<void(int, int)> fn) {
        m_onChunkUnloaded = std::move(fn);
    }

    // 方块变动通知回调：本地方块真正被修改后触发，参数为
    //   (worldPos, oldState, newState)
    // 供光源注册表等子系统监听方块变动。
    void setBlockChangedCallback(std::function<void(const glm::ivec3&, BlockState, BlockState)> fn) {
        m_onBlockChanged = std::move(fn);
    }

    // 强制加载 chunk（服务端按需响应 CHUNK_REQUEST）
    void forceChunkLoad(const glm::ivec2& chunkPos);

    // 数据相关加载中心：chunk 坐标 + 该中心自己的渲染半径（per-player）。
    struct LoadCenter {
        glm::ivec2 center;
        int radius;
        bool operator==(const LoadCenter& o) const {
            return center == o.center && radius == o.radius;
        }
    };

    // 多锚点加载（阶段 B）：设置所有"数据相关"加载中心（坐标 + 各自半径）。
    // 服务端每帧灌入 Host + 所有远程玩家；客户端/单机留空 → 内部退化为本机相机中心 + m_renderRadius。
    // 注意：这是"数据相关"量纲，决定生成/落盘/卸载/推送；mesh 仍只看本机相机（渲染相关）。
    void setLoadCenters(std::vector<LoadCenter> centers) {
        if (centers != m_loadCenters) {
            m_loadCenters = std::move(centers);
            m_needChunkScan = true;  // 加载中心变了才重新扫描缺失 chunk
        }
    }

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

    // Task 1 / Task 2 结果队列（unique_ptr 避免在队列间 move 大对象）
    std::deque<std::unique_ptr<BlockDataResult>> m_pendingBlockData;
    std::deque<std::unique_ptr<ChunkBuildResult>> m_pendingMeshResults;

    // 渲染数据缓冲区管理
    ChunkArena m_arena;
    std::unordered_map<SectionKey, ChunkArena::Slot> m_sectionSlots;

    // 渲染指令
    std::vector<DrawElementsIndirectCommand> m_drawCommands;
    GLuint m_indirectBuffer = 0;
    GLsizeiptr m_indirectBufferCapacityBytes = 0;
    int m_visibleInstanceCount = 0;

    // draw command 缓存：cull pass 的结果（m_drawCommands / m_sectionBases）仅在
    // 相机移动（m_visGeneration 变）或可见 section 集合变化（上传/加载/驱逐/卸载）时改变。
    // 静止帧直接复用上一帧结果，跳过遍历全部 active chunk 的 cull pass + GPU 重传。
    uint32_t m_lastBuiltVisGeneration = 0;  // 上次重建 cull 时的 visGeneration（0=从未建）
    bool m_drawListDirty = true;            // 可见 section 集合发生变化，需重建
    // 标记 draw list 需重建（section 上传/chunk 加载/驱逐/卸载时调用）。
    void markDrawListDirty() { m_drawListDirty = true; }

    // Section base SSBO
    std::vector<glm::vec4> m_sectionBases;
    GLuint m_sectionBaseSSBO = 0;
    GLsizeiptr m_sectionBaseCapacityBytes = 0;

    int m_renderRadius;
    glm::ivec2 m_currentCenterChunk;  // 渲染中心（本机相机所在 chunk）

    // 数据相关加载中心（Host + 远程玩家，各带自己的半径）。空 → 退化为仅本机相机。
    std::vector<LoadCenter> m_loadCenters;

    // GPU slot 逐出半径余量（hysteresis 避免边界来回抖动）
    static constexpr int EVICT_MARGIN_CHUNKS = 2;

    // 数据加载余量（曾名 BLOCK_PRELOAD_MARGIN）：render + DATA_MARGIN 是"数据相关"半径，
    // 让 renderRadius 边缘的 chunk 有外侧邻居提供方块数据以缝 mesh 边界。编译期常量
    // （由 mesh 邻居需求决定，与内存预算无关）。
    static constexpr int DATA_MARGIN = 1;

    // 温存/落盘半径余量（曾名 UNLOAD_MARGIN_CHUNKS）：render + RETAIN_MARGIN 内的 chunk
    // 离开渲染半径后仍保留在内存（不渲染、不卸载、不落盘），超出才落盘 + 卸载。
    // 运行时从 RuntimeConfig.retainMarginChunks 读，按内存预算可调。见 m_retainMargin。

    // 每帧最多处理多少个 Task 1 / Task 2 结果（分摊多 worker 同时完成的尖峰）
    static constexpr int MAX_BLOCK_INTEGRATE_PER_FRAME = 8;
    static constexpr int MAX_MESH_INTEGRATE_PER_FRAME = 4;

    // 网络请求超时（秒），超时后允许重新请求
    static constexpr double INFLIGHT_TIMEOUT_SEC = 5.0;

    // 从 RuntimeConfig 读
    int m_maxUploadsPerFrame = 8;
    int m_maxInflightRequests = 64;
    int m_autoSaveIntervalSec = 60;
    int m_retainMargin = 6;  // 温存/落盘半径余量（render + 此值），见 RETAIN 注释

    // 存档
    ChunkSaveManager* m_saveManager = nullptr;
    double m_lastSaveCheckTime = 0.0;
    float  m_autoSaveTimer = 0.0f;
    // 远距离卸载降频：chunk 不会一帧内跑到卸载半径外，无需每帧扫全部 loaded chunk。
    float  m_unloadTimer = 0.0f;
    static constexpr float UNLOAD_CHECK_INTERVAL_SEC = 0.5f;

    // 仍处于 BLOCK_READY（未 loaded、无 mesh）但被网络改动过的 chunk，
    // 需要在自动保存/退出时落盘。loaded chunk 由 Chunk::isSaveDirty 跟踪，
    // block-ready chunk 没有 Chunk 对象，故单独记一个集合（仅服务端有意义）。
    std::unordered_set<ChunkKey> m_blockReadyDirty;
    // 把 m_blockReadyDirty 中的 chunk 序列化落盘（从 box 直接写）。
    void saveDirtyBlockReadyChunks();
    // 把一个 block-ready chunk 的 16 个 box 序列化进整 chunk buffer（落盘布局），并落盘。
    void saveBlockReadyChunkToDisk(ChunkKey key, const ChunkBoxes& boxes);

    // 网络客户端
    bool m_networkClient = false;

    // 服务端增量推送：是否记录 chunk 晋升事件（仅 Host 会话开启）。
    bool m_trackPromotions = false;
    // 本帧新晋升到 BLOCK_READY / LOADED 的 chunk 坐标，供 NetChunkSync 增量消费。
    std::vector<glm::ivec2> m_promotedChunks;
    // 记录一次晋升事件（仅在 m_trackPromotions 为 true 时入队）。
    void notePromotedChunk(const glm::ivec2& pos) {
        if (m_trackPromotions) m_promotedChunks.push_back(pos);
    }

    // 网络请求回调
    std::function<void(int, int)> m_onRequestChunk;

    // 用户发起方块修改的重定向 sink（见 setBlockChangeSink）
    std::function<void(const glm::ivec3&, BlockState)> m_blockChangeSink;

    // chunk 卸载回调（见 setChunkUnloadedCallback）
    std::function<void(int, int)> m_onChunkUnloaded;

    // 方块变动通知回调（见 setBlockChangedCallback）
    std::function<void(const glm::ivec3&, BlockState, BlockState)> m_onBlockChanged;
    // setBlock 的真正本地实现（绕过 sink，供 sink 内部 / applyBlockChange 调用）
    bool applyLocalSetBlock(const glm::ivec3& worldPos, BlockState state);

    // 是否需要扫描缺失 chunk
    bool m_needChunkScan = true;

    // ── 光源系统 ──────────────────────────────────────────────────
    LightCacheManager m_lightCache;                // 每 section 光照缓存（GPU SSBO 管理）
    std::vector<glm::ivec3> m_pendingLightChanges; // 待处理的增量光照变动位置

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

    // 数据相关：chunkPos 是否在任一加载中心的「各自半径 + margin」内。
    // m_loadCenters 为空时退化为只看 m_currentCenterChunk + m_renderRadius + margin。
    bool isDataRelevant(const glm::ivec2& chunkPos, int margin) const;
    bool isWithinEvictRadius(const glm::ivec2& chunkPos,
        const glm::ivec2& centerChunk) const;

    void evictFarChunkSlots();
    void unloadDistantChunks();
    void saveChunkToDisk(Chunk* chunk);
    void doAutoSave();

    void linkNeighbors(Chunk* newChunk);
};
