#include "NetPlayer.h"

// ============================================================================
// PlayerNetState
// ============================================================================

PlayerNetState::PlayerNetState() {
    REGISTER_PROP_BEGIN(PlayerNetState);
    REGISTER_PROP(m_position, Unreliable, OnRep_Position);
    REGISTER_PROP(m_yaw,      Unreliable, OnRep_Look);
    REGISTER_PROP(m_pitch,    Unreliable, OnRep_Look);
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

void PlayerNetState::OnRep_Position() {
    if (m_owner) m_owner->updateCachedPosition(m_position);
}

void PlayerNetState::OnRep_Look() {
    if (m_owner) m_owner->updateCachedLook(m_yaw, m_pitch);
}
