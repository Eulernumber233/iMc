#include "ChunkManager.h"
#include <iostream>
#include <algorithm>
#include "Chunk.h"
ChunkManager::ChunkManager()
    : m_renderRadius(8) {
}

ChunkManager::~ChunkManager() {
    // 清理所有区块
    m_loadedChunks.clear();
    m_visibleChunks.clear();

    // 清理渲染数据
    for (auto& pair : m_renderData) {
        pair.second.clear();
        pair.second.shrink_to_fit();
    }
}

void ChunkManager::initialize(int renderRadius, const glm::vec3& cameraPos) {
    m_renderRadius = renderRadius;
    m_currentCenterChunk = glm::ivec2(0);
    //update(cameraPos);
    updateVisibleChunks(cameraPos);
}

void ChunkManager::update(std::shared_ptr<Camera> camera) {
    m_camera = camera;
    // 计算当前相机所在的区块
    glm::ivec2 cameraChunk(
        static_cast<int>(std::floor(m_camera->Position.x / Chunk::WIDTH)),
        static_cast<int>(std::floor(m_camera->Position.z / Chunk::DEPTH))
    );

    // 如果中心区块变化，更新可见区块
    if (cameraChunk != m_currentCenterChunk) {
        m_currentCenterChunk = cameraChunk;
        updateVisibleChunks(m_camera->Position);
    }

    // 处理加载队列（每帧加载有限数量的区块）
    int loadsThisFrame = 0;
    while (!m_loadQueue.empty() && loadsThisFrame < MAX_LOADS_PER_FRAME) {
        glm::ivec2 chunkPos = m_loadQueue.front();
        m_loadQueue.pop();

        loadChunk(chunkPos);
        loadsThisFrame++;
    }

    // 合并渲染数据
    mergeRenderData();
}

Chunk* ChunkManager::getChunk(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    auto it = m_loadedChunks.find(key);
    if (it != m_loadedChunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

Chunk* ChunkManager::getChunk(int x, int z){
    glm::ivec2 a = { x, z };
    return getChunk(a);
}

Chunk* ChunkManager::getChunkAtWorld(const glm::vec3& worldPos) {
    glm::ivec2 chunkPos(
        static_cast<int>(std::floor(worldPos.x / Chunk::WIDTH)),
        static_cast<int>(std::floor(worldPos.z / Chunk::DEPTH))
    );
    return getChunk(chunkPos);
}

void ChunkManager::setRenderRadius(int radius) {
    if (radius > 0 && radius != m_renderRadius) {
        m_renderRadius = radius;
        updateVisibleChunks(glm::vec3(
            m_currentCenterChunk.x * Chunk::WIDTH + Chunk::WIDTH / 2.0f,
            Chunk::HEIGHT / 2.0f,
            m_currentCenterChunk.y * Chunk::DEPTH + Chunk::DEPTH / 2.0f
        ));
    }
}

int ChunkManager::getTotalInstances() const {
    int total = 0;
    for (const auto& pair : m_renderData) {
        total += pair.second.size();
    }
    return total;
}

void ChunkManager::printStats() const {
    std::cout << "=== Chunk Manager Stats ===" << std::endl;
    std::cout << "Render Radius: " << m_renderRadius << std::endl;
    std::cout << "Loaded Chunks: " << getLoadedChunkCount() << std::endl;
    std::cout << "Visible Chunks: " << getVisibleChunkCount() << std::endl;
    std::cout << "Total Instances: " << getTotalInstances() << std::endl;
    std::cout << "\nInstance Distribution:" << std::endl;
    std::cout << "===========================" << std::endl;
}

ChunkKey ChunkManager::chunkPosToKey(const glm::ivec2& pos) const {
    // 使用简单的哈希：将X和Z坐标打包到64位整数
    return ((static_cast<int64_t>(pos.x) & 0xFFFFFFFF) << 32) |
        (static_cast<int64_t>(pos.y) & 0xFFFFFFFF);
}

void ChunkManager::updateVisibleChunks(const glm::vec3& cameraPos) {
    // 清空可见区块列表
    m_visibleChunks.clear();

    // 计算需要加载的区块范围
    for (int dx = -m_renderRadius; dx <= m_renderRadius; ++dx) {
        for (int dz = -m_renderRadius; dz <= m_renderRadius; ++dz) {
            glm::ivec2 chunkPos(m_currentCenterChunk.x + dx,
                m_currentCenterChunk.y + dz);

            // 检查是否在渲染半径内
            if (shouldChunkBeVisible(chunkPos, m_currentCenterChunk)) {
                // 检查区块是否已加载
                Chunk* chunk = getChunk(chunkPos);

                if (chunk) {
                    // 区块已加载，标记为可见
                    chunk->setVisible(true);
                    m_visibleChunks.push_back(chunk);

                }
                else {
                    // 区块未加载，添加到加载队列
                    m_loadQueue.push(chunkPos);
                }
            }
        }
    }

    // 卸载不在可见范围内的区块
    std::vector<ChunkKey> toUnload;
    for (const auto& pair : m_loadedChunks) {
        Chunk* chunk = pair.second.get();
        glm::ivec2 chunkPos = chunk->getPosition();

        if (!shouldChunkBeVisible(chunkPos, m_currentCenterChunk)) {
            chunk->setVisible(false);
            // 这里可以添加延迟卸载逻辑
        }
    }
}

void ChunkManager::loadChunk(const glm::ivec2& chunkPos) {
    // 检查是否已加载
    if (getChunk(chunkPos) != nullptr) {
        return;
    }

    // 创建新区块
    auto chunk = std::make_unique<Chunk>(chunkPos,this);
    ChunkKey key = chunkPosToKey(chunkPos);

    // 加载区块
    chunk->load();

    m_visibleChunks.push_back(chunk.get());
    chunk->setVisible(true);

    // 添加到已加载区块列表
    m_loadedChunks[key] = std::move(chunk);        
    
    std::cout << "Loaded chunk at (" << chunkPos.x << ", " << chunkPos.y << ")" << std::endl;
}

void ChunkManager::unloadChunk(const glm::ivec2& chunkPos) {
    ChunkKey key = chunkPosToKey(chunkPos);
    auto it = m_loadedChunks.find(key);

    if (it != m_loadedChunks.end()) {
        std::cout << "Unloading chunk at (" << chunkPos.x << ", " << chunkPos.y << ")" << std::endl;
        m_loadedChunks.erase(it);
    }
}

void ChunkManager::mergeRenderData() {
    // 清空现有渲染数据
    for (auto& pair : m_renderData) {
        pair.second.clear();
    }

    // 合并所有可见区块的实例化数据
    for (Chunk* chunk : m_visibleChunks) {
        if (!chunk->isVisible()|| !chunk->is_can_render(m_camera)) continue;

        const auto& chunkData = chunk->getInstanceData();

        for (const auto& pair : chunkData) {
            BlockFaceType type = pair.first;
            const std::vector<glm::mat4>& matrices = pair.second;

            // 添加到全局渲染数据
            m_renderData[type].insert(m_renderData[type].end(),
                matrices.begin(), matrices.end());
        }
    }
}

bool ChunkManager::shouldChunkBeVisible(const glm::ivec2& chunkPos,
    const glm::ivec2& centerChunk) const {
    // 计算曼哈顿距离
    int dx = std::abs(chunkPos.x - centerChunk.x);
    int dz = std::abs(chunkPos.y - centerChunk.y);

    return dx <= m_renderRadius && dz <= m_renderRadius;
}

float ChunkManager::distanceToCamera(const glm::ivec2& chunkPos,
    const glm::vec3& cameraPos) const {
    // 计算区块中心的世界坐标
    glm::vec3 chunkCenter(
        chunkPos.x * Chunk::WIDTH + Chunk::WIDTH / 2.0f,
        Chunk::HEIGHT / 2.0f,
        chunkPos.y * Chunk::DEPTH + Chunk::DEPTH / 2.0f
    );

    return glm::distance(chunkCenter, cameraPos);
}