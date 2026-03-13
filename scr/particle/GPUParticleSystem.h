#pragma once
#include "../core.h"
#include "ParticleCommon.h"
#include "../Shader.h"
#include <vector>

class GPUParticleSystem {
public:
    GPUParticleSystem();
    ~GPUParticleSystem();

    // 初始化GPU粒子系统
    bool initialize();

    // 使用配置创建天气粒子系统（如雪、雨）
    bool createWeatherEffect(const ParticleConfig& config);

    // 更新粒子（调用计算着色器）
    void update(float deltaTime, const glm::vec3& cameraPosition);

    // 渲染粒子（实例化渲染）
    void render(const glm::mat4& view, const glm::mat4& projection);

    // 获取当前活跃粒子数
    int getActiveParticleCount() const { return m_activeParticles; }

    // 设置全局风力和重力
    void setGlobalForces(const glm::vec3& gravity, const glm::vec3& wind);

    // 重置粒子系统
    void reset();

private:

    // 创建粒子缓冲区
    bool createParticleBuffers();

    // 初始化粒子数据
    void initializeParticles();

    // 发射新粒子（CPU端，用于初始填充）
    //void emitParticles(int count);

private:
    // 粒子配置
    ParticleConfig m_config;

    // 着色器
    Shader m_computeShader{
        { GL_COMPUTE_SHADER, "shader/particle.comp" }
    };
    Shader m_renderShader{
            { GL_VERTEX_SHADER, "shader/particle.vert" },
            { GL_FRAGMENT_SHADER, "shader/particle.frag" }
    };

    // 粒子缓冲区（双缓冲）
    GLuint m_particleBuffer[2];
    GLuint m_particleVAO;
    int m_currentBuffer = 0; // 当前读取缓冲区索引
    int m_activeParticles = 0;

    // 原子计数器（用于统计活跃粒子）
    GLuint m_atomicCounter;

    // 渲染四边形VAO/VBO
    GLuint m_quadVAO;
    GLuint m_quadVBO;

    // 纹理数组（可选）
    GLuint m_textureArray = 0;

    // 随机数生成器
    std::mt19937 m_randomGen;

    // 时间累积
    float m_timeAccumulator = 0.0f;

    // 标志
    bool m_initialized = false;
    bool m_weatherEffectActive = false;
};