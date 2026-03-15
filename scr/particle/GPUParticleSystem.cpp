#include "GPUParticleSystem.h"
#include <iostream>
#include <array>

GPUParticleSystem::GPUParticleSystem()
    : m_randomGen(12345)
{
    m_particleBuffer[0] = 0;
    m_particleBuffer[1] = 0;
    m_particleVAO = 0;
    m_atomicCounter = 0;
    m_quadVBO = 0;
    m_camera = nullptr;
    m_chunksSSBO = 0;
}

GPUParticleSystem::~GPUParticleSystem() {
    if (m_particleBuffer[0]) glDeleteBuffers(2, m_particleBuffer);
    if (m_particleVAO) glDeleteVertexArrays(1, &m_particleVAO);
    if (m_atomicCounter) glDeleteBuffers(1, &m_atomicCounter);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
    if (m_chunksSSBO) glDeleteBuffers(1, &m_chunksSSBO);
}

bool GPUParticleSystem::initialize() {
    if (m_initialized) return true;

    float quadVertices[] = {
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
    glGenBuffers(2, m_particleBuffer);
    int totalParticles = m_config.maxParticles > 0 ? m_config.maxParticles : ParticleConstants::MAX_GPU_PARTICLES;

    for (int i = 0; i < 2; ++i) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffer[i]);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            totalParticles * sizeof(GPUParticleData),
            nullptr, GL_DYNAMIC_DRAW);
    }

    glGenBuffers(1, &m_atomicCounter);
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounter);
    glBufferData(GL_ATOMIC_COUNTER_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_DRAW);
    GLuint zero = 0;
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    glGenVertexArrays(1, &m_particleVAO);
    glBindVertexArray(m_particleVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, m_particleBuffer[0]);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GPUParticleData), (void*)0);
    glVertexAttribDivisor(2, 1);
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
        p.position = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f); // 死亡
        p.velocity = glm::vec4(0.0f);
        p.homeChunk = glm::ivec2(-1, -1); // 无效
        particles[i] = p;
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_particleBuffer[m_currentBuffer]);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, totalParticles * sizeof(GPUParticleData), particles.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    m_activeParticles = 0;
}

void GPUParticleSystem::update(float deltaTime, const glm::vec3& cameraPosition) {
    if (!m_weatherEffectActive) return;

    m_timeAccumulator += deltaTime;

    // --- 更新视锥平面（每帧）---
    if (m_frustumCullingEnabled && m_camera) {
        m_frustumPlanes = m_camera->GetFrustumPlanes();
    }

    // --- 计算并上传配额 ---
    if (m_hasVisibleChunksInfo && !m_visibleChunkPositions.empty()) {
        int chunkCount = static_cast<int>(m_visibleChunkPositions.size());
        int totalParticles = m_config.maxParticles;

        // 计算每个区块的配额（这里使用均匀分配，可根据需要改为距离权重）
        std::vector<GLuint> quotas(chunkCount);
        int baseQuota = totalParticles / chunkCount;
        int remainder = totalParticles % chunkCount;
        for (int i = 0; i < chunkCount; ++i) {
            quotas[i] = baseQuota + (i < remainder ? 1 : 0);
        }

        // 计算累计配额（前缀和）
        m_chunkCumulativeQuota.resize(chunkCount);
        GLuint sum = 0;
        for (int i = 0; i < chunkCount; ++i) {
            sum += quotas[i];
            m_chunkCumulativeQuota[i] = sum;
        }

        // 上传累计配额到 SSBO (binding = 4)
        if (m_chunkQuotaSSBO == 0) {
            glGenBuffers(1, &m_chunkQuotaSSBO);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_chunkQuotaSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            m_chunkCumulativeQuota.size() * sizeof(GLuint),
            m_chunkCumulativeQuota.data(),
            GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_chunkQuotaSSBO);

        // 上传可见区块坐标到 SSBO (binding = 3)
        if (m_chunksSSBO == 0) {
            glGenBuffers(1, &m_chunksSSBO);
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_chunksSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
            m_visibleChunkPositions.size() * sizeof(glm::ivec2),
            m_visibleChunkPositions.data(),
            GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_chunksSSBO);
        m_chunksCount = static_cast<int>(m_visibleChunkPositions.size());
    }
    else {
        m_chunksCount = 0;
    }

    m_computeShader.use();

    // 绑定粒子缓冲区（双缓冲）
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_particleBuffer[m_currentBuffer]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_particleBuffer[1 - m_currentBuffer]);
    glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, 2, m_atomicCounter);

    // 设置 uniforms
    m_computeShader.setFloat("deltaTime", deltaTime);
    m_computeShader.setFloat("totalTime", m_timeAccumulator);
    m_computeShader.setVec3("gravity", m_config.gravity);
    m_computeShader.setVec3("windForce", m_config.windForce);
    m_computeShader.setVec3("velocityMin", m_config.velocityMin);
    m_computeShader.setVec3("velocityMax", m_config.velocityMax);
    m_computeShader.setFloat("lifetimeMin", m_config.lifetimeMin);
    m_computeShader.setFloat("lifetimeMax", m_config.lifetimeMax);
    m_computeShader.setFloat("sizeMin", m_config.sizeStart);
    m_computeShader.setFloat("sizeMax", m_config.sizeEnd);
    m_computeShader.setFloat("damping", m_config.damping);
    m_computeShader.setInt("chunksCount", m_chunksCount);
    m_computeShader.setBool("enableFrustumCulling", m_frustumCullingEnabled);
    m_computeShader.setFloat("rainHeightMin", m_rainHeightMin);
    m_computeShader.setFloat("rainHeightMax", m_rainHeightMax);
    m_computeShader.setFloat("frustumExpandDistance", 50.0f); // 可配置

    if (m_frustumCullingEnabled && m_camera) {
        for (int i = 0; i < 6; ++i) {
            std::string planeName = "frustumPlanes[" + std::to_string(i) + "]";
            m_computeShader.setVec4(planeName, m_frustumPlanes[i]);
        }
    }

    // 重置原子计数器
    GLuint zero = 0;
    glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, m_atomicCounter);
    glBufferSubData(GL_ATOMIC_COUNTER_BUFFER, 0, sizeof(GLuint), &zero);

    int totalParticles = m_config.maxParticles;
    glDispatchCompute((totalParticles + 255) / 256, 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_ATOMIC_COUNTER_BARRIER_BIT);

    m_currentBuffer = 1 - m_currentBuffer;

    // 读取活跃粒子数
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
    //glDisable(GL_CULL_FACE);
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

    glBindVertexArray(m_particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleBuffer[m_currentBuffer]);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GPUParticleData), (void*)0);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(GPUParticleData), (void*)offsetof(GPUParticleData, velocity));

    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, m_activeParticles);

    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
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

void GPUParticleSystem::setFrustumCullingParams(const std::shared_ptr<Camera>& camera,
    float rainHeightMin, float rainHeightMax) {
    m_camera = camera;
    m_rainHeightMin = rainHeightMin;
    m_rainHeightMax = rainHeightMax;
    if (camera) {
        m_frustumPlanes = camera->GetFrustumPlanes();
        m_frustumCullingEnabled = true;
    }
    else {
        m_frustumCullingEnabled = false;
    }
}

void GPUParticleSystem::setVisibleChunksInfo(const glm::vec3& cameraPosition,
    const std::vector<glm::ivec2>& visibleChunkPositions) {
    m_visibleChunkPositions = visibleChunkPositions;
    m_hasVisibleChunksInfo = !visibleChunkPositions.empty();
}