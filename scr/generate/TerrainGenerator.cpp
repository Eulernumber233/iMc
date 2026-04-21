// [file name]: TerrainGenerator.cpp
#include "TerrainGenerator.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>

struct TerrainGenerator::Impl {
    unsigned int seed = 0;

    float generateTerrainNoise(int worldX, int worldZ) const {
        const float fx = static_cast<float>(worldX);
        const float fz = static_cast<float>(worldZ);

        float baseFbm = Noise::fractalBrownianMotion2D(
            fx * 0.012f, fz * 0.012f,
            5, 0.55f, 2.1f, seed);
        float base = baseFbm * 2.0f - 1.0f;               // [-1, 1]

        float ridgeRaw = Noise::perlin2D(fx * 0.018f, fz * 0.018f, seed + 2027);
        float ridge = 1.0f - std::fabs(ridgeRaw);         // [0, 1]
        ridge = ridge * ridge;
        ridge = ridge * 2.0f - 1.0f;                      // [-1, 1]

        float detailFbm = Noise::fractalBrownianMotion2D(
            fx * 0.06f, fz * 0.06f,
            3, 0.5f, 2.0f, seed + 3041);
        float detail = detailFbm * 2.0f - 1.0f;           // [-1, 1]

        float signed_h = base * 0.55f + ridge * 0.35f + detail * 0.10f;
        signed_h = std::clamp(signed_h, -1.0f, 1.0f);

        const float minY = 30.0f;
        const float maxY = 60.0f;
        float worldY = 0.5f * (minY + maxY) + 0.5f * (maxY - minY) * signed_h;

        float norm = worldY / static_cast<float>(Chunk::HEIGHT);
        return std::clamp(norm, 0.0f, 1.0f);
    }
};

TerrainGenerator::TerrainGenerator()
    : m_impl(std::make_unique<Impl>()) {
    m_impl->seed = m_params.seed;
}

TerrainGenerator::~TerrainGenerator() = default;

void TerrainGenerator::setSeed(unsigned int seed) {
    m_params.seed = seed;
    m_impl->seed = seed;
}

void TerrainGenerator::fillChunk(Chunk* chunk, const glm::ivec2& chunkPos) {
    if (!chunk) return;

    int startX = chunkPos.x * Chunk::WIDTH;
    int startZ = chunkPos.y * Chunk::DEPTH;

    for (int localZ = 0; localZ < Chunk::DEPTH; ++localZ) {
        for (int localX = 0; localX < Chunk::WIDTH; ++localX) {
            int worldX = startX + localX;
            int worldZ = startZ + localZ;

            float heightValue = m_impl->generateTerrainNoise(worldX, worldZ);
            int groundHeight = std::clamp(
                static_cast<int>(heightValue * Chunk::HEIGHT),
                0, Chunk::HEIGHT - 1);

            for (int y = 0; y < Chunk::HEIGHT; ++y) {
                BlockType block = BLOCK_AIR;

                if (y <= groundHeight) {
                    if (y < 45) {
                        block = BLOCK_STONE;
                    }
                    else if (y == groundHeight) {
                        block = (y >= 50) ? BLOCK_GRASS : BLOCK_DIRT;
                    }
                    else {
                        block = BLOCK_DIRT;
                    }
                }

                chunk->setBlock(localX, y, localZ, block);
            }
        }
    }

    std::cout << "Generated terrain for chunk (" << chunkPos.x << ", "
        << chunkPos.y << ")" << std::endl;
}

float TerrainGenerator::getHeightAt(int worldX, int worldZ) const {
    float heightValue = m_impl->generateTerrainNoise(worldX, worldZ);
    return heightValue * Chunk::HEIGHT;
}

BlockType TerrainGenerator::getBlockAt(int worldX, int worldY, int worldZ) const {
    float heightValue = m_impl->generateTerrainNoise(worldX, worldZ);
    int groundHeight = std::clamp(
        static_cast<int>(heightValue * Chunk::HEIGHT),
        0, Chunk::HEIGHT - 1);

    if (worldY > groundHeight) {
        return BLOCK_AIR;
    }
    if (worldY < 45) {
        return BLOCK_STONE;
    }
    if (worldY == groundHeight) {
        return (worldY >= 50) ? BLOCK_GRASS : BLOCK_DIRT;
    }
    return BLOCK_DIRT;
}
