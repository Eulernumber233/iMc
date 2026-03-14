#include "ParticleManager.h"
#include <iostream>

ParticleManager::ParticleManager() {
}

ParticleManager::~ParticleManager() {
}

bool ParticleManager::initialize() {
    if (m_initialized) return true;

    // 初始化GPU粒子系统
    if (!m_gpuParticleSystem.initialize()) {
        std::cerr << "Failed to initialize GPU particle system!" << std::endl;
        return false;
    }

    // 初始化ECS粒子系统
    if (!m_ecsParticleSystem.initialize()) {
        std::cerr << "Failed to initialize ECS particle system!" << std::endl;
        return false;
    }

    m_initialized = true;
    return true;
}

void ParticleManager::update(float deltaTime,
                              const std::shared_ptr<Camera>& camera,
                              const std::vector<glm::ivec2>& visibleChunkPositions) {
    if (!m_initialized) return;

    // 设置视锥剔除参数
    if (camera) {
        m_gpuParticleSystem.setFrustumCullingParams(camera);
    }

    // 设置可见区块信息（用于优化粒子生成）
    m_gpuParticleSystem.setVisibleChunksInfo(camera->Position, visibleChunkPositions);

    // 更新GPU粒子系统
    m_gpuParticleSystem.update(deltaTime, camera->Position);

    // 更新ECS粒子系统
    //m_ecsParticleSystem.update(deltaTime);
}

void ParticleManager::render(const glm::mat4& view, const glm::mat4& projection) {
    if (!m_initialized) return;

    // 先渲染GPU粒子（天气，背景）
    m_gpuParticleSystem.render(view, projection);

    // 再渲染ECS粒子（前景，如方块碎片）
    //m_ecsParticleSystem.render(view, projection);
}

ParticleConfig ParticleManager::getWeatherConfig(ParticleType weatherType) const {
    ParticleConfig config;

    switch (weatherType) {
    case ParticleType::WeatherSnow:
        config.type = ParticleType::WeatherSnow;
        config.behaviorFlags = ParticleBehaviorFlags::GRAVITY | ParticleBehaviorFlags::WIND;
        config.spawnAreaMin = glm::vec3(-50.0f, 30.0f, -50.0f);
        config.spawnAreaMax = glm::vec3(50.0f, 50.0f, 50.0f);
        config.velocityMin = glm::vec3(-0.2f, -1.5f, -0.2f);
        config.velocityMax = glm::vec3(0.2f, -0.5f, 0.2f);
        config.colorStart = glm::vec4(1.0f, 1.0f, 1.0f, 0.8f);
        config.colorEnd = glm::vec4(1.0f, 1.0f, 1.0f, 0.2f);
        config.sizeStart = 0.08f;
        config.sizeEnd = 0.04f;
        config.lifetimeMin = 8.0f;
        config.lifetimeMax = 15.0f;
        config.spawnRate = 500.0f;
        config.maxParticles = 50000;
        config.gravity = glm::vec3(0.0f, -1.0f, 0.0f);
        config.windForce = m_globalWind;
        config.damping = 0.99f;
        config.randomSeed = 12345;
        break;

    case ParticleType::WeatherRain:
        config.type = ParticleType::WeatherRain;
        config.behaviorFlags = ParticleBehaviorFlags::GRAVITY | ParticleBehaviorFlags::WIND;
        // 这些区域参数现在主要用于初始化和回退，实际生成位置由视锥剔除控制
        config.spawnAreaMin = glm::vec3(-100.0f, 40.0f, -100.0f);
        config.spawnAreaMax = glm::vec3(100.0f, 70.0f, 100.0f);
        config.velocityMin = glm::vec3(-0.2f, -8.0f, -0.2f);
        config.velocityMax = glm::vec3(0.2f, -6.0f, 0.2f);
        config.colorStart = glm::vec4(0.5f, 0.6f, 0.9f, 0.8f);
        config.colorEnd = glm::vec4(0.5f, 0.6f, 0.9f, 0.4f);
        config.sizeStart = 0.06f;
        config.sizeEnd = 0.04f;
        config.lifetimeMin = 2.0f;
        config.lifetimeMax = 4.0f;
        config.spawnRate = 1000.0f;
        config.maxParticles = 100000;
        config.gravity = glm::vec3(0.0f, -15.0f, 0.0f); // 更快的下落速度
        config.windForce = m_globalWind;
        config.damping = 1.0f; // 无阻尼
        config.randomSeed = 54321;
        break;

    default:
        break;
    }

    return config;
}

bool ParticleManager::createWeatherEffect(ParticleType weatherType) {
    if (!m_initialized && !initialize()) {
        return false;
    }

    ParticleConfig config = getWeatherConfig(weatherType);
    if (config.type == ParticleType::Custom) {
        return false;
    }

    if (!m_gpuParticleSystem.createWeatherEffect(config)) {
        return false;
    }

    m_currentWeather = weatherType;
    m_weatherActive = true;
    return true;
}

void ParticleManager::toggleWeather() {
    if (!m_weatherActive) {
        // 激活下雪
        createWeatherEffect(ParticleType::WeatherSnow);
    }
    else {
        // 关闭天气
        m_gpuParticleSystem.reset();
        m_weatherActive = false;
        m_currentWeather = ParticleType::Custom;
    }
}

void ParticleManager::emitBlockDebris(const glm::vec3& blockPosition, BlockType blockType, int count) {
    if (!m_initialized) return;

    ParticleConfig config;
    config.type = ParticleType::BlockDebris;
    config.behaviorFlags = ParticleBehaviorFlags::GRAVITY;
    config.spawnAreaMin = blockPosition - glm::vec3(0.5f);
    config.spawnAreaMax = blockPosition + glm::vec3(0.5f);
    config.velocityMin = glm::vec3(-2.0f, 2.0f, -2.0f);
    config.velocityMax = glm::vec3(2.0f, 5.0f, 2.0f);

    // 根据方块类型设置颜色
    switch (blockType) {
    case BLOCK_STONE:
        config.colorStart = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        config.colorEnd = glm::vec4(0.3f, 0.3f, 0.3f, 0.0f);
        break;
    case BLOCK_DIRT:
        config.colorStart = glm::vec4(0.4f, 0.3f, 0.2f, 1.0f);
        config.colorEnd = glm::vec4(0.2f, 0.15f, 0.1f, 0.0f);
        break;
    case BLOCK_GRASS:
        config.colorStart = glm::vec4(0.2f, 0.6f, 0.3f, 1.0f);
        config.colorEnd = glm::vec4(0.1f, 0.3f, 0.15f, 0.0f);
        break;
    default:
        config.colorStart = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
        config.colorEnd = glm::vec4(0.4f, 0.4f, 0.4f, 0.0f);
        break;
    }

    config.sizeStart = 0.08f;
    config.sizeEnd = 0.04f;
    config.lifetimeMin = 1.5f;
    config.lifetimeMax = 3.0f;
    config.gravity = m_globalGravity;
    config.randomSeed = static_cast<unsigned int>(time(nullptr));

    m_ecsParticleSystem.emitBurst(config, blockPosition, count);
}

void ParticleManager::setWindForce(const glm::vec3& wind) {
    m_globalWind = wind;
    m_gpuParticleSystem.setGlobalForces(m_globalGravity, m_globalWind);
}

void ParticleManager::setGravity(const glm::vec3& gravity) {
    m_globalGravity = gravity;
    m_gpuParticleSystem.setGlobalForces(m_globalGravity, m_globalWind);
}

int ParticleManager::getTotalParticleCount() const {
    return m_gpuParticleSystem.getActiveParticleCount() +
           m_ecsParticleSystem.getActiveParticleCount();
}