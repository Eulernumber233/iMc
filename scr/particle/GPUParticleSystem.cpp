#include "GPUParticleSystem.h"
#include <iostream>

GPUParticleSystem::GPUParticleSystem()
    : m_randomGen(12345)
{
    m_particleBuffer[0] = 0;
    m_particleBuffer[1] = 0;
    m_particleVAO = 0;
    m_atomicCounter = 0;
    m_quadVBO = 0;
}

GPUParticleSystem::~GPUParticleSystem() {
    if (m_particleBuffer[0]) glDeleteBuffers(2, m_particleBuffer);
    if (m_particleVAO) glDeleteVertexArrays(1, &m_particleVAO);
    if (m_atomicCounter) glDeleteBuffers(1, &m_atomicCounter);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
}

bool GPUParticleSystem::initialize() {
    if (m_initialized) return true;

    // 创建四边形顶点数据（用于渲染每个粒子）
    float quadVertices[] = {
        // 位置     // 纹理坐标
        -0.5f,  0.5f, 0.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 0.0f,

        -0.5f,  0.5f, 0.0f, 1.0f,
         0.5f, -0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f
    };

    glGenBuffers(1, &m_quadVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_initialized = true;
    return true;
}

bool GPUParticleSystem::createParticleBuffers() {
    // 创建双缓冲粒子缓冲区
    glGenBuffers(2, m_particleBuffer);

    int totalParticles = m_config.maxParticles > 0 ? m_config.maxParticles : ParticleConstants::MAX_GPU_PARTICLES;

    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffer[i]);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            totalParticles * sizeof(GPUParticleData),
            nullptr, GL_DYNAMIC_DRAW);
    }

    // 创建原子计数器
    glGenBuffers(1, &m_atomicCounter);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounter);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    GLuint zero = 0;
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    // --- 创建主 VAO，同时包含四边形顶点和粒子实例数据 ---
    glGenVertexArrays(1, &m_particleVAO);
    glBindVertexArray(m_particleVAO);

    // 1. 设置四边形顶点属性（非实例化）
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    // 位置 (location = 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    // 纹理坐标 (location = 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    // 顶点属性不需要 divisor（默认 0）

    // 2. 设置粒子实例属性（实例化）
    // 先临时绑定缓冲区 0，之后在渲染时会根据当前缓冲区重新设置指针
    glBindBuffer(GL_ARRAY_BUFFER, m_particleBuffer[0]);
    // 粒子位置 (location = 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GPUParticleData), (void*)0);
    glVertexAttribDivisor(2, 1);   // 实例化
    // 粒子速度/大小 (location = 3)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(GPUParticleData), (void*)offsetof(GPUParticleData, velocity));
    glVertexAttribDivisor(3, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

bool GPUParticleSystem::createWeatherEffect(const ParticleConfig& config) {
    if (!m_initialized && !initialize()) {
        return false;
    }

    m_config = config;
    m_randomGen.seed(config.randomSeed);

    // 如果最大粒子数变化，重新创建缓冲区
    if (m_config.maxParticles > 0) {
        if (m_particleBuffer[0]) glDeleteBuffers(2, m_particleBuffer);
        if (m_atomicCounter) glDeleteBuffers(1, &m_atomicCounter);
        if (!createParticleBuffers()) return false;
    }

    initializeParticles();
    m_weatherEffectActive = true;
    return true;
}

void GPUParticleSystem::initializeParticles() {
    int totalParticles = m_config.maxParticles > 0 ? m_config.maxParticles : ParticleConstants::MAX_GPU_PARTICLES;
    std::vector<GPUParticleData> particles(totalParticles);

    for (int i = 0; i < totalParticles; ++i) {
        GPUParticleData p;
        p.position = glm::vec4(
            RandomVec3(m_config.spawnAreaMin, m_config.spawnAreaMax, m_randomGen),
            RandomFloat(m_config.lifetimeMin, m_config.lifetimeMax, m_randomGen)
        );
        p.velocity = glm::vec4(
            RandomVec3(m_config.velocityMin, m_config.velocityMax, m_randomGen),
            RandomFloat(m_config.sizeEnd, m_config.sizeStart, m_randomGen)
        );
        particles[i] = p;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffer[m_currentBuffer]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, totalParticles * sizeof(GPUParticleData), particles.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    m_activeParticles = totalParticles;
}

void GPUParticleSystem::update(float deltaTime, const glm::vec3& cameraPosition) {
    if (!m_weatherEffectActive) return;

    m_timeAccumulator += deltaTime;

    m_computeShader.use();

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleBuffer[m_currentBuffer]);      // 输入
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_particleBuffer[1 - m_currentBuffer]);  // 输出
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 2, m_atomicCounter);

    // 设置 uniforms（省略，与原代码相同）
    m_computeShader.setFloat("deltaTime", deltaTime);
    m_computeShader.setFloat("totalTime", m_timeAccumulator);
    m_computeShader.setVec3("gravity", m_config.gravity);
    m_computeShader.setVec3("windForce", m_config.windForce);
    m_computeShader.setVec3("cameraPosition", cameraPosition);
    m_computeShader.setVec3("spawnAreaMin", m_config.spawnAreaMin);
    m_computeShader.setVec3("spawnAreaMax", m_config.spawnAreaMax);
    m_computeShader.setVec3("velocityMin", m_config.velocityMin);
    m_computeShader.setVec3("velocityMax", m_config.velocityMax);
    m_computeShader.setFloat("lifetimeMin", m_config.lifetimeMin);
    m_computeShader.setFloat("lifetimeMax", m_config.lifetimeMax);
    m_computeShader.setFloat("sizeMin", m_config.sizeStart);
    m_computeShader.setFloat("sizeMax", m_config.sizeEnd);
    m_computeShader.setFloat("damping", m_config.damping);

    GLuint zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    int totalParticles = m_config.maxParticles > 0 ? m_config.maxParticles : ParticleConstants::MAX_GPU_PARTICLES;
    glDispatchCompute((totalParticles + 255) / 256, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    // 交换缓冲区
    m_currentBuffer = 1 - m_currentBuffer;

    // 读取原子计数器
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounter);
    GLuint* counter = (GLuint*)glMapBuffer(GL_ATOMIC_COUNTER_BUFFER, GL_READ_ONLY);
    if (counter) {
        m_activeParticles = *counter;
        glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);
    }
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, 0);
}

void GPUParticleSystem::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!m_weatherEffectActive || m_activeParticles == 0) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    m_renderShader.use();
    m_renderShader.setMat4("view", view);
    m_renderShader.setMat4("projection", projection);
    m_renderShader.setVec4("colorStart", m_config.colorStart);
    m_renderShader.setVec4("colorEnd", m_config.colorEnd);
    m_renderShader.setFloat("sizeStart", m_config.sizeStart);
    m_renderShader.setFloat("sizeEnd", m_config.sizeEnd);
    m_renderShader.setFloat("maxLifetime", m_config.lifetimeMax);
    m_renderShader.setInt("useTexture", m_config.textureLayer >= 0 ? 1 : 0);
    m_renderShader.setInt("usePerParticleColor", 0);
    m_renderShader.setInt("usePerParticleSize", 0);

    if (m_config.textureLayer >= 0 && m_textureArray) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArray);
        m_renderShader.setInt("particleTextureArray", 0);
        m_renderShader.setInt("textureLayer", m_config.textureLayer);
    }

    // 绑定主 VAO（包含四边形顶点和实例属性）
    glBindVertexArray(m_particleVAO);

    // 更新实例属性指向当前粒子缓冲区（因为双缓冲切换了）
    glBindBuffer(GL_ARRAY_BUFFER, m_particleBuffer[m_currentBuffer]);

    // 重新设置实例属性指针（仅 location 2 和 3）
    // 注意：顶点属性 (location 0,1) 保持不变，它们始终指向 m_quadVBO
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GPUParticleData), (void*)0);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(GPUParticleData), (void*)offsetof(GPUParticleData, velocity));

    // 绘制实例（6 个顶点构成四边形，共 m_activeParticles 个实例）
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_activeParticles);

    // 清理
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE); // 恢复深度写入
}

void GPUParticleSystem::setGlobalForces(const glm::vec3& gravity, const glm::vec3& wind) {
    m_config.gravity = gravity;
    m_config.windForce = wind;
}

void GPUParticleSystem::reset() {
    m_timeAccumulator = 0.0f;
    m_currentBuffer = 0;
    m_activeParticles = 0;
    initializeParticles();
}