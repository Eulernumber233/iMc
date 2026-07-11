#pragma once
#include "../core.h"
#include <glm/glm.hpp>
#include "../chunk/BlockType.h"

// ── 光源属性静态表 ─────────────────────────────────────────────────
// 不再需要独立的 LightSource 结构体 / LightSourceRegistry 单例。
// 光照属性全部从方块类型 + 编译期静态表派生，遵循"唯一数据源"原则。

struct LightDef {
    glm::vec3 color;
    float     intensity;
    float     radius;
};

/// 按 BlockType 查发光属性（仅 emissive > 0 的方块有有效值）
inline LightDef getLightDefForBlock(BlockType type) {
    switch (type) {
    case BLOCK_GLOWSTONE:
        return { { 1.0f, 0.95f, 0.65f }, 1.0f, 15.0f };
    case BLOCK_TORCH:
        return { { 0.98f, 0.85f, 0.35f }, 0.8f, 14.0f };
    // 更多发光方块在此添加 case
    default:
        return { { 0.0f, 0.0f, 0.0f }, 0.0f, 0.0f };
    }
}

/// 火把光源（火把方块本身不发光，光从相邻空气格发出）
inline LightDef getTorchLightDef() {
    return { { 0.98f, 0.85f, 0.35f }, 0.8f, 14.0f };
}

/// 已知光源类型中最大传播半径（增量更新用）
constexpr float kMaxLightRadius = 16.0f;

/// 是否发光方块（有独立的 BlockType 条目）
inline bool isLightBlock(BlockType type) {
    return getLightDefForBlock(type).intensity > 0.0f;
}

/// 从方块类型直接判断是否发光
inline bool isEmissive(BlockType type) {
    return getLightDefForBlock(type).intensity > 0.0f;
}

// ── 压缩光源坐标（chunk 内 16-bit）────────────────────────────────
// 布局：bits 0-3: localX(0-15), bits 4-11: worldY(0-255), bits 12-15: localZ(0-15)
// 结合 chunk 原点可得完整世界坐标：world = (chunkX*16+lx, y, chunkZ*16+lz)

inline uint16_t packChunkLightPos(int lx, int worldY, int lz) {
    return (uint16_t)((lx & 0xF) | ((worldY & 0xFF) << 4) | ((lz & 0xF) << 12));
}
inline int unpackLightX(uint16_t p) { return p & 0xF; }
inline int unpackLightY(uint16_t p) { return (p >> 4) & 0xFF; }
inline int unpackLightZ(uint16_t p) { return (p >> 12) & 0xF; }
inline glm::ivec3 unpackLightWorld(uint16_t p, int chunkX, int chunkZ) {
    return glm::ivec3(chunkX * 16 + unpackLightX(p), unpackLightY(p), chunkZ * 16 + unpackLightZ(p));
}
