#include "NetPlayer.h"
#include <cmath>

// ============================================================================
// NetPlayer 远程插值
// ============================================================================

// 最短路径角度插值（度）：把 b-a 折到 [-180,180] 再线性，避免 359→1 绕一整圈。
static float shortestAngleLerp(float a, float b, float u) {
    float diff = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
    return a + diff * u;
}

void NetPlayer::pushInterpSnapshot(const glm::vec3& pos, float yaw, float pitch,
                                   const glm::vec3& vel) {
    InterpSnapshot& s = m_interpBuf[m_interpHead];
    s.t = netNowSeconds();
    s.pos = pos;
    s.yaw = yaw;
    s.pitch = pitch;
    s.vel = vel;
    m_interpHead = (m_interpHead + 1) % INTERP_BUFFER_SIZE;
    if (m_interpCount < INTERP_BUFFER_SIZE) m_interpCount++;
}

bool NetPlayer::sampleInterpolated(double renderTime, InterpSample& out) const {
    if (m_interpCount == 0) return false;

    const InterpSnapshot& oldest = interpAt(0);
    const InterpSnapshot& newest = interpAt(m_interpCount - 1);

    // renderTime 早于最旧快照（缓冲刚开始填、或延迟设得比缓冲跨度还大）→ 用最旧值兜底。
    if (renderTime <= oldest.t) {
        out.pos = oldest.pos; out.yaw = oldest.yaw;
        out.pitch = oldest.pitch; out.vel = oldest.vel;
        return true;
    }

    // renderTime 超过最新快照（下一个包还没到，说明丢包/发送端卡顿）→ 有界线性外推。
    if (renderTime >= newest.t) {
        double dt = renderTime - newest.t;
        if (dt > MAX_EXTRAP) dt = MAX_EXTRAP;  // 封顶，防过冲橡皮筋
        out.pos = newest.pos + newest.vel * static_cast<float>(dt);
        out.yaw = newest.yaw;      // 无角速度，朝向冻结
        out.pitch = newest.pitch;
        out.vel = newest.vel;
        return true;
    }

    // 常规路径：找夹住 renderTime 的相邻两快照做 Hermite 插值。
    for (int i = 0; i < m_interpCount - 1; ++i) {
        const InterpSnapshot& a = interpAt(i);
        const InterpSnapshot& b = interpAt(i + 1);
        if (renderTime >= a.t && renderTime <= b.t) {
            double span = b.t - a.t;
            float u = (span > 1e-6) ? static_cast<float>((renderTime - a.t) / span) : 0.0f;
            // Hermite 基函数；切线 = 端点速度 × span（把 d/dt 换成 d/du）。
            float u2 = u * u, u3 = u2 * u;
            float h00 = 2.0f * u3 - 3.0f * u2 + 1.0f;
            float h10 = u3 - 2.0f * u2 + u;
            float h01 = -2.0f * u3 + 3.0f * u2;
            float h11 = u3 - u2;
            glm::vec3 m0 = a.vel * static_cast<float>(span);
            glm::vec3 m1 = b.vel * static_cast<float>(span);
            out.pos = h00 * a.pos + h10 * m0 + h01 * b.pos + h11 * m1;
            out.yaw = shortestAngleLerp(a.yaw, b.yaw, u);
            out.pitch = a.pitch + (b.pitch - a.pitch) * u;
            out.vel = a.vel + (b.vel - a.vel) * u;
            return true;
        }
    }

    // 兜底（理论到不了）：返回最新值。
    out.pos = newest.pos; out.yaw = newest.yaw;
    out.pitch = newest.pitch; out.vel = newest.vel;
    return true;
}

// ============================================================================
// PlayerNetState
// ============================================================================

PlayerNetState::PlayerNetState() {
    REGISTER_PROP_BEGIN(PlayerNetState);
    REGISTER_PROP(m_position,     Unreliable, OnRep_Position);  // 0
    REGISTER_PROP(m_yaw,          Unreliable, OnRep_Look);      // 1
    REGISTER_PROP(m_pitch,        Unreliable, OnRep_Look);      // 2
    REGISTER_PROP(m_velocity,     Unreliable, OnRep_Motion);    // 3
    REGISTER_PROP(m_flags,        Unreliable, OnRep_Motion);    // 4
    REGISTER_PROP(m_swingCounter, Reliable,   OnRep_Swing);     // 5
    // 整背包：可靠 + NO_REBROADCAST（客户端上报服务端持久化，服务端不外发给其他客户端）
    REGISTER_PROP_NOREP_FLAGS(m_inventory, Reliable, NetObject::PROP_NO_REBROADCAST); // 6
    REGISTER_PROP(m_heldItemId, Reliable, OnRep_Held);          // 7
    REGISTER_PROP_END();
}

void PlayerNetState::setPosition(const glm::vec3& pos) {
    m_position = pos;
    // markDirty 通过 __propId 编号：按 REGISTER_PROP 顺序，m_position=0
    markDirty(0);
}

void PlayerNetState::setLook(float yaw, float pitch) {
    m_yaw = yaw;
    m_pitch = pitch;
    markDirty(1);  // m_yaw
    markDirty(2);  // m_pitch
}

void PlayerNetState::setMotion(const glm::vec3& velocity, uint8_t flags) {
    m_velocity = velocity;
    m_flags = flags;
    markDirty(3);  // m_velocity
    markDirty(4);  // m_flags
}

void PlayerNetState::setSwingCounter(uint8_t counter) {
    m_swingCounter = counter;
    markDirty(5);
}

void PlayerNetState::setInventory(const InventoryData& inv) {
    m_inventory = inv;
    markDirty(6);
}

void PlayerNetState::setHeldItem(const std::string& id) {
    m_heldItemId = id;
    markDirty(7);
}

void PlayerNetState::OnRep_Position() {
    if (m_owner) m_owner->updateCachedPosition(m_position);
}

void PlayerNetState::OnRep_Look() {
    if (m_owner) m_owner->updateCachedLook(m_yaw, m_pitch);
}

void PlayerNetState::OnRep_Motion() {
    if (m_owner) m_owner->updateCachedMotion(m_velocity, m_flags);
}

void PlayerNetState::OnRep_Swing() {
    if (m_owner) m_owner->onSwingCounter(m_swingCounter);
}

void PlayerNetState::OnRep_Held() {
    if (m_owner) m_owner->updateHeldItem(m_heldItemId);
}
