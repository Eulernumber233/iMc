#pragma once
#include "../core.h"
#include "ParticleCommon.h"
#include "../Shader.h"
#include <vector>
#include "../Camera.h"



class GPUParticleSystem {
public:
    GPUParticleSystem();
    ~GPUParticleSystem();

    bool initialize();
    bool createWeatherEffect(const ParticleConfig& config);
    void update(float deltaTime, const glm::vec3& cameraPosition);
    void render(const glm::mat4& view, const glm::mat4& projection);

    int getActiveParticleCount() const { return m_activeParticles; }
    void setGlobalForces(const glm::vec3& gravity, const glm::vec3& wind);
    void reset();

    void setFrustumCullingParams(const std::shared_ptr<Camera>& camera,
        float rainHeightMin = 40.0f,
        float rainHeightMax = 70.0f);

    void setVisibleChunksInfo(const glm::vec3& cameraPosition,
        const std::vector<glm::ivec2>& visibleChunkPositions);

private:
    bool createParticleBuffers();
    void initializeParticles();

private:
    ParticleConfig m_config;

    Shader m_computeShader{ { GL_COMPUTE_SHADER, "shader/particle.comp" } };
    Shader m_renderShader{
        { GL_VERTEX_SHADER, "shader/particle.vert" },
        { GL_FRAGMENT_SHADER, "shader/particle.frag" }
    };

    GLuint m_particleBuffer[2] = { 0 };
    GLuint m_particleVAO = 0;
    int m_currentBuffer = 0;
    int m_activeParticles = 0;

    GLuint m_atomicCounter = 0;
    GLuint m_quadVBO = 0;
    GLuint m_textureArray = 0;

    // 存储每个可见区块的累计配额（前缀和）
    std::vector<GLuint> m_chunkCumulativeQuota;
    GLuint m_chunkQuotaSSBO = 0;       // 用于传递累计配额的SSBO

    std::mt19937 m_randomGen;
    float m_timeAccumulator = 0.0f;

    // 视锥剔除参数
    std::shared_ptr<Camera> m_camera;
    std::array<glm::vec4, 6> m_frustumPlanes;
    float m_rainHeightMin = 40.0f;
    float m_rainHeightMax = 70.0f;
    bool m_frustumCullingEnabled = false;

    // 可见区块信息
    std::vector<glm::ivec2> m_visibleChunkPositions;
    bool m_hasVisibleChunksInfo = false;

    // 存储可见区块的 SSBO
    GLuint m_chunksSSBO = 0;
    int m_chunksCount = 0;

    bool m_initialized = false;
    bool m_weatherEffectActive = false;
};