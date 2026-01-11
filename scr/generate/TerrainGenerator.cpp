// [file name]: TerrainGenerator.cpp
#include "TerrainGenerator.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>
#include <iostream>

// 私有实现类
struct TerrainGenerator::Impl {
    unsigned int seed = 131232145;
    std::vector<float> noiseCache;
    int cacheWidth = 0;
    int cacheHeight = 0;
    glm::ivec2 cacheOrigin = glm::ivec2(0);

    // 为某个区域预计算噪声
    void buildCache(const glm::ivec2& origin, int width, int height) {
        cacheOrigin = origin;
        cacheWidth = width;
        cacheHeight = height;
        noiseCache.resize(width * height);

        for (int z = 0; z < height; ++z) {
            for (int x = 0; x < width; ++x) {
                int worldX = origin.x + x;
                int worldZ = origin.y + z;

                // 计算多层噪声
                float heightValue = generateTerrainNoise(worldX, worldZ);
                noiseCache[z * width + x] = heightValue;
            }
        }
    }

    float getCachedNoise(int worldX, int worldZ) const {
        // 计算在缓存中的位置
        int localX = worldX - cacheOrigin.x;
        int localZ = worldZ - cacheOrigin.y;

        if (localX >= 0 && localX < cacheWidth &&
            localZ >= 0 && localZ < cacheHeight) {
            return noiseCache[localZ * cacheWidth + localX];
        }

        // 如果不在缓存中，实时计算（这种情况应该很少）
        return generateTerrainNoise(worldX, worldZ);
    }

private:
    float generateTerrainNoise(int worldX, int worldZ) const {
        // 计算多层噪声（使用Noise命名空间中的函数）
        float baseNoise = Noise::fractalBrownianMotion2D(
            worldX * 0.01f, worldZ * 0.01f,
            5, 0.5f, 2.0f, seed);

        float mountainNoise = Noise::fractalBrownianMotion2D(
            worldX * 0.005f, worldZ * 0.005f,
            3, 0.5f, 2.0f, seed + 123);

        float detailNoise = Noise::fractalBrownianMotion2D(
            worldX * 0.02f, worldZ * 0.02f,
            4, 0.6f, 2.2f, seed + 456);

        // 组合噪声
        float height = baseNoise * 0.6f;
        height += mountainNoise * 0.3f;
        height += detailNoise * 0.1f;

        // 应用平滑（使用平方根让地形更平缓）
        height = std::sqrt(height);

        // 确保在[0,1]范围内
        return std::clamp(height, 0.0f, 1.0f);
    }
};

TerrainGenerator::TerrainGenerator()
    : m_impl(std::make_unique<Impl>()) {
}

TerrainGenerator::~TerrainGenerator() = default;

void TerrainGenerator::setSeed(unsigned int seed) {
    m_params.seed = seed;
    m_impl->seed = seed;
    m_impl->noiseCache.clear(); // 清除缓存
}

void TerrainGenerator::fillChunk(Chunk* chunk, const glm::ivec2& chunkPos) {
    if (!chunk) return;

    // 计算区块在世界中的范围
    int startX = chunkPos.x * Chunk::WIDTH;
    int startZ = chunkPos.y * Chunk::DEPTH;

    // 为这个区域预计算噪声（包含边界）
    int margin = 2; // 边界扩展，确保连续性
    m_impl->buildCache(
        glm::ivec2(startX - margin, startZ - margin),
        Chunk::WIDTH + margin * 2,
        Chunk::DEPTH + margin * 2
    );

    // 遍历区块内的所有位置
    for (int localZ = 0; localZ < Chunk::DEPTH; ++localZ) {
        for (int localX = 0; localX < Chunk::WIDTH; ++localX) {
            // 计算世界坐标
            int worldX = startX + localX;
            int worldZ = startZ + localZ;

            // 获取高度（使用缓存）
            float heightValue = m_impl->getCachedNoise(worldX, worldZ);

            //// 将高度值映射到实际方块高度
            //// 0-1 映射到 0-HEIGHT，并考虑海平面
            //float minHeight = m_params.seaLevel * Chunk::HEIGHT;
            //float maxHeight = m_params.mountainLevel * Chunk::HEIGHT;
            //int groundHeight = static_cast<int>(
            //    minHeight + heightValue * (maxHeight - minHeight)
            //    );
            //groundHeight = std::clamp(groundHeight, 0, Chunk::HEIGHT - 1);

            int groundHeight = std::clamp((int)(heightValue * Chunk::HEIGHT), 0, Chunk::HEIGHT - 1);
            // 为这一列填充方块
            for (int y = 0; y < Chunk::HEIGHT; ++y) {
                BlockType block = BLOCK_AIR;

                if (y <= groundHeight) {
                    // 地面以下
                    if (y >= m_params.dirtDepth* Chunk::HEIGHT) {
                        // 表层泥土
                        block = BLOCK_DIRT;
                    }
                    else if (y >= m_params.stoneDepth* Chunk::HEIGHT) {
                        // 中层石头
                        block = BLOCK_STONE;
                    }
                    else {
                        // 深层石头
                        block = BLOCK_STONE;
                    }
                }

                if (y == groundHeight && heightValue > m_params.grassLevel) {
                    // 高海拔处可能直接是石头
                    block = BLOCK_GRASS;
                }

                //// 添加一些山石裸露效果
                //if (heightValue > 0.90f) {
                //    // 高海拔处可能直接是石头
                //    block = BLOCK_STONE;
                //}

                // 设置方块
                chunk->setBlock(localX, y, localZ, block);
            }
            chunk->setBlock(5, 63, 5, BLOCK_WOOD);
            chunk->setBlock(6, 63, 5, BLOCK_WOOD);
            chunk->setBlock(7, 63, 5, BLOCK_WOOD);
            chunk->setBlock(8, 63, 5, BLOCK_WOOD);
            chunk->setBlock(9, 63, 5, BLOCK_WOOD);
            chunk->setBlock(9, 63, 6, BLOCK_WOOD);
            chunk->setBlock(9, 63, 7, BLOCK_WOOD);
            chunk->setBlock(9, 63, 4, BLOCK_WOOD);

            chunk->setBlock(0, 63, 0, BLOCK_STONE);
            chunk->setBlock(1, 63, 1, BLOCK_STONE);
            chunk->setBlock(3, 63, 0, BLOCK_STONE);
        }
    }

    // 调试信息
    std::cout << "Generated terrain for chunk (" << chunkPos.x << ", "
        << chunkPos.y << ")" << std::endl;
}

float TerrainGenerator::getHeightAt(int worldX, int worldZ) const {
    // 获取噪声值（使用缓存或实时计算）
    float heightValue = m_impl->getCachedNoise(worldX, worldZ);

    // 映射到实际高度
    float minHeight = m_params.seaLevel * Chunk::HEIGHT;
    float maxHeight = m_params.mountainLevel * Chunk::HEIGHT;

    return minHeight + heightValue * (maxHeight - minHeight);
}

BlockType TerrainGenerator::getBlockAt(int worldX, int worldY, int worldZ) const {
    // 获取高度
    float heightValue = m_impl->getCachedNoise(worldX, worldZ);
    float minHeight = m_params.seaLevel * Chunk::HEIGHT;
    float maxHeight = m_params.mountainLevel * Chunk::HEIGHT;
    int groundHeight = static_cast<int>(
        minHeight + heightValue * (maxHeight - minHeight)
        );

    // 确保高度在合理范围内
    groundHeight = std::clamp(groundHeight, 0, Chunk::HEIGHT - 1);

    // 根据高度判断方块类型
    if (worldY > groundHeight) {
        return BLOCK_AIR;
    }
    else if (worldY >= groundHeight - m_params.dirtDepth) {
        return BLOCK_DIRT;
    }
    else {
        return BLOCK_STONE;
    }
}