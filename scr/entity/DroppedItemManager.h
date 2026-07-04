#pragma once
#include "DroppedItem.h"
#include <vector>

class ChunkManager;
class Player;

// ── 掉落物管理器 ────────────────────────────────────────────────
// 拥有并每帧更新所有掉落物：重力 + AABB 落地碰撞 + 旋转/浮动动画 +
// 靠近玩家自动拾取（合并进背包）+ 邻近同类掉落物合并。单机本地，不联网同步。
class DroppedItemManager {
public:
    // 生成一个掉落物（stack 拷贝进来）。pos 为世界坐标，vel 为初速度。
    void spawn(const ItemStack& stack, const glm::vec3& pos, const glm::vec3& vel);

    // 每帧更新：物理 + 拾取 + 合并。player 用于拾取判定与入包。
    void update(float dt, ChunkManager& chunkManager, Player& player);

    const std::vector<DroppedItem>& items() const { return m_items; }
    size_t count() const { return m_items.size(); }

private:
    std::vector<DroppedItem> m_items;

    // item 小盒是否与实体方块相交（half = 半边长）
    bool boxHitsSolid(ChunkManager& cm, const glm::vec3& center, float half) const;
    // 沿单轴移动并做碰撞：碰撞则不移动并返回 true
    bool moveAxis(ChunkManager& cm, DroppedItem& it, int axis, float delta, float half);
    // 邻近同类掉落物合并
    void mergeNearby();
};
