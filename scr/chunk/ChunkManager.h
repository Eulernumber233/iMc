#pragma once
//#include "Chunk.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <glm/glm.hpp>
#include "BlockType.h"
#include "ChunkArena.h"
#include "../Camera.h"
#include <mutex>

// 区块键（64位整数）
using ChunkKey = int64_t;

// 与 OpenGL 4.3 的 DrawElementsIndirectCommand 二进制布局一致
struct DrawElementsIndirectCommand {
    GLuint count;          // 顶点索引数（每个面 6）
    GLuint instanceCount;  // 该 chunk 的面数
    GLuint firstIndex;     // 共享索引起点（0）
    GLint  baseVertex;     // 共享顶点起点（0）
    GLuint baseInstance;   // 该 chunk 在 arena 中的实例偏移
};

// 区块管理器 - 负责加载、卸载和管理区块
class Chunk;
class TerrainGenerator;
class ChunkManager {
public:
    // 构造函数/析构函数
    ChunkManager(unsigned int seed);
    ~ChunkManager();

    // 初始化
    void initialize(int renderRadius , const glm::vec3& cameraPos);

    // 更新（每帧调用）
    void update(std::shared_ptr<Camera> camera);

    // 当前帧的 indirect 绘制命令（per-chunk 一条）
    const std::vector<DrawElementsIndirectCommand>& getDrawCommands() const {
        return m_drawCommands;
    }
    // arena VBO（实例属性数据源）
    GLuint getArenaVBO() const { return m_arena.getVBO(); }
    // CPU 端 indirect buffer 已经填好的 GL 对象（绑定后即可 MDI）
    GLuint getIndirectBuffer() const { return m_indirectBuffer; }
    int getVisibleInstanceCount() const { return m_visibleInstanceCount; }
    std::mutex& getRenderDataMutex() { return m_renderDataMutex; }

    // 获取区块
    Chunk* getChunk(const glm::ivec2& chunkPos);
    Chunk* getChunk(const int x, const int z);
    Chunk* getChunkAtWorld(const glm::vec3& worldPos);
	const std::vector<Chunk*>& getVisibleChunks()const { return m_visibleChunks; }
    std::vector<glm::ivec2> getVisibleChunkPositions()const;
    // 设置渲染半径
    void setRenderRadius(int radius);

    // 获取统计信息
    int getLoadedChunkCount() const { return m_loadedChunks.size(); }
    int getVisibleChunkCount() const { return m_visibleChunks.size(); }
    int getTotalInstances() const;
    void printStats() const;

    std::shared_ptr<TerrainGenerator> getTerrainGenerator() { return generator; }

    // 方块修改与查询
    bool setBlock(const glm::ivec3& worldPos, BlockType type);
    BlockType getBlockAt(const glm::ivec3& worldPos);// const

private:
    std::shared_ptr<TerrainGenerator> generator = nullptr;

    std::shared_ptr<Camera> m_camera;
    // 区块存储
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>> m_loadedChunks;
    std::vector<Chunk*> m_visibleChunks;

    // GPU 端实例数据：所有 chunk 共用一块大 VBO，每个 chunk 占据其中一段
    ChunkArena m_arena;
    // 每个已分配 slot 的 chunk 的对应 slot（key 同 m_loadedChunks）
    std::unordered_map<ChunkKey, ChunkArena::Slot> m_chunkSlots;

    // 本帧 indirect 命令（CPU 端构建，写入 m_indirectBuffer 后 MDI 提交）
    std::vector<DrawElementsIndirectCommand> m_drawCommands;
    GLuint m_indirectBuffer = 0;
    GLsizeiptr m_indirectBufferCapacityBytes = 0;
    int m_visibleInstanceCount = 0;

    mutable std::mutex m_renderDataMutex;

    // 渲染半径
    int m_renderRadius;
    glm::ivec2 m_currentCenterChunk;

    // 加载队列
    std::queue<glm::ivec2> m_loadQueue;

    // 每帧最大加载数量
    static constexpr int MAX_LOADS_PER_FRAME = 2;

    // 私有方法
    ChunkKey chunkPosToKey(const glm::ivec2& pos) const;

    // 更新可见区块列表
    void updateVisibleChunks(const glm::vec3& cameraPos);

    // 加载和卸载区块
    void loadChunk(const glm::ivec2& chunkPos);
    void unloadChunk(const glm::ivec2& chunkPos);

    // 把脏 chunk 的 mesh 上传到 arena，构建本帧 indirect 命令
    void rebuildDrawCommands();

    // 把单个 chunk 的最新 mesh 上传到 arena（必要时分配/换段）
    void uploadChunkToArena(Chunk* chunk);
    // 释放 chunk 的 arena slot
    void releaseChunkSlot(ChunkKey key);
    // 把 m_drawCommands 同步到 GPU indirect buffer
    void syncIndirectBuffer();

    // 检查区块是否应该可见
    bool shouldChunkBeVisible(const glm::ivec2& chunkPos,
        const glm::ivec2& centerChunk) const;

    // 计算区块距离
    float distanceToCamera(const glm::ivec2& chunkPos, const glm::vec3& cameraPos) const;
};