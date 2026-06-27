#pragma once

#include "NetObject.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

#include "../enet/enet.h"

// ============================================================================
// PlayerNetState: 玩家可同步属性（NetObject 子类）
// ============================================================================

class NetPlayer; // 前向声明

class PlayerNetState : public NetObject {
public:
    PlayerNetState();

    // ---- 同步属性 ----
    REPLICATED(glm::vec3, m_position);
    REPLICATED(float,    m_yaw);
    REPLICATED(float,    m_pitch);

    // ---- 回调 ----
    void OnRep_Position();
    void OnRep_Look();

    // 由外部（Player 或远程插值器）调用，设置值并标记脏
    void setPosition(const glm::vec3& pos);
    void setLook(float yaw, float pitch);

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

    NetPlayer() : netState(std::make_unique<PlayerNetState>()) {}

    // 获取玩家当前位置/朝向（用于渲染远程玩家）
    glm::vec3 getRenderPosition() const { return m_renderPos; }
    float getRenderYaw() const   { return m_renderYaw; }
    float getRenderPitch() const { return m_renderPitch; }

    // 由 OnRep 回调更新缓存
    void updateCachedPosition(const glm::vec3& pos) { m_renderPos = pos; }
    void updateCachedLook(float yaw, float pitch) {
        m_renderYaw = yaw;
        m_renderPitch = pitch;
    }

private:
    glm::vec3 m_renderPos{0.0f};
    float m_renderYaw = 0.0f;
    float m_renderPitch = 0.0f;
};
