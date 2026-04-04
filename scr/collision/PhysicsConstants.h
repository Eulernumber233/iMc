#pragma once

// 物理常量定义
// 世界尺度：1区块 = 16×16×64 米，1方块 = 1m³
// 玩家碰撞箱：0.4×0.4×0.7 米
// 速度单位：m/s，加速度单位：m/s²
namespace PhysicsConstants {
    // 重力加速度 (m/s²) - Minecraft风格，略高于现实
    constexpr float GRAVITY = 20.0f;

    // 空气密度 (kg/m³)
    constexpr float AIR_DENSITY = 1.225f;

    // 水的密度 (kg/m³)
    constexpr float WATER_DENSITY = 1000.0f;

    // 最大速度限制 (m/s)
    constexpr float MAX_HORIZONTAL_SPEED = 8.0f;   // 水平最大速度（略高于跑步速度）
    constexpr float MAX_VERTICAL_SPEED = 30.0f;    // 最大垂直速度（上升）
    constexpr float MAX_FALL_SPEED = 40.0f;        // 最大下落速度

    // ============ 玩家移动参数（开放调整） ============
    // 三速系统目标速度 (m/s)
    constexpr float PLAYER_WALK_SPEED = 4.3f;     // 行走速度 - Minecraft默认约4.3 m/s
    constexpr float PLAYER_RUN_SPEED = 5.6f;      // 跑步速度 - 需要前进键双击
    constexpr float PLAYER_CROUCH_SPEED = 1.3f;   // 蹲伏速度
    constexpr float PLAYER_AIR_SPEED = 4.3f;      // 空中水平移动控制速度（同行走）

    // 加速/减速参数 (m/s²) - 控制惯性手感
    constexpr float GROUND_ACCEL = 50.0f;          // 地面加速度 - ~0.1秒达到行走速度
    constexpr float GROUND_DECEL = 50.0f;          // 地面减速度 - ~0.1秒停下
    constexpr float AIR_ACCEL = 10.0f;             // 空中加速度（较弱的空中控制）
    constexpr float AIR_DECEL = 5.0f;              // 空中减速度

    // 跳跃参数
    constexpr float PLAYER_MASS = 70.0f;           // 质量 (kg)
    constexpr float PLAYER_JUMP_VELOCITY = 7.5f;   // 跳跃初速度 (m/s) - 约跳1.4m高

    // 控制参数
    constexpr float DOUBLE_TAP_THRESHOLD = 0.3f;   // 前进键双击检测阈值（秒）

    // 碰撞箱尺寸 (m)
    constexpr float PLAYER_WIDTH = 0.4f;           // 玩家宽度 X
    constexpr float PLAYER_HEIGHT_STANDING = 0.7f; // 玩家站立高度 Y
    constexpr float PLAYER_HEIGHT_CROUCHING = 0.35f; // 玩家蹲伏高度 Y
    constexpr float PLAYER_DEPTH = 0.4f;           // 玩家深度 Z

    // 摄像机偏移 (m) - 相对于碰撞箱中心
    constexpr float CAMERA_OFFSET_Y_STANDING = 0.9f;  // 站立时摄像机在碰撞箱上方的偏移
    constexpr float CAMERA_OFFSET_Y_CROUCHING = 0.0f; // 蹲伏时

    // 物理迭代参数
    constexpr int MAX_PHYSICS_ITERATIONS = 4;      // 最大碰撞解算迭代次数
    constexpr float PHYSICS_BIAS = 0.001f;         // 物理偏置，避免振荡
    constexpr float PENETRATION_SLOP = 0.005f;     // 穿透容差

    // 恢复系数（弹性）
    constexpr float RESTITUTION_STONE = 0.1f;
    constexpr float RESTITUTION_WOOD = 0.3f;
    constexpr float RESTITUTION_SLIME = 0.8f;

    // 旧接口兼容（已废弃，勿直接使用）
    constexpr float PLAYER_JUMP_FORCE = PLAYER_JUMP_VELOCITY;
    constexpr float PLAYER_AIR_CONTROL = 1.0f;
    constexpr float FRICTION_GROUND = 0.0f;
    constexpr float FRICTION_AIR = 0.0f;
    constexpr float FRICTION_WATER = 15.0f;
}

// 物理工具函数
namespace PhysicsUtils {
    // 计算浮力
    inline float calculateBuoyancy(float volume, float fluidDensity) {
        return volume * fluidDensity * PhysicsConstants::GRAVITY;
    }

    // 计算阻力 (简化模型)
    inline float calculateDrag(float velocity, float crossSection, float dragCoefficient) {
        return 0.5f * PhysicsConstants::AIR_DENSITY * velocity * velocity * crossSection * dragCoefficient;
    }

    // 计算动量
    inline float calculateMomentum(float mass, float velocity) {
        return mass * velocity;
    }

    // 计算动能
    inline float calculateKineticEnergy(float mass, float velocity) {
        return 0.5f * mass * velocity * velocity;
    }

    // 限制值在范围内
    template<typename T>
    inline T clamp(T value, T min, T max) {
        if (value < min) return min;
        if (value > max) return max;
        return value;
    }

    // 线性插值
    template<typename T>
    inline T lerp(T a, T b, float t) {
        return a + (b - a) * t;
    }

    // 计算反射向量
    inline glm::vec3 reflectVector(const glm::vec3& vector, const glm::vec3& normal) {
        return vector - 2.0f * glm::dot(vector, normal) * normal;
    }

    // 计算投影向量
    inline glm::vec3 projectVector(const glm::vec3& vector, const glm::vec3& onto) {
        float dot = glm::dot(vector, onto);
        float lenSq = glm::dot(onto, onto);
        if (lenSq < 1e-6f) return glm::vec3(0.0f);
        return onto * (dot / lenSq);
    }

    // 计算拒绝向量（垂直于给定方向的分量）
    inline glm::vec3 rejectVector(const glm::vec3& vector, const glm::vec3& from) {
        return vector - projectVector(vector, from);
    }
}
