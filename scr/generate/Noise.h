// [file name]: Noise.h
#pragma once
#include <cstdint>

namespace Noise {
    // 2D Perlin噪声
    float perlin2D(float x, float y, uint64_t seed = 0);

    // 3D Perlin噪声
    float perlin3D(float x, float y, float z, uint64_t seed = 0);

    // 分形布朗运动（多个八度叠加）
    float fractalBrownianMotion2D(float x, float y, int octaves = 4,
        float persistence = 0.5f, float lacunarity = 2.0f,
        uint64_t seed = 0);

    // Simplex噪声（更快）
    float simplex2D(float x, float y, uint64_t seed = 0);

    // 值噪声
    float valueNoise2D(float x, float y, uint64_t seed = 0);
}