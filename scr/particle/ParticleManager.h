#pragma once
#include "../core.h"
#include "../chunk/BlockType.h"
#include "ParticleCommon.h"
#include "GPUParticleSystem.h"
#include "ECSParticleSystem.h"

class ParticleManager {
public:
    ParticleManager();
    ~ParticleManager();

    // 初始化粒子管理器
    bool initialize();

    // 更新所有粒子系统
    void update(float deltaTime, const std::shared_ptr<Camera>& camera,
                const std::vector<glm::ivec2>& visibleChunkPositions = {});

    // 渲染所有粒子
    void render(const glm::mat4& view, const glm::mat4& projection);

    // 创建天气效果（雪、雨）
    bool createWeatherEffect(ParticleType weatherType);

    // 切换天气效果（通过快捷键'g'）
    void toggleWeather();

    // 发射方块破坏碎片
    void emitBlockDebris(const glm::vec3& blockPosition, BlockType blockType, int count = 50);

    // 设置全局风力
    void setWindForce(const glm::vec3& wind);

    // 设置全局重力
    void setGravity(const glm::vec3& gravity);

    // 获取统计信息
    int getTotalParticleCount() const;

private:
    // 获取天气配置
    ParticleConfig getWeatherConfig(ParticleType weatherType) const;

private:
    GPUParticleSystem m_gpuParticleSystem;
    ECSParticleSystem m_ecsParticleSystem;

    // 当前天气类型
    ParticleType m_currentWeather = ParticleType::Custom;
    bool m_weatherActive = false;

    // 全局参数
    glm::vec3 m_globalWind = glm::vec3(0.0f);
    glm::vec3 m_globalGravity = glm::vec3(0.0f, -9.8f, 0.0f);

    // 标志
    bool m_initialized = false;
};