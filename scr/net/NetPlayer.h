#pragma once

#include "NetObject.h"
#include "../mode/PlayerAnimator.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

#include "../enet/enet.h"

// ============================================================================
// PlayerNetState: 玩家可同步属性（NetObject 子类）
// ============================================================================

class NetPlayer; // 前向声明

// m_flags 位定义（运动状态打包成 1 字节，走不可靠通道）
namespace PlayerFlagBits {
    constexpr uint8_t ON_GROUND = 0x01;
    constexpr uint8_t CROUCHING = 0x02;
    constexpr uint8_t RUNNING   = 0x04;
}

class PlayerNetState : public NetObject {
public:
    PlayerNetState();

    // ---- 同步属性（propId 顺序即 REGISTER_PROP 顺序，勿随意插入中间）----
    REPLICATED(glm::vec3, m_position);   // 0
    REPLICATED(float,     m_yaw);        // 1
    REPLICATED(float,     m_pitch);      // 2
    REPLICATED(glm::vec3, m_velocity);   // 3  水平速度用于步态；含竖直分量无妨
    REPLICATED(uint8_t,   m_flags);      // 4  PlayerFlagBits 组合（onGround/crouch/run）
    REPLICATED(uint8_t,   m_swingCounter);// 5  挥手事件计数（低字节），变化即触发一次挥手
    REPLICATED(InventoryData, m_inventory);  // 6  整背包：owner→server，NO_REBROADCAST（服务端持久化，不外发）
    REPLICATED(std::string,   m_heldItemId); // 7  手持物 id：广播全端（供远程模型/挥手显示）

    // ---- 回调 ----
    void OnRep_Position();
    void OnRep_Look();
    void OnRep_Motion();   // 速度 / 运动标志更新
    void OnRep_Swing();    // 挥手计数更新
    void OnRep_Held();     // 手持物更新

    // 由外部（Player 或远程插值器）调用，设置值并标记脏
    void setPosition(const glm::vec3& pos);
    void setLook(float yaw, float pitch);
    void setMotion(const glm::vec3& velocity, uint8_t flags);
    void setSwingCounter(uint8_t counter);
    void setInventory(const InventoryData& inv);
    void setHeldItem(const std::string& id);

    // 设置所属 NetPlayer（OnRep 回调需要）
    void setOwner(NetPlayer* owner) { m_owner = owner; }

private:
    NetPlayer* m_owner = nullptr;
    friend class NetPlayer;
};

// ============================================================================
// NetPlayer: 网络玩家
// ============================================================================

class NetPlayer {
public:
    uint16_t playerId = 0;
    ENetPeer* peer = nullptr;        // 所属连接（服务端用，客户端本地玩家为 nullptr）
    std::string playerName;
    std::string skinName;
    int renderRadius = 0;            // 该玩家上报的渲染半径（服务端用于 per-player 加载/推送）
    std::unique_ptr<PlayerNetState> netState;  // 拥有 PlayerNetState

    // 远程玩家动画器：由 renderRemotePlayers 每帧用复制过来的运动状态驱动，
    // 复现走/跑/蹲/待机与挥手（需求 1，状态复制 + 挥手事件计数方案）。
    PlayerAnimator animator;

    NetPlayer() : netState(std::make_unique<PlayerNetState>()) {}

    // 获取玩家当前位置/朝向（用于渲染远程玩家）
    glm::vec3 getRenderPosition() const { return m_renderPos; }
    float getRenderYaw() const   { return m_renderYaw; }
    float getRenderPitch() const { return m_renderPitch; }

    // 运动状态（供动画器 Input）
    glm::vec3 getRenderVelocity() const { return m_renderVel; }
    bool isOnGround()  const { return m_onGround; }
    bool isCrouching() const { return m_crouching; }
    bool isRunning()   const { return m_running; }

    // 手持物 id（远程渲染用；空串 = 空手）
    const std::string& getHeldItemId() const { return m_heldItemId; }
    void updateHeldItem(const std::string& id) { m_heldItemId = id; }

    // 由 OnRep 回调更新缓存
    void updateCachedPosition(const glm::vec3& pos) { m_renderPos = pos; }
    void updateCachedLook(float yaw, float pitch) {
        m_renderYaw = yaw;
        m_renderPitch = pitch;
    }
    void updateCachedMotion(const glm::vec3& vel, uint8_t flags) {
        m_renderVel = vel;
        m_onGround  = (flags & PlayerFlagBits::ON_GROUND) != 0;
        m_crouching = (flags & PlayerFlagBits::CROUCHING) != 0;
        m_running   = (flags & PlayerFlagBits::RUNNING)   != 0;
    }
    // 收到挥手计数：与上次不同则触发一次挥手（首帧只记录不触发，避免加入时幻影挥手）
    void onSwingCounter(uint8_t counter) {
        if (m_lastSwingByte < 0) { m_lastSwingByte = counter; return; }
        if ((uint8_t)m_lastSwingByte != counter) {
            m_lastSwingByte = counter;
            animator.triggerSwingArm();
        }
    }

private:
    glm::vec3 m_renderPos{0.0f};
    float m_renderYaw = 0.0f;
    float m_renderPitch = 0.0f;

    glm::vec3 m_renderVel{0.0f};
    bool m_onGround = true;
    bool m_crouching = false;
    bool m_running = false;
    int  m_lastSwingByte = -1;  // -1 = 尚未收到过
    std::string m_heldItemId;   // 手持物 id 缓存
};
