#pragma once
#include "../core.h"
#include "../item/ItemStack.h"

// ── 掉落物实体 ──────────────────────────────────────────────────
// 世界里的一个掉落物：一个物品栈 + 位置 / 速度 + 动画状态。
// 由 DroppedItemManager 拥有并每帧更新（重力 + AABB 落地 + 旋转 + 拾取）。
struct DroppedItem {
    ItemStack stack;
    glm::vec3 pos = glm::vec3(0.0f);
    glm::vec3 vel = glm::vec3(0.0f);
    float age = 0.0f;          // 存活时间（秒）
    float spin = 0.0f;         // 绕 Y 旋转角（弧度）
    float bob = 0.0f;          // 上下浮动相位
    float pickupDelay = 0.5f;  // 抛出后不可拾取的时间
    bool  onGround = false;
    uint16_t netId = 0;        // 网络 id（服务端分配；0 = 单机/未联网）
};
