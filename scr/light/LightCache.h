#pragma once
#include "../core.h"
#include "../chunk/BlockType.h"
#include "LightSource.h"
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include "../chunk/ChunkDimensions.h"

// ── 单 section 光照缓存：16³ = 4096 格，每格 RGBA8 uint32 ──────────
// 由 Section 持有（通过 shared_ptr 共享，与 BlockBox 模式一致）。
//
// RGBA8 布局：
//   R(8): 红光 0–255
//   G(8): 绿光 0–255
//   B(8): 蓝光 0–255
//   A(8): 到最近光源的曼哈顿距离 0–255（0=光源格本身，255=无光照/未知）
//         增量更新时用此字段精确计算衰减，替代原来的估算公式。
//
// 兼容性：旧数据（A=255）通过 getDist() 返回 255（视为"距离未知"），
// 不影响现有 RGB 读取。writeRawData 从 Task 3 结果整块写入，已含距离。

class SectionLightCache {
public:
    static constexpr int WIDTH  = 16;
    static constexpr int HEIGHT = 16;
    static constexpr int DEPTH  = 16;
    static constexpr int CELLS  = WIDTH * HEIGHT * DEPTH;  // 4096
    static constexpr size_t BYTES = CELLS * sizeof(uint32_t); // 16384

    SectionLightCache() { m_data.fill(0); }

    // 写入 RGB + 距离（dist 默认 255 保持向后兼容）
    void set(int x, int y, int z, const glm::vec3& rgb, uint8_t dist = 255) {
        if (!inBounds(x, y, z)) return;
        m_data[index(x, y, z)] = pack(
            clampByte(rgb.r), clampByte(rgb.g), clampByte(rgb.b), dist);
        dirty = true;
    }

    // max-clamp 写入：仅当新 RGB 任意分量 > 缓存值时更新。
    // 距离取获胜光源的距离（即传入的 dist）。
    // 向后兼容：不传 dist 时保持旧距离值。
    void maxSet(int x, int y, int z, const glm::vec3& rgb, uint8_t dist = 255) {
        if (!inBounds(x, y, z)) return;
        glm::vec3 cur = get(x, y, z);
        // 逐分量比较：任一通道提升即视为更亮
        bool improved = (rgb.r > cur.r) || (rgb.g > cur.g) || (rgb.b > cur.b);
        if (!improved) return;
        // dist=255 表示未传距离 → 保留旧距离（向后兼容）
        if (dist == 255) dist = getDist(x, y, z);
        set(x, y, z, rgb, dist);
    }

    glm::vec3 get(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return glm::vec3(0.0f);
        uint32_t v = m_data[index(x, y, z)];
        return glm::vec3(float(v & 0xFFu) / 255.0f,
                          float((v >> 8) & 0xFFu) / 255.0f,
                          float((v >> 16) & 0xFFu) / 255.0f);
    }

    // 获取存储的源距离（alpha 通道）。0=光源格，255=无光照/未知。
    uint8_t getDist(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return 255;
        return uint8_t((m_data[index(x, y, z)] >> 24) & 0xFFu);
    }

    bool hasAnyLight() const { return m_hasLight; }
    void setHasLight(bool v) { m_hasLight = v; }

    bool dirty = true;

    const uint32_t* rawData() const { return m_data.data(); }
    size_t rawSize() const { return BYTES; }

    // 整块写入（Task 3 结果，数据已含距离编码）
    void writeRawData(const uint32_t* src) {
        std::memcpy(m_data.data(), src, BYTES);
        dirty = true;
    }

    void clear() {
        m_data.fill(0);
        m_hasLight = false;
        dirty = true;
    }

private:
    static constexpr int index(int x, int y, int z) {
        return (y * DEPTH + z) * WIDTH + x;
    }
    static bool inBounds(int x, int y, int z) {
        return (uint32_t)x < WIDTH && (uint32_t)y < HEIGHT && (uint32_t)z < DEPTH;
    }
    static uint8_t clampByte(float v) {
        int i = int(v * 255.0f + 0.5f);
        return uint8_t(i < 0 ? 0 : (i > 255 ? 255 : i));
    }
    static uint32_t pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }

    std::array<uint32_t, CELLS> m_data;
    bool m_hasLight = false;
};

// ── 共享光照数据类型 ──────────────────────────────────────────────

/// 单个 section 的光照数据（16³ = 4096 RGBA8 格，含距离）
using SectionLightData = std::array<uint32_t, SectionLightCache::CELLS>;

/// 一个 chunk 的 16 个 section 光照数据（shared_ptr 共享所有权）
using ChunkLightData = std::array<std::shared_ptr<SectionLightData>, ChunkConstants::SECTION_COUNT>;
