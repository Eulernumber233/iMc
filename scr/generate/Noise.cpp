// [file name]: Noise.cpp
#include "Noise.h"
#include <glm/glm.hpp>
#include <glm/gtc/noise.hpp>

// 将 64-bit 种子展开为坐标偏移，使不同种子采样噪声场中完全不同的区域
static inline float seedToOffset(uint64_t seed, int slot) {
    uint64_t h = seed;
    h ^= (uint64_t)(slot) * 0x9E3779B97F4A7C15ULL;
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    h = h ^ (h >> 31);
    return (float)(int32_t)(h & 0x7FFFFFFF) * 0.000137f;
}

float Noise::perlin2D(float x, float y, uint64_t seed) {
    float ox = seedToOffset(seed, 0);
    float oy = seedToOffset(seed, 1);
    return glm::perlin(glm::vec2(x + ox, y + oy));
}

float Noise::perlin3D(float x, float y, float z, uint64_t seed) {
    float xy = perlin2D(x, y, seed);
    float yz = perlin2D(y, z, seed + 0x9E3779B9);
    float xz = perlin2D(x, z, seed + 0x7F4A7C15);
    return (xy + yz + xz) / 3.0f;
}

float Noise::fractalBrownianMotion2D(float x, float y, int octaves,
    float persistence, float lacunarity,
    uint64_t seed) {
    float value = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; ++i) {
        float ox = seedToOffset(seed, i * 2);
        float oy = seedToOffset(seed, i * 2 + 1);
        float noise = glm::perlin(glm::vec2(x * frequency + ox, y * frequency + oy));
        noise = (noise + 1.0f) * 0.5f;
        value += noise * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= lacunarity;
    }
    return value / maxValue;
}

float Noise::simplex2D(float x, float y, uint64_t seed) {
    return perlin2D(x, y, seed);
}

float Noise::valueNoise2D(float x, float y, uint64_t seed) {
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    float tx = x - xi;
    float ty = y - yi;

    auto h = [seed](int a, int b) -> uint64_t {
        uint64_t v = seed;
        v ^= (uint64_t)(a) * 0x9E3779B97F4A7C15ULL;
        v ^= (uint64_t)(b) * 0xBF58476D1CE4E5B9ULL;
        v = (v ^ (v >> 30)) * 0x94D049BB133111EBULL;
        return v ^ (v >> 31);
    };

    float v00 = (float)(h(xi,     yi)     & 0xFFFF) / 65535.0f;
    float v10 = (float)(h(xi + 1, yi)     & 0xFFFF) / 65535.0f;
    float v01 = (float)(h(xi,     yi + 1) & 0xFFFF) / 65535.0f;
    float v11 = (float)(h(xi + 1, yi + 1) & 0xFFFF) / 65535.0f;

    float sx = tx * tx * (3 - 2 * tx);
    float sy = ty * ty * (3 - 2 * ty);
    float a = v00 + sx * (v10 - v00);
    float b = v01 + sx * (v11 - v01);
    return a + sy * (b - a);
}
