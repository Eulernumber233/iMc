#pragma once
#include "../core.h"

// 玩家模型的姿态（由动画器输出，由 PlayerModel::draw 消费）
// 所有旋转单位为弧度；每个关节的轴心由 PlayerModel 内部 pivot 决定
struct PlayerPose {
    // 整体
    glm::vec3 rootOffset = glm::vec3(0.0f); // 身体整体位移（下蹲时 Y 下沉等）
    float bodyYaw = 0.0f;                   // 身体朝向（绕 Y，滞后于相机）
    float bodyPitch = 0.0f;                 // 身体前倾（下蹲时）

    // 头部相对身体的旋转
    float headYaw = 0.0f;
    float headPitch = 0.0f;

    // 四肢绕 X 轴摆动（前后）
    float rightArmPitch = 0.0f;
    float leftArmPitch = 0.0f;
    float rightLegPitch = 0.0f;
    float leftLegPitch = 0.0f;

    // 四肢绕 Z 轴张开（跳跃时手臂略张、闲置摆动等）
    float rightArmRoll = 0.0f;
    float leftArmRoll = 0.0f;
};

// 动画器的可调参数。改这里就能改动画手感，不必动 .cpp
struct PlayerAnimConfig {
    // ---- 步态频率 ----
    // 相位推进速率：rad/s = gaitFreqPerMetre * 当前水平速度(m/s)
    // gaitFreqPerMetre = 2π 意味着每米走一个完整周期（两步）
    float gaitFreqPerMetre = glm::two_pi<float>() * 0.25f;
    float runFreqMultiplier = 1.25f;  // 跑步额外加速
    float crouchFreqMultiplier = 0.8f; // 下蹲时频率降低

    // ---- 摆动振幅（弧度） ----
    float legSwingAmp = glm::radians(55.0f);
    float armSwingAmp = glm::radians(40.0f);
    float runLegAmpMul = 1.25f;
    float runArmAmpMul = 1.4f;
    float crouchAmpMul = 0.5f;
    // 振幅随速度比例增长的参考速度（m/s）；超过此速度后振幅达到满值
    float fullAmpSpeed = 4.5f;

    // ---- 空中姿态 ----
    // 空中模式：
    //   Freeze     - 冻结相位与四肢，落地后无缝继续（MC 风格，默认）
    //   BlendToAir - 插值到 airLegPitch/airArmPitch（收腿抬手姿态）
    enum class AirMode { Freeze, BlendToAir };
    AirMode airMode = AirMode::Freeze;
    float airLegPitch = glm::radians(-10.0f);
    float airArmPitch = glm::radians(-15.0f);
    float airBlendRate = 4.0f;  // BlendToAir 模式下插值速率（1/s）

    // ---- 落地冲击 ----
    float landImpactDuration = 0.15f;
    float landImpactLegExtraPitch = glm::radians(15.0f);
    float landImpactDip = 0.05f; // rootOffset.y 额外下沉

    // ---- 身体朝向追踪 ----
    float bodyTrackRateMoving = 14.0f;  // 移动时身体快速跟随相机
    float bodyTrackRateIdle = 3.0f;     // 静止时缓慢跟随（允许头先转）
    float movingSpeedThreshold = 0.1f;  // 判定"移动"的速度阈值
    float headMaxYaw = glm::radians(75.0f); // 头相对身体最大扭转角

    // ---- 下蹲姿态 ----
    float crouchRootDip = 0.15f;         // rootOffset.y 下沉（方块单位）
    float crouchBodyPitch = glm::radians(20.0f);
    float crouchArmExtraPitch = glm::radians(10.0f);
    float crouchArmRoll = glm::radians(3.0f);

    // ---- 静止/停步平滑 ----
    float idleSpeedThreshold = 0.15f;  // 速度低于此值视为静止，振幅拉回 0
    float idleBlendRate = 10.0f;       // 静止时四肢回到中立位的速率（1/s）

    // ---- 挥手动画（挖/放块触发） ----
    float swingDuration = 0.25f;                 // 一次挥手的时长（秒）
    float swingArmPitchAmp = glm::radians(70.0f);  // 挥手前后摆幅（加到 rightArmPitch）
    float swingArmRollAmp = glm::radians(20.0f);   // 挥手外张幅度（加到 rightArmRoll）
    // 挥手曲线的相位：半个正弦 sin(πt)，在中间达到峰值
    // （如果手感偏快，可加大 swingDuration；偏僵，可减小 amp）
};

// 驱动 PlayerPose 的动画器
// 输入：玩家运动状态（水平速度、是否在地、是否下蹲、是否跑步、相机 yaw/pitch）
// 输出：当前帧 PlayerPose
class PlayerAnimator {
public:
    struct Input {
        glm::vec3 horizontalVelocity = glm::vec3(0.0f); // XZ 速度
        float walkSpeed = 4.0f;       // 参考走速（保留兼容，目前未使用，频率改由绝对速度驱动）
        bool onGround = true;
        bool crouching = false;
        bool running = false;
        float cameraYaw = 0.0f;       // 度数，直接用 Camera::Yaw
        float cameraPitch = 0.0f;     // 度数
    };

    // 可调参数（可随时外部修改）
    PlayerAnimConfig config;

    // 推进动画时间并根据输入更新 pose
    void update(float deltaTime, const Input& in);

    // 触发一次右手挥手动画（挖方块/放方块/交互）
    // 若正在挥手中再次触发：重置计时（长按破坏时每次 cooldown 重新挥一次，效果类似 MC 连续挥手）
    void triggerSwingArm();

    // 当前是否正在挥手
    bool isSwinging() const { return m_swingTimer > 0.0f; }

    // 读取当前帧的 pose
    const PlayerPose& getPose() const { return m_pose; }

    // 读取身体朝向（度数），供外部可视化/调试
    float getBodyYawDegrees() const;

    // 读取当前步态相位（调试）
    float getGaitPhase() const { return m_gaitPhase; }

private:
    PlayerPose m_pose;

    // 步态相位（0..2π 循环）
    float m_gaitPhase = 0.0f;

    // 身体朝向（弧度），平滑追踪相机 yaw
    float m_bodyYawRad = 0.0f;
    bool  m_bodyYawInitialized = false;

    // 落地冲击的残余动画时间
    float m_landImpactTimer = 0.0f;
    bool  m_wasOnGround = true;

    // 当前四肢目标角度（供平滑过渡）
    float m_curRightLegPitch = 0.0f;
    float m_curLeftLegPitch = 0.0f;
    float m_curRightArmPitch = 0.0f;
    float m_curLeftArmPitch = 0.0f;

    // 挥手动画剩余时间（秒）；>0 表示正在挥手
    float m_swingTimer = 0.0f;
};
