#include "ECSParticleSystem.h"
#include <iostream>

ECSParticleSystem::ECSParticleSystem()
    : m_particleVAO(0), m_particleVBO(0), m_instanceVBO(0) {
}

ECSParticleSystem::~ECSParticleSystem() {
    clear();
    if (m_particleVAO) glDeleteVertexArrays(1, &m_particleVAO);
    if (m_particleVBO) glDeleteBuffers(1, &m_particleVBO);
    if (m_instanceVBO) glDeleteBuffers(1, &m_instanceVBO);
}

bool ECSParticleSystem::initialize() {
    if (m_initialized) return true;

    // 创建渲染资源
    if (!createRenderResources()) {
        return false;
    }

    m_initialized = true;
    return true;
}

bool ECSParticleSystem::createRenderResources() {
    // 创建四边形VAO/VBO（与GPU粒子系统相同）
    float quadVertices[] = {
        -0.5f,  0.5f, 0.0f, 1.0f,
        -0.5f, -0.5f, 0.0f, 0.0f,
         0.5f, -0.5f, 1.0f, 0.0f,

        -0.5f,  0.5f, 0.0f, 1.0f,
         0.5f, -0.5f, 1.0f, 0.0f,
         0.5f,  0.5f, 1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_particleVAO);
    glGenBuffers(1, &m_particleVBO);
    glGenBuffers(1, &m_instanceVBO);

    glBindVertexArray(m_particleVAO);

    // 绑定四边形顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // 位置属性
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    // 纹理坐标属性
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    // 绑定实例数据缓冲区
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    // 预分配空间（假设最大粒子数）
    glBufferData(GL_ARRAY_BUFFER, ParticleConstants::MAX_ECS_PARTICLES * sizeof(ECSInstanceData),
        nullptr, GL_DYNAMIC_DRAW);

    // 实例属性：位置（vec4）
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(ECSInstanceData), (void*)0);
    glVertexAttribDivisor(2, 1);

    // 实例属性：速度（vec4）
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(ECSInstanceData), (void*)offsetof(ECSInstanceData, velocity));
    glVertexAttribDivisor(3, 1);

    // 实例属性：颜色（vec4）
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(ECSInstanceData), (void*)offsetof(ECSInstanceData, color));
    glVertexAttribDivisor(4, 1);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}


void ECSParticleSystem::emitBurst(const ParticleConfig& config, const glm::vec3& position, int count) {
    std::mt19937 gen(config.randomSeed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        auto entity = m_registry.create();
        m_particleEntities.push_back(entity);

        // 随机位置偏移
        glm::vec3 offset(
            dist(gen) * 2.0f - 1.0f,
            dist(gen) * 2.0f - 1.0f,
            dist(gen) * 2.0f - 1.0f
        );
        offset *= 0.5f; // 缩小偏移范围

        // 随机速度
        glm::vec3 velocity = config.velocityMin + (config.velocityMax - config.velocityMin) *
            glm::vec3(dist(gen), dist(gen), dist(gen));

        // 随机生命周期
        float lifetime = config.lifetimeMin + (config.lifetimeMax - config.lifetimeMin) * dist(gen);

        // 随机大小
        float size = config.sizeStart + (config.sizeEnd - config.sizeStart) * dist(gen);

        // 随机颜色插值因子
        float t = dist(gen);
        glm::vec4 color = config.colorStart + (config.colorEnd - config.colorStart) * t;

        // 添加粒子组件
        m_registry.emplace<ParticleComponent>(entity,
            position + offset,
            velocity,
            color,
            size,
            lifetime,
            config.behaviorFlags,
            config.gravity
        );
    }

    m_activeParticles += count;
}

void ECSParticleSystem::applyPhysics(ParticleComponent& particle, float deltaTime) {
    // 应用重力
    if (particle.behaviorFlags & ParticleBehaviorFlags::GRAVITY) {
        particle.velocity += particle.gravity * deltaTime;
    }

    // 应用风力（如果有）
    // 可以添加全局风力

    // 应用阻尼（可以后期从配置中获取）
    particle.velocity *= 0.99f;

    // 更新位置
    particle.position += particle.velocity * deltaTime;

    // 更新生命周期
    particle.lifetime -= deltaTime;
}

void ECSParticleSystem::update(float deltaTime) {
    // 遍历所有粒子实体
    auto view = m_registry.view<ParticleComponent>();
    m_activeParticles = 0;

    std::vector<entt::entity> toRemove;

    for (auto entity : view) {
        auto& particle = view.get<ParticleComponent>(entity);

        // 应用物理
        applyPhysics(particle, deltaTime);

        // 检查粒子是否死亡
        if (particle.lifetime <= 0.0f) {
            toRemove.push_back(entity);
        }
        else {
            m_activeParticles++;
        }
    }

    // 移除死亡粒子
    for (auto entity : toRemove) {
        m_registry.destroy(entity);
        // 从m_particleEntities中移除
        auto it = std::find(m_particleEntities.begin(), m_particleEntities.end(), entity);
        if (it != m_particleEntities.end()) {
            m_particleEntities.erase(it);
        }
    }
}

void ECSParticleSystem::collectInstanceData() {
    m_instanceData.clear();

    auto view = m_registry.view<ParticleComponent>();
    for (auto entity : view) {
        const auto& particle = view.get<ParticleComponent>(entity);

        // 归一化生命周期（0-1），0表示死亡，1表示新生
        float normalizedLifetime = particle.lifetime / particle.maxLifetime;

        // 创建ECS实例数据
        ECSInstanceData instance;
        instance.position = glm::vec4(particle.position, normalizedLifetime);
        instance.velocity = glm::vec4(particle.velocity, particle.size);
        instance.color = particle.color;

        m_instanceData.push_back(instance);
    }
}

void ECSParticleSystem::render(const glm::mat4& view, const glm::mat4& projection) {
    if (m_activeParticles == 0) return;

    // 收集实例数据
    collectInstanceData();

    // 启用混合和深度测试
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // 使用着色器
    m_particleShader.use();
    m_particleShader.setMat4("view", view);
    m_particleShader.setMat4("projection", projection);

    // 设置uniform：ECS粒子使用每个粒子的颜色和大小
    m_particleShader.setInt("usePerParticleColor", 1);
    m_particleShader.setInt("usePerParticleSize", 1);
    m_particleShader.setFloat("maxLifetime", 1.0f); // 因为生命周期已经归一化
    // 以下uniform不会被使用，但为了安全设置默认值
    m_particleShader.setVec4("colorStart", glm::vec4(1.0f));
    m_particleShader.setVec4("colorEnd", glm::vec4(1.0f));
    m_particleShader.setFloat("sizeStart", 1.0f);
    m_particleShader.setFloat("sizeEnd", 1.0f);
    m_particleShader.setInt("useTexture", 0);

    // 上传实例数据
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
        m_instanceData.size() * sizeof(ECSInstanceData),
        m_instanceData.data());

    // 绘制
    glBindVertexArray(m_particleVAO);
    glDrawArraysInstanced(GL_TRIANGLES, 0, 6, static_cast<GLsizei>(m_instanceData.size()));

    // 清理
    glBindVertexArray(0);
    glDisable(GL_BLEND);
}

void ECSParticleSystem::clear() {
    // 销毁所有实体
    for (auto entity : m_particleEntities) {
        m_registry.destroy(entity);
    }
    m_particleEntities.clear();
    m_instanceData.clear();
    m_activeParticles = 0;
}