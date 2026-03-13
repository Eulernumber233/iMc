#pragma once
#include "../core.h"
#include "ParticleCommon.h"
#include "../Shader.h"
#include "../entt/entt.hpp"
#include <vector>

// ECS实例数据，匹配着色器布局
struct ECSInstanceData {
    glm::vec4 position; // xyz: position, w: lifetime
    glm::vec4 velocity; // xyz: velocity, w: size
    glm::vec4 color;    // rgba color
};

class ECSParticleSystem {
public:
    ECSParticleSystem();
    ~ECSParticleSystem();

    // 初始化ECS粒子系统
    bool initialize();

    // 发射一批粒子（例如方块破坏）
    void emitBurst(const ParticleConfig& config, const glm::vec3& position, int count);

    // 更新粒子（CPU物理）
    void update(float deltaTime);

    // 渲染粒子（实例化渲染）
    void render(const glm::mat4& view, const glm::mat4& projection);

    // 获取活跃粒子数
    int getActiveParticleCount() const { return m_activeParticles; }

    // 清理所有粒子
    void clear();

private:
    // 创建渲染数据
    bool createRenderResources();

    // 收集实例数据
    void collectInstanceData();

    // 应用物理到单个粒子
    void applyPhysics(ParticleComponent& particle, float deltaTime);

private:
    // ECS registry
    entt::registry m_registry;
    std::vector<entt::entity> m_particleEntities;

    // 实例渲染数据
    std::vector<ECSInstanceData> m_instanceData;

    // 渲染资源
    Shader m_particleShader{
        { GL_VERTEX_SHADER, "shader/particle.vert" },
        { GL_FRAGMENT_SHADER, "shader/particle.frag" }
    };
    GLuint m_particleVAO;
    GLuint m_particleVBO;
    GLuint m_instanceVBO;
    GLuint m_textureArray = 0;

    // 统计数据
    int m_activeParticles = 0;

    // 标志
    bool m_initialized = false;
};