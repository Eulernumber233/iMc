#pragma once

// ============================================================================
// WorldOrigin: 动态浮动原点（Dynamic Floating Origin）
// ----------------------------------------------------------------------------
// 解决「绝对大坐标 → float32」转换边界上的精度丢失（见 docs/float-precision-analysis.md）。
//
// 核心思想：所有参与 float 渲染/物理运算的坐标，统一减去一个「跟随相机的浮动
// 原点」（相机所在区块的起点，16 的倍数），使参与 float 运算的值始终保持在
// ±400 内（精度 ≈ 0.05mm），彻底避免大 float 的 ULP 抖动。
//
// 坐标系分层：
//   - 绝对世界坐标  : glm::i64vec3（int64 方块坐标），权威表示，用于网络/存档/交互
//   - 相对坐标      : glm::vec3 / glm::ivec3（±400 内小值），渲染/物理/碰撞/剔除/光照
//   - 局部坐标      : int（0~15），Section 内方块
//
// 关键不变量：相对坐标计算永远「先减 origin 再乘/转 float」，热路径无需 long long。
// 仅主线程读写（worker 线程只碰整数 BlockBox，不读 origin），无需加锁。
// ============================================================================

#include <glm/glm.hpp>
#include <cstdint>

// 类型别名：统一坐标语义
using WorldBlockPos = glm::i64vec3;   // 绝对方块坐标（int64 分量）
using RelBlockPos   = glm::ivec3;     // 相对方块坐标（int32 小值）
using RelFloatPos   = glm::vec3;      // 相对浮点位置（±400 内）

// int64 向下取整除法（floor division）。负数输入也要向下取整（如 -1/16 = -1），
// C++ 整数除法向零截断不满足，须手写。用于把绝对方块坐标精确映射到 chunk/section。
inline int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

class WorldOrigin {
public:
    static WorldOrigin& instance() {
        static WorldOrigin inst;
        return inst;
    }

    // 用玩家绝对方块坐标更新原点（×16 对齐，取所在区块起点）。返回 true = 原点发生了变化。
    // 用 floorDiv 而非 (x/16)*16：后者对负坐标向零截断，玩家在 -1 方块时会错取 0 而非 -16。
    bool update(int64_t blockX, int64_t blockZ) {
        glm::i64vec3 next(
            floorDiv(blockX, (int64_t)16) * 16,
            0,
            floorDiv(blockZ, (int64_t)16) * 16);
        m_changed = (next != m_origin);
        if (m_changed) m_origin = next;
        return m_changed;
    }

    // 新会话重置（返回主菜单再开新世界时调用）
    void reset() { m_origin = glm::i64vec3(0); m_changed = false; }

    // 当前 origin（方块坐标，X/Z 为 16 的倍数，Y 恒 0）
    const glm::i64vec3& get() const { return m_origin; }

    // origin 的区块坐标（int32 小值，供 chunk 系相对计算）
    glm::ivec3 getChunk() const {
        return glm::ivec3((int)(m_origin.x / 16), 0, (int)(m_origin.z / 16));
    }

    // origin 的 float 表示（仅当 origin 距世界原点 < INT32_MAX 时安全；非关键路径用）
    glm::vec3 getVec3() const { return glm::vec3(m_origin); }

    bool changedThisFrame() const { return m_changed; }
    void endFrame() { m_changed = false; }

    // ── 绝对 → 相对 ──────────────────────────────────────────────────────
    RelBlockPos toRelative(const glm::i64vec3& abs) const {
        return RelBlockPos(abs - m_origin);   // 差在 ±400 内，int32 安全
    }
    RelFloatPos toRelativeF(const glm::i64vec3& abs, const glm::vec3& subOffset) const {
        return RelFloatPos(abs - m_origin) + subOffset;   // 小值，float 精确
    }
    // ── 相对 → 绝对 ──────────────────────────────────────────────────────
    glm::i64vec3 toAbsolute(const glm::ivec3& rel) const {
        return glm::i64vec3(rel) + m_origin;
    }

private:
    WorldOrigin() = default;
    glm::i64vec3 m_origin{0, 0, 0};
    bool m_changed = false;
};
