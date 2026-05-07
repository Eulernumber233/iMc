// [file name]: TerrainGenerator.h
#pragma once
#include "../chunk/Chunk.h"
#include "../chunk/BlockType.h"
#include "Noise.h"
#include <glm/glm.hpp>
#include <functional>
#include <memory>
#include <vector>

// 地形生成参数
struct TerrainParams {
    float seaLevel = 0.3f;         // 海平面 (0-1)
    float mountainLevel = 0.7f;    // 山地起始高度
	float grassLevel = 0.7f;       // 草地层深度
    float dirtDepth = 0.65f;        // 泥土层深度
    float stoneDepth = 0.60f;      // 石头层深度
    unsigned int seed = 11242342;     // 随机种子
};

// 连续地形生成器
class TerrainGenerator {
public:
    TerrainGenerator();
    ~TerrainGenerator();

    // 设置种子
    void setSeed(unsigned int seed);

    // 线程安全：直接把方块写到外部 buffer。
    // dst 大小必须为 ChunkConstants::CHUNK_VOLUME，
    // 索引同 Chunk 内部布局：(y * DEPTH + z) * WIDTH + x
    // 该接口只读 noise + seed，不依赖 generator 实例的可变状态，可在 worker 线程并发调用。
    void fillChunkBuffer(BlockType* dst, const glm::ivec2& chunkPos) const;

    // 获取世界位置的高度（用于区块间连续）
    float getHeightAt(int worldX, int worldZ) const;

    // 获取世界位置的方块
    BlockType getBlockAt(int worldX, int worldY, int worldZ) const;

    // 获取参数
    TerrainParams& getParams() { return m_params; }
    const TerrainParams& getParams() const { return m_params; }

private:
    // 私有实现
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    TerrainParams m_params;
};