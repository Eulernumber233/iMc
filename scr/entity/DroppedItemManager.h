#pragma once
#include "DroppedItem.h"
#include <vector>
#include <functional>
#include <cstdint>

class ChunkManager;
class Player;

// ── 掉落物管理器 ────────────────────────────────────────────────
// 拥有并每帧更新所有掉落物：重力 + AABB 落地碰撞 + 旋转/浮动动画 +
// 靠近玩家自动拾取（合并进背包）+ 邻近同类掉落物合并。
//
// 联机（需求 3）：服务端权威模拟——生成/销毁经回调广播给客户端，位置由 World 定时
// 批量同步。客户端置 clientMode：不跑物理/拾取，只播动画，实体由网络 spawn/despawn/同步
// 驱动；客户端本地的丢弃/破坏改为向服务端发「生成请求」（onDropRequest）。
class DroppedItemManager {
public:
    // 生成一个掉落物（stack 拷贝进来）。pos 为世界坐标，vel 为初速度。
    // 服务端/单机：真正生成（分配 netId 并触发 onSpawn）。
    // 客户端：不本地生成，转成 onDropRequest 请求服务端生成。
    void spawn(const ItemStack& stack, const glm::vec3& pos, const glm::vec3& vel);

    // 每帧更新：物理 + 拾取 + 合并。player 用于拾取判定与入包。
    // clientMode 下退化为只播动画（updateClientVisual）。
    void update(float dt, ChunkManager& chunkManager, Player& player);

    const std::vector<DroppedItem>& items() const { return m_items; }
    size_t count() const { return m_items.size(); }

    // ---- 联机 ----
    void setClientMode(bool v) { m_clientMode = v; }
    bool isClientMode() const { return m_clientMode; }

    // 服务端：生成/销毁回调（World 用来广播 SPAWN/DESTROY）
    void setServerCallbacks(std::function<void(const DroppedItem&)> onSpawn,
                            std::function<void(uint16_t)> onDespawn) {
        m_onSpawn = std::move(onSpawn);
        m_onDespawn = std::move(onDespawn);
    }
    // 客户端：本地丢弃/破坏 → 请求服务端生成
    void setDropRequestCallback(
        std::function<void(const ItemStack&, const glm::vec3&, const glm::vec3&)> cb) {
        m_onDropRequest = std::move(cb);
    }

    // 客户端：网络驱动的实体生命周期
    void netSpawn(uint16_t netId, const ItemStack& stack, const glm::vec3& pos);
    void netApply(uint16_t netId, const glm::vec3& pos, int count);  // 位置/数量同步
    void netDespawn(uint16_t netId);

private:
    std::vector<DroppedItem> m_items;

    bool m_clientMode = false;
    uint16_t m_nextNetId = 0x1000;  // = NetConstants::DROPPED_ITEM_NETID_BASE
    std::function<void(const DroppedItem&)> m_onSpawn;
    std::function<void(uint16_t)> m_onDespawn;
    std::function<void(const ItemStack&, const glm::vec3&, const glm::vec3&)> m_onDropRequest;

    DroppedItem* findByNetId(uint16_t netId);

    // 客户端：只推进动画（旋转/浮动），不做物理/拾取
    void updateClientVisual(float dt);

    // item 小盒是否与实体方块相交（half = 半边长）
    bool boxHitsSolid(ChunkManager& cm, const glm::vec3& center, float half) const;
    // 沿单轴移动并做碰撞：碰撞则不移动并返回 true
    bool moveAxis(ChunkManager& cm, DroppedItem& it, int axis, float delta, float half);
    // 邻近同类掉落物合并
    void mergeNearby();
};
