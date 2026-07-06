#include "DroppedItemManager.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/BlockType.h"
#include "../Player.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace {
    const float GRAVITY       = 20.0f;
    const float MAX_FALL      = 30.0f;
    const float ITEM_HALF     = 0.13f;   // 掉落物碰撞盒半边长（约 0.25³）
    const float PICKUP_RANGE  = 1.5f;
    const float MERGE_RANGE   = 0.8f;    // 合并距离
    const float ATTRACT_RANGE = 2.0f;    // 同类相互吸引距离（先聚拢再合并）
    const float ATTRACT_ACCEL = 6.0f;    // 吸引加速度
}

void DroppedItemManager::spawn(const ItemStack& stack, const glm::vec3& pos, const glm::vec3& vel) {
    if (stack.empty()) return;
    // 客户端：不本地生成，转成向服务端的生成请求（服务端权威生成后再广播回来）
    if (m_clientMode) {
        if (m_onDropRequest) m_onDropRequest(stack, pos, vel);
        return;
    }
    DroppedItem it;
    it.stack = stack;
    it.pos = pos;
    it.vel = vel;
    it.netId = m_nextNetId++;
    m_items.push_back(it);
    if (m_onSpawn) m_onSpawn(m_items.back());  // 服务端广播 SPAWN
}

DroppedItem* DroppedItemManager::findByNetId(uint16_t netId) {
    for (auto& it : m_items) if (it.netId == netId) return &it;
    return nullptr;
}

void DroppedItemManager::updateClientVisual(float dt) {
    const float rotSpeed = 1.5f;
    for (auto& it : m_items) {
        it.spin += rotSpeed * dt;
        it.bob += dt;
    }
}

void DroppedItemManager::netSpawn(uint16_t netId, const ItemStack& stack, const glm::vec3& pos) {
    if (DroppedItem* ex = findByNetId(netId)) { ex->stack = stack; ex->pos = pos; return; }
    DroppedItem it;
    it.stack = stack;
    it.pos = pos;
    it.netId = netId;
    it.pickupDelay = 0.0f;
    m_items.push_back(it);
}

void DroppedItemManager::netApply(uint16_t netId, const glm::vec3& pos, int count) {
    DroppedItem* it = findByNetId(netId);
    if (!it) return;
    it->pos = pos;
    if (count > 0) it->stack.count = count;
}

void DroppedItemManager::netDespawn(uint16_t netId) {
    m_items.erase(
        std::remove_if(m_items.begin(), m_items.end(),
            [netId](const DroppedItem& it) { return it.netId == netId; }),
        m_items.end());
}

bool DroppedItemManager::boxHitsSolid(ChunkManager& cm, const glm::vec3& c, float half) const {
    int minX = (int)std::floor(c.x - half), maxX = (int)std::floor(c.x + half);
    int minY = (int)std::floor(c.y - half), maxY = (int)std::floor(c.y + half);
    int minZ = (int)std::floor(c.z - half), maxZ = (int)std::floor(c.z + half);
    for (int x = minX; x <= maxX; ++x)
        for (int y = minY; y <= maxY; ++y)
            for (int z = minZ; z <= maxZ; ++z) {
                BlockState b = cm.getBlockAt(glm::ivec3(x, y, z));
                if (b.type() != BLOCK_AIR && GetBlockProperties(b.type()).isSolid)
                    return true;
            }
    return false;
}

bool DroppedItemManager::moveAxis(ChunkManager& cm, DroppedItem& it, int axis, float delta, float half) {
    if (delta == 0.0f) return false;
    glm::vec3 np = it.pos;
    np[axis] += delta;
    if (boxHitsSolid(cm, np, half)) return true; // 碰撞：不移动
    it.pos = np;
    return false;
}

void DroppedItemManager::update(float dt, ChunkManager& chunkManager, Player& player) {
    // 客户端：不跑物理/拾取，实体由网络驱动，这里只播动画
    if (m_clientMode) { updateClientVisual(dt); return; }

    const float rotSpeed = 1.5f;
    glm::vec3 pc = player.getPosition();

    // 同类掉落物相互吸引：让在途的同种物品先聚拢，从而更容易堆叠合并。
    // 只影响水平速度（保持重力自然下落），且未满栈才吸引。
    for (size_t i = 0; i < m_items.size(); ++i) {
        DroppedItem& a = m_items[i];
        if (a.stack.empty() || a.stack.count >= a.stack.maxStack()) continue;
        for (size_t j = i + 1; j < m_items.size(); ++j) {
            DroppedItem& b = m_items[j];
            if (b.stack.empty() || !a.stack.sameItem(b.stack)) continue;
            glm::vec3 d = b.pos - a.pos;
            float dist = glm::length(d);
            if (dist < 1e-4f || dist > ATTRACT_RANGE) continue;
            glm::vec3 dir = d / dist;
            float pull = ATTRACT_ACCEL * dt;
            a.vel.x += dir.x * pull;  a.vel.z += dir.z * pull;
            b.vel.x -= dir.x * pull;  b.vel.z -= dir.z * pull;
        }
    }

    for (auto& it : m_items) {
        it.age += dt;
        it.spin += rotSpeed * dt;
        it.bob += dt;
        if (it.pickupDelay > 0.0f) it.pickupDelay -= dt;

        // 重力
        it.vel.y -= GRAVITY * dt;
        if (it.vel.y < -MAX_FALL) it.vel.y = -MAX_FALL;

        // 垂直移动 + 落地判定
        bool hitY = moveAxis(chunkManager, it, 1, it.vel.y * dt, ITEM_HALF);
        if (hitY) {
            it.onGround = (it.vel.y < 0.0f);
            it.vel.y = 0.0f;
        } else {
            it.onGround = false;
        }

        // 水平移动
        moveAxis(chunkManager, it, 0, it.vel.x * dt, ITEM_HALF);
        moveAxis(chunkManager, it, 2, it.vel.z * dt, ITEM_HALF);

        // 阻尼（落地强、空中弱），帧率无关
        float damp = it.onGround ? 10.0f : 1.0f;
        float f = std::exp(-damp * dt);
        it.vel.x *= f;
        it.vel.z *= f;
    }

    // 邻近同类合并
    mergeNearby();

    // 拾取 + 清理
    for (auto& it : m_items) {
        if (it.pickupDelay <= 0.0f && !it.stack.empty()) {
            if (glm::distance(it.pos, pc) < PICKUP_RANGE) {
                player.addToInventory(it.stack); // 就地减少；余量留在掉落物里
            }
        }
    }

    // 移除已被完全拾取 / 掉出世界的项；服务端在移除前通知 onDespawn 广播 DESTROY
    m_items.erase(
        std::remove_if(m_items.begin(), m_items.end(),
            [this](const DroppedItem& it) {
                bool gone = it.stack.empty() || it.pos.y < -64.0f;
                if (gone && m_onDespawn && it.netId != 0) m_onDespawn(it.netId);
                return gone;
            }),
        m_items.end());
}

void DroppedItemManager::mergeNearby() {
    for (size_t i = 0; i < m_items.size(); ++i) {
        if (m_items[i].stack.empty()) continue;
        for (size_t j = i + 1; j < m_items.size(); ++j) {
            DroppedItem& a = m_items[i];
            DroppedItem& b = m_items[j];
            if (b.stack.empty() || !a.stack.sameItem(b.stack)) continue;
            if (glm::distance(a.pos, b.pos) > MERGE_RANGE) continue;
            int space = a.stack.maxStack() - a.stack.count;
            if (space <= 0) continue;
            int move = std::min(space, b.stack.count);
            a.stack.count += move;
            b.stack.count -= move;
            if (b.stack.count <= 0) b.stack.clear();
        }
    }
}
