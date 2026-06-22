#pragma once
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

class ChunkManager {
public:
    ChunkManager(unsigned int seed);
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
    // 查找 pending 或 loaded 中的区块（用于出生点计算等只需 block 数据的场景）
    Chunk* getChunkAnyState(const glm::ivec2& chunkPos);
    const std::vector<Chunk*>& getActiveChunks() const { return m_activeChunks; }
    std::vector<glm::ivec2> getActiveChunkPositions() const;
    void setRenderRadius(int radius);

    int getLoadedChunkCount() const { return (int)m_loadedChunks.size(); }
    int getActiveChunkCount() const { return (int)m_activeChunks.size(); }
    int getInFlightCount() const { return (int)m_inFlight.size(); }
    void printStats() const;

    std::shared_ptr<TerrainGenerator> getTerrainGenerator() { return m_generator; }

    // 放置方块。state 包含 type + orient + 预留位；对带轴向方块（hasAxis == true）
    // 上层（Item 等）应当根据放置方向算出 orient 后塞进 state。
    bool setBlock(const glm::ivec3& worldPos, BlockState state);
    BlockState getBlockAt(const glm::ivec3& worldPos);

    static SectionKey makeSectionKey(int chunkX, int chunkZ, int sectionY);

    // 存档管理
    void setSaveManager(ChunkSaveManager* sm);
    void saveAllDirtyChunks();

private:
    std::shared_ptr<TerrainGenerator> m_generator;

    std::shared_ptr<Camera> m_camera;

    // PHASE 2：已完成横向 stitch、可参与渲染与玩家交互的 chunk。
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>> m_loadedChunks;

    // PHASE 1：已生成方块 + 内部/垂直可见面、但还有横向边界未缝合的 chunk。
    // 仅主线程读写。worker 的 stitch 任务只可能涉及此容器中的 chunk
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>> m_pendingChunks;

    // "活跃" chunk：玩家附近、需要逻辑更新与渲染候选的 chunk。
    // 渲染层剔除（空section + 视锥 + 距离 + 纵向）由 Chunk::getVisibleSectionMask 统一完成。
    std::vector<Chunk*> m_activeChunks;

    // 投递加载区块任务 load及 stitch
    ChunkWorkerPool m_workerPool;
    std::unordered_set<ChunkKey> m_inFlight;
    // 主线程持有：每帧从 worker drain 出来后先放到这里，按 MAX_INTEGRATE_PER_FRAME 慢慢消化，
    // 避免单帧瞬时 adoptSections 多次叠加成 16ms 尖峰。
    std::deque<ChunkBuildResult> m_pendingIntegrate;

    // 渲染数据缓冲区管理
    ChunkArena m_arena;
    std::unordered_map<SectionKey, ChunkArena::Slot> m_sectionSlots;

    // 渲染指令
    std::vector<DrawElementsIndirectCommand> m_drawCommands;
    GLuint m_indirectBuffer = 0;
    GLsizeiptr m_indirectBufferCapacityBytes = 0;
    int m_visibleInstanceCount = 0;

    // 与 m_drawCommands 同序：第 i 条命令对应的 section base 世界坐标
    // GPU 端布局为 vec4[]，xyz = (chunkX*16, sectionY*16, chunkZ*16)，w 保留。
    // shader 通过 gl_DrawID 索引此数组还原方块世界坐标。
    std::vector<glm::vec4> m_sectionBases;
    GLuint m_sectionBaseSSBO = 0;
    GLsizeiptr m_sectionBaseCapacityBytes = 0;

    int m_renderRadius;
    glm::ivec2 m_currentCenterChunk;

    // 走出 renderRadius + EVICT_MARGIN_CHUNKS 的 chunk 释放 GPU slot（CPU 数据保留，等后续存档系统接入再彻底卸载）。
    // hysteresis 避免边界来回走时反复释放/重传。
    static constexpr int EVICT_MARGIN_CHUNKS = 2;

    // 异步 stitch 需要外圈一格的 "陪练" chunk 也已生成，否则 render 边缘的 chunk 永远凑不齐 4 邻居。
    // 因此 build 任务的请求半径 = renderRadius + STITCH_PRELOAD_MARGIN，外圈 chunk 进 m_pendingChunks 但
    // 不会被晋升到 m_loadedChunks（它们自己缺一个外侧邻居）。
    static constexpr int STITCH_PRELOAD_MARGIN = 1;

    // 超出 renderRadius + UNLOAD_MARGIN_CHUNKS 的 chunk 会被保存并从内存彻底卸载。
    // 必须 > EVICT_MARGIN_CHUNKS，形成 hysteresis 避免来回加载卸载。
    static constexpr int UNLOAD_MARGIN_CHUNKS = 6;

    // 每帧最多接管多少个 build 结果。多 worker 时一帧 drain 出 8+ 个 chunk 的尖峰
    // 会导致 adoptSections 串行 ~16ms（profiler 实测），把帧率打回原形。把消化量摊到多帧，
    // 总吞吐不变（worker 已经做完了，只是结果在 m_buildDone 队列里多等几帧）。
    static constexpr int MAX_INTEGRATE_PER_FRAME = 4;

    // 从 RuntimeConfig 读
    int m_maxUploadsPerFrame = 8;
    int m_maxInflightRequests = 64;
    int m_autoSaveIntervalSec = 60;

    // 存档
    ChunkSaveManager* m_saveManager = nullptr;
    double m_lastSaveCheckTime = 0.0;
    float  m_autoSaveTimer = 0.0f;

    ChunkKey chunkPosToKey(const glm::ivec2& pos) const;

    // 重新计算 m_activeChunks（玩家附近半径内的 chunk）
    void updateActiveChunks(const glm::vec3& cameraPos);

    void requestChunkLoad(const glm::ivec2& chunkPos);
    // 扫一遍半径内还没加载且不在 in-flight 队列里的 chunk，从近到远补投递
    void requestMissingChunks();
    void integrateBuiltChunks();
    // worker 完成的 stitch 任务回主线程：更新 stitch 状态位，必要时晋升 chunk 到 loaded。
    void integrateStitchResults();

    // 在 m_pendingChunks 中查 chunk（不查 loaded）
    Chunk* getPendingChunk(const glm::ivec2& chunkPos);

    // 给定一个 pending chunk，扫它 4 方向：邻居在 pending 且双方该方向都未 stitch
    // 也未在投递中 → 投递 stitch 任务并标记 pending。
    void trySubmitStitchJobs(Chunk* chunk);
    // 若 pending chunk 的 4 方向 stitch 全部完成，把它移入 m_loadedChunks。
    void promoteIfReady(const glm::ivec2& chunkPos);

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

    // 释放走出 evict 半径的 chunk 的所有 section GPU slot；CPU 数据保留。
    void evictFarChunkSlots();

    // 卸载走出 unload 半径的 chunk：先保存，再从 m_loadedChunks 移除
    void unloadDistantChunks();

    // 将单个 chunk 的方块数据写入存档
    void saveChunkToDisk(Chunk* chunk);

    // 定时自动保存：遍历 m_loadedChunks，保存所有 m_saveDirty 的 chunk
    void doAutoSave();

    void linkNeighbors(Chunk* newChunk);
};
