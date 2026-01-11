// [file name]: Noise.cpp
#include "Noise.h"
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>
#include <random>
#include <functional>

// 快速哈希函数
static inline unsigned int hash(unsigned int x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

float Noise::perlin2D(float x, float y, unsigned int seed) {
    // 使用GLM的Perlin噪声，加入种子偏移
    return glm::perlin(glm::vec2(x + seed * 0.01f, y + seed * 0.01f));
}

float Noise::perlin3D(float x, float y, float z, unsigned int seed) {
    // GLM没有3D Perlin，这里实现简化版
    float xy = perlin2D(x, y, seed);
    float yz = perlin2D(y, z, seed);
    float xz = perlin2D(x, z, seed);

    return (xy + yz + xz) / 3.0f;
}

float Noise::fractalBrownianMotion2D(float x, float y, int octaves,
    float persistence, float lacunarity,
    unsigned int seed) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        // 获取Perlin噪声（在[-1,1]范围）
        float noise = perlin2D(x * frequency, y * frequency, seed + i);

        // 将噪声映射到[0,1]范围
        noise = (noise + 1.0f) * 0.5f;

        value += noise * amplitude;
        maxValue += amplitude;

        amplitude *= persistence;
        frequency *= lacunarity;
    }

    return value / maxValue;
}

float Noise::simplex2D(float x, float y, unsigned int seed) {
    // GLM没有Simplex噪声，这里使用Perlin噪声替代
    return perlin2D(x, y, seed);
}

float Noise::valueNoise2D(float x, float y, unsigned int seed) {
    // 简单值噪声实现
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));

    float tx = x - xi;
    float ty = y - yi;

    // 使用哈希获取四个角点的随机值
    float v00 = static_cast<float>(hash(xi + hash(yi + seed))) / UINT_MAX;
    float v10 = static_cast<float>(hash(xi + 1 + hash(yi + seed))) / UINT_MAX;
    float v01 = static_cast<float>(hash(xi + hash(yi + 1 + seed))) / UINT_MAX;
    float v11 = static_cast<float>(hash(xi + 1 + hash(yi + 1 + seed))) / UINT_MAX;

    // 双线性插值
    float sx = tx * tx * (3 - 2 * tx);
    float sy = ty * ty * (3 - 2 * ty);

    float a = v00 + sx * (v10 - v00);
    float b = v01 + sx * (v11 - v01);

    return a + sy * (b - a);
}