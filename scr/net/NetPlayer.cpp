#include "NetPlayer.h"

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
