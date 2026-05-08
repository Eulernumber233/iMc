#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
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
    const std::vector<Chunk*>& getActiveChunks() const { return m_activeChunks; }
    std::vector<glm::ivec2> getActiveChunkPositions() const;
    void setRenderRadius(int radius);

    int getLoadedChunkCount() const { return (int)m_loadedChunks.size(); }
    int getActiveChunkCount() const { return (int)m_activeChunks.size(); }
    int getInFlightCount() const { return (int)m_inFlight.size(); }
    void printStats() const;

    std::shared_ptr<TerrainGenerator> getTerrainGenerator() { return m_generator; }

    bool setBlock(const glm::ivec3& worldPos, BlockType type);
    BlockType getBlockAt(const glm::ivec3& worldPos);

    static SectionKey makeSectionKey(int chunkX, int chunkZ, int sectionY);

private:
    std::shared_ptr<TerrainGenerator> m_generator;

    std::shared_ptr<Camera> m_camera;
    // 已加载的全部 chunk（含磁盘读回的、worker 产出的；目前不主动驱逐）
    std::unordered_map<ChunkKey, std::unique_ptr<Chunk>> m_loadedChunks;
    // "活跃" chunk：玩家附近、需要逻辑更新与渲染候选的 chunk。
    // 与渲染层 frustum/距离剔除无关，那部分由 Chunk::isSectionVisible 决定。
    std::vector<Chunk*> m_activeChunks;

    ChunkWorkerPool m_workerPool;
    std::unordered_set<ChunkKey> m_inFlight;

    ChunkArena m_arena;
    std::unordered_map<SectionKey, ChunkArena::Slot> m_sectionSlots;

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

    // 从 RuntimeConfig 读，运行时可调
    int m_maxUploadsPerFrame = 8;
    int m_maxInflightRequests = 64;

    ChunkKey chunkPosToKey(const glm::ivec2& pos) const;

    // 重新计算 m_activeChunks（玩家附近半径内的 chunk）
    void updateActiveChunks(const glm::vec3& cameraPos);

    void requestChunkLoad(const glm::ivec2& chunkPos);
    // 扫一遍半径内还没加载且不在 in-flight 队列里的 chunk，从近到远补投递
    void requestMissingChunks();
    void integrateBuiltChunks();

    void rebuildDrawCommands();
    void uploadSection(int chunkX, int chunkZ, int sectionY, Section& section, int& uploadBudget);
    void releaseSectionSlot(SectionKey key);
    void syncIndirectBuffer();
    void syncSectionBaseSSBO();

    bool isWithinActiveRadius(const glm::ivec2& chunkPos,
        const glm::ivec2& centerChunk) const;
    bool isWithinEvictRadius(const glm::ivec2& chunkPos,
        const glm::ivec2& centerChunk) const;

    // 释放走出 evict 半径的 chunk 的所有 section GPU slot；CPU 数据保留。
    void evictFarChunkSlots();

    void linkNeighbors(Chunk* newChunk);
};
