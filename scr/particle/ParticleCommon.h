#pragma once
#include "../core.h"
#include <glm/glm.hpp>
#include <random>

namespace ParticleConstants {
    constexpr int MAX_GPU_PARTICLES = 100000;  // 最大GPU粒子数量
    constexpr int MAX_ECS_PARTICLES = 1000;    // 最大ECS粒子数量
    constexpr float PARTICLE_QUAD_SIZE = 0.1f; // 粒子四边形默认大小
}

// 粒子类型枚举
enum class ParticleType {
    WeatherSnow,
    WeatherRain,
    BlockDebris,
    Dust,  // 尘埃
    Custom // 自定义
};

// 粒子行为标志
enum ParticleBehaviorFlags {
    NONE = 0,
    GRAVITY = 1 << 0,
    WIND = 1 << 1,
    COLLISION = 1 << 2,
    FADE = 1 << 3,
    ROTATE = 1 << 4
};

// 粒子配置结构，用于参数化创建粒子效果
struct ParticleConfig {
    ParticleType type = ParticleType::Custom;
    uint32_t behaviorFlags = ParticleBehaviorFlags::GRAVITY;

    // 生成区域
    glm::vec3 spawnAreaMin = glm::vec3(-10.0f, 10.0f, -10.0f);
    glm::vec3 spawnAreaMax = glm::vec3(10.0f, 20.0f, 10.0f);

    // 速度范围
    glm::vec3 velocityMin = glm::vec3(-0.5f, -2.0f, -0.5f);
    glm::vec3 velocityMax = glm::vec3(0.5f, -1.0f, 0.5f);

    // 外观
    glm::vec4 colorStart = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
    float sizeStart = 0.1f;
    float sizeEnd = 0.05f;
    float lifetimeMin = 5.0f;
    float lifetimeMax = 10.0f;

    // 纹理
    int textureLayer = -1;  // -1表示不使用纹理，使用纯色

    // 生成率（每秒生成粒子数）
    float spawnRate = 100.0f;
    int maxParticles = 10000;

    // 物理参数
    glm::vec3 gravity = glm::vec3(0.0f, -9.8f, 0.0f);
    glm::vec3 windForce = glm::vec3(0.0f);
    float damping = 0.99f;

    // 随机种子
    unsigned int randomSeed = 12345;
};

// 扩展粒子数据结构，增加 homeChunk
struct GPUParticleData {
    glm::vec4 position;   // w: lifetime
    glm::vec4 velocity;   // w: size
    glm::ivec2 homeChunk; // 所属区块坐标
    // 保证结构体大小为 48 字节（与着色器对齐）
    float _padding[2];
};

// ECS粒子组件
struct ParticleComponent {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec4 color;
    float size;
    float lifetime;
    float maxLifetime;
    uint32_t behaviorFlags;
    glm::vec3 gravity; // 重力加速度

    // 构造函数
    ParticleComponent(
        const glm::vec3& pos = glm::vec3(0.0f),
        const glm::vec3& vel = glm::vec3(0.0f),
        const glm::vec4& col = glm::vec4(1.0f),
        float sz = 0.1f,
        float life = 5.0f,
        uint32_t flags = ParticleBehaviorFlags::GRAVITY,
        const glm::vec3& grav = glm::vec3(0.0f, -9.8f, 0.0f)
    ) : position(pos), velocity(vel), color(col), size(sz),
        lifetime(life), maxLifetime(life), behaviorFlags(flags), gravity(grav) {}
};

// 粒子渲染实例数据（用于实例化渲染）
struct ParticleInstanceData {
    glm::vec3 position;
    float size;
    glm::vec4 color;
    int textureLayer;

    ParticleInstanceData(
        const glm::vec3& pos = glm::vec3(0.0f),
        float sz = 0.1f,
        const glm::vec4& col = glm::vec4(1.0f),
        int texLayer = -1
    ) : position(pos), size(sz), color(col), textureLayer(texLayer) {}
};

// 实用函数：在范围内生成随机浮点数
inline float RandomFloat(float min, float max, std::mt19937& gen) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(gen);
}

// 实用函数：在范围内生成随机向量
inline glm::vec3 RandomVec3(const glm::vec3& min, const glm::vec3& max, std::mt19937& gen) {
    return glm::vec3(
        RandomFloat(min.x, max.x, gen),
        RandomFloat(min.y, max.y, gen),
        RandomFloat(min.z, max.z, gen)
    );
}