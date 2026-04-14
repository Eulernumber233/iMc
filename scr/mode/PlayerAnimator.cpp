#include "PlayerAnimator.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace {
    // 两个角（弧度）之间的最短差，结果在 (-PI, PI]
    float angleDiff(float target, float current) {
        float d = std::fmod(target - current + glm::pi<float>(), glm::two_pi<float>());
        if (d < 0.0f) d += glm::two_pi<float>();
        return d - glm::pi<float>();
    }

    // 带速率上限的指数插值：x 向 target 逼近，速率 rate (1/s)
    float smoothTowards(float x, float target, float rate, float dt) {
        float k = std::min(1.0f, rate * dt);
        return x + (target - x) * k;
    }
}

void PlayerAnimator::update(float deltaTime, const Input& in) {
    const PlayerAnimConfig& c = config;

    // ---- 1. 身体朝向追踪相机 ----
    // 约定：PlayerModel 在 yaw=0 时正面朝 +Z。
    // Camera.Yaw 度数，Front = (cos(θ), *, sin(θ))；rotate(yaw, Y) 把 +Z 转到
    // (sin(yaw), 0, cos(yaw))。解得 bodyYaw = π/2 - radians(cameraYaw)。
    float targetBodyYaw = glm::half_pi<float>() - glm::radians(in.cameraYaw);

    if (!m_bodyYawInitialized) {
        m_bodyYawRad = targetBodyYaw;
        m_bodyYawInitialized = true;
    } else {
        float speed = glm::length(in.horizontalVelocity);
        float trackRate = (speed > c.movingSpeedThreshold)
            ? c.bodyTrackRateMoving
            : c.bodyTrackRateIdle;
        float diff = angleDiff(targetBodyYaw, m_bodyYawRad);
        m_bodyYawRad += diff * std::min(1.0f, trackRate * deltaTime);
    }
    m_pose.bodyYaw = m_bodyYawRad;

    // ---- 2. 头部相对身体偏转 ----
    float headRelYaw = angleDiff(targetBodyYaw, m_bodyYawRad);
    m_pose.headYaw = std::clamp(headRelYaw, -c.headMaxYaw, c.headMaxYaw);
    m_pose.headPitch = -glm::radians(in.cameraPitch);

    // ---- 3. 步态频率：由绝对水平速度驱动 ----
    float horizSpeed = glm::length(in.horizontalVelocity);
    float gaitFreq = c.gaitFreqPerMetre * horizSpeed;
    if (in.running)   gaitFreq *= c.runFreqMultiplier;
    if (in.crouching) gaitFreq *= c.crouchFreqMultiplier;
    // 空中且为 Freeze 模式时冻结相位（落地无缝接上）
    if (in.onGround || c.airMode != PlayerAnimConfig::AirMode::Freeze) {
        m_gaitPhase += gaitFreq * deltaTime;
        if (m_gaitPhase > glm::two_pi<float>()) m_gaitPhase -= glm::two_pi<float>();
        if (m_gaitPhase < 0.0f)                 m_gaitPhase += glm::two_pi<float>();
    }

    // ---- 4. 四肢目标角度（连续函数，避免分支切换造成重影） ----
    // 振幅随速度比例增长
    float speedRatio = std::min(1.0f, horizSpeed / std::max(c.fullAmpSpeed, 0.01f));
    // 接近静止时的额外衰减（在 [0, idleSpeedThreshold] 区间把振幅平滑拉到 0）
    float activeFactor = std::clamp(horizSpeed / std::max(c.idleSpeedThreshold, 0.01f),
                                    0.0f, 1.0f);
    float swingScale = speedRatio * activeFactor;

    float legAmp = c.legSwingAmp * swingScale;
    float armAmp = c.armSwingAmp * swingScale;
    if (in.running) {
        legAmp *= c.runLegAmpMul;
        armAmp *= c.runArmAmpMul;
    }
    if (in.crouching) {
        legAmp *= c.crouchAmpMul;
        armAmp *= c.crouchAmpMul;
    }

    float s = std::sin(m_gaitPhase);
    // 地面步态目标（左右反相；手臂与同侧腿反相）
    float targetRLeg = s * legAmp;
    float targetLLeg = -s * legAmp;
    float targetRArm = -s * armAmp;
    float targetLArm = s * armAmp;

    if (!in.onGround) {
        if (c.airMode == PlayerAnimConfig::AirMode::Freeze) {
            // 冻结：保持当前四肢角度不变（目标=当前），相位也不再推进
            targetRLeg = m_curRightLegPitch;
            targetLLeg = m_curLeftLegPitch;
            targetRArm = m_curRightArmPitch;
            targetLArm = m_curLeftArmPitch;
        } else {
            // 插值到空中姿态
            float blend = std::min(1.0f, c.airBlendRate * deltaTime);
            targetRLeg = targetRLeg + (c.airLegPitch - targetRLeg) * blend;
            targetLLeg = targetLLeg + (c.airLegPitch - targetLLeg) * blend;
            targetRArm = targetRArm + (c.airArmPitch - targetRArm) * blend;
            targetLArm = targetLArm + (c.airArmPitch - targetLArm) * blend;
        }
    }

    // 平滑过渡（移动时快速跟随目标，接近静止时用 idleBlendRate 拉回中立位）
    // 空中 Freeze 模式不 blend（目标=当前，等价于不变）
    float blendRate;
    if (!in.onGround && c.airMode == PlayerAnimConfig::AirMode::Freeze) {
        blendRate = 0.0f;
    } else {
        blendRate = (horizSpeed > c.idleSpeedThreshold) ? 40.0f : c.idleBlendRate;
    }
    m_curRightLegPitch = smoothTowards(m_curRightLegPitch, targetRLeg, blendRate, deltaTime);
    m_curLeftLegPitch  = smoothTowards(m_curLeftLegPitch,  targetLLeg, blendRate, deltaTime);
    m_curRightArmPitch = smoothTowards(m_curRightArmPitch, targetRArm, blendRate, deltaTime);
    m_curLeftArmPitch  = smoothTowards(m_curLeftArmPitch,  targetLArm, blendRate, deltaTime);

    m_pose.rightLegPitch = m_curRightLegPitch;
    m_pose.leftLegPitch  = m_curLeftLegPitch;
    m_pose.rightArmPitch = m_curRightArmPitch;
    m_pose.leftArmPitch  = m_curLeftArmPitch;

    // ---- 5. 下蹲姿态 ----
    if (in.crouching) {
        m_pose.rootOffset.y = -c.crouchRootDip;
        m_pose.bodyPitch = c.crouchBodyPitch;
        m_pose.rightArmPitch += c.crouchArmExtraPitch;
        m_pose.leftArmPitch  += c.crouchArmExtraPitch;
    } else {
        m_pose.rootOffset.y = 0.0f;
        m_pose.bodyPitch = 0.0f;
    }

    // 手臂外张（下蹲时微微外张，避免穿模）
    float rollBase = in.crouching ? c.crouchArmRoll : 0.0f;
    m_pose.rightArmRoll = -rollBase;
    m_pose.leftArmRoll = rollBase;

    // ---- 6. 落地冲击 ----
    if (!m_wasOnGround && in.onGround) {
        m_landImpactTimer = c.landImpactDuration;
    }
    if (m_landImpactTimer > 0.0f) {
        float t = m_landImpactTimer / std::max(c.landImpactDuration, 0.001f);
        m_pose.rightLegPitch += c.landImpactLegExtraPitch * t;
        m_pose.leftLegPitch  += c.landImpactLegExtraPitch * t;
        m_pose.rootOffset.y  -= c.landImpactDip * t;
        m_landImpactTimer -= deltaTime;
        if (m_landImpactTimer < 0.0f) m_landImpactTimer = 0.0f;
    }
    m_wasOnGround = in.onGround;

    // ---- 7. 挥手动画（叠加在 rightArm 上） ----
    if (m_swingTimer > 0.0f) {
        // t ∈ [0,1]，0 = 刚触发，1 = 即将结束
        float t = 1.0f - m_swingTimer / std::max(c.swingDuration, 0.001f);
        t = std::clamp(t, 0.0f, 1.0f);
        // 半正弦曲线：中间达到峰值，两端为 0，自然过渡
        float envelope = std::sin(t * glm::pi<float>());
        m_pose.rightArmPitch += -c.swingArmPitchAmp * envelope;
        m_pose.rightArmRoll  += -c.swingArmRollAmp * envelope;

        m_swingTimer -= deltaTime;
        if (m_swingTimer < 0.0f) m_swingTimer = 0.0f;
    }
}

void PlayerAnimator::triggerSwingArm() {
    // 重新播放（长按破坏时每次冷却到期都重置，得到连续挥手）
    m_swingTimer = config.swingDuration;
}

float PlayerAnimator::getBodyYawDegrees() const {
    return glm::degrees(m_bodyYawRad);
}
