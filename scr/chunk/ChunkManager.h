#pragma once
//#include "Chunk.h"
#include <unordered_map>
#include <vector>
#include <queue>
#include <glm/glm.hpp>
#include "BlockType.h"
#include "../Camera.h"
// 区块键（64位整数）
using ChunkKey = int64_t;

//  


// 区块管理器 - 负责加载、卸载和管理区块
class Chunk;
class ChunkManager {
public:
    // 构造函数/析构函数
    ChunkManager();
    ~ChunkManager();

    // 初始化
    void initialize(int renderRadius , const glm::vec3& cameraPos);

    // 更新（每帧调用）
    void update(std::shared_ptr<Camera> camera);

    // 获取渲染数据
    const std::unordered_map<BlockFaceType, std::vector<glm::mat4>>& getRenderData() const {
        return m_renderData;
    }

    // 获取区块
    Chunk* getChunk(const glm::ivec2& chunkPos);
    Chunk* getChunk(const int x, const int z);
    Chunk* getChunkAtWorld(const glm::vec3& worldPos);

    // 设置渲染半径
    void setRenderRadius(int radius);

    // 获取统计信息
    int getLoadedChunkCount() const { return m_loadedChunks.size(); }
    int getVisibleChunkCount() const { return m_visibleChunks.size(); }
    int getTotalInstances() const;
    void printStats() const;

private:
    std::shared_ptr<Camera> m_camera;
    // 区块存储
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>> m_loadedChunks;
    std::vector<Chunk*> m_visibleChunks;

    // 渲染数据（合并所有可见区块的实例化数据）
    std::unordered_map<BlockFaceType, std::vector<glm::mat4>> m_renderData;

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

    // 合并渲染数据
    void mergeRenderData();

    // 检查区块是否应该可见
    bool shouldChunkBeVisible(const glm::ivec2& chunkPos,
        const glm::ivec2& centerChunk) const;

    // 计算区块距离
    float distanceToCamera(const glm::ivec2& chunkPos, const glm::vec3& cameraPos) const;
};