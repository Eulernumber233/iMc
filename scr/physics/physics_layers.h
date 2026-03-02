#pragma once
#include <cstdint>

// 碰撞层位掩码
enum CollisionLayer : uint32_t {
    LAYER_STATIC = 1 << 0,     // 静态方块
    LAYER_DYNAMIC = 1 << 1,    // 动态方块（门、活塞等）
    LAYER_ENTITY = 1 << 2,     // 实体（生物、玩家）
    LAYER_ITEM = 1 << 3,       // 掉落物
    LAYER_PROJECTILE = 1 << 4, // 抛射物
    LAYER_TRIGGER = 1 << 5,    // 触发器（压力板、按钮）
    LAYER_FLUID = 1 << 6,      // 流体（水、岩浆）

    // 常用组合
    ALL = 0xFFFFFFFF,
    SOLID = LAYER_STATIC | LAYER_DYNAMIC,
    LIVING = LAYER_ENTITY,
    ITEMS = LAYER_ITEM | LAYER_PROJECTILE,
};

// 碰撞掩码结构
struct CollisionMask {
    uint32_t belongsTo;  // 属于哪些层
    uint32_t collidesWith; // 与哪些层碰撞

    bool canCollideWith(uint32_t layer) const {
        return (collidesWith & layer) != 0;
    }
};