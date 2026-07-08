#pragma once
#include "../core.h"
#include "../chunk/BlockType.h"
#include "LightSource.h"
#include <glm/glm.hpp>
#include <array>
#include <unordered_map>
#include <vector>
#include <cstring>

// Per-section light cache: 16^3 = 4096 cells, each packed as RGBA8 uint32.
// Lifecycle: managed by LightCacheManager, indexed by SectionKey.
class SectionLightCache {
public:
    static constexpr int WIDTH  = 16;
    static constexpr int HEIGHT = 16;
    static constexpr int DEPTH  = 16;
    static constexpr int CELLS  = WIDTH * HEIGHT * DEPTH;  // 4096

    SectionLightCache() { m_data.fill(0); }

    void set(int x, int y, int z, const glm::vec3& rgb) {
        if (!inBounds(x, y, z)) return;
        uint8_t r = clampByte(rgb.r);
        uint8_t g = clampByte(rgb.g);
        uint8_t b = clampByte(rgb.b);
        uint8_t a = 255;
        m_data[index(x, y, z)] = pack(r, g, b, a);
    }

    // Accumulate light (max of existing and new, per component)
    void maxSet(int x, int y, int z, const glm::vec3& rgb) {
        if (!inBounds(x, y, z)) return;
        glm::vec3 cur = get(x, y, z);
        glm::vec3 n(glm::max(cur.r, rgb.r),
                     glm::max(cur.g, rgb.g),
                     glm::max(cur.b, rgb.b));
        set(x, y, z, n);
    }

    glm::vec3 get(int x, int y, int z) const {
        if (!inBounds(x, y, z)) return glm::vec3(0.0f);
        uint32_t v = m_data[index(x, y, z)];
        return glm::vec3(float(v & 0xFFu) / 255.0f,
                          float((v >> 8) & 0xFFu) / 255.0f,
                          float((v >> 16) & 0xFFu) / 255.0f);
    }

    bool hasAnyLight() const { return m_hasLight; }
    void setHasLight(bool v) { m_hasLight = v; }

    // Marked true when GPU needs re-upload
    bool dirty = true;

    const uint32_t* rawData() const { return m_data.data(); }
    size_t rawSize() const { return CELLS * sizeof(uint32_t); }

    // Bulk write light data (used by main thread to merge Task 2 results)
    void writeRawData(const uint32_t* src) {
        std::memcpy(m_data.data(), src, CELLS * sizeof(uint32_t));
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

// Light cache manager: owns all section light caches, manages GPU SSBO.
// Held by ChunkManager, uploads dirty caches each frame before rendering.
class LightCacheManager {
public:
    LightCacheManager() = default;
    ~LightCacheManager();

    void initGL();
    void shutdown();

    SectionLightCache* getOrCreate(uint64_t sectionKey);
    const SectionLightCache* get(uint64_t sectionKey) const;
    SectionLightCache* getMutable(uint64_t sectionKey);

    void remove(uint64_t sectionKey);
    void clear();

    // Upload all dirty section caches to GPU SSBO
    void uploadToGPU(const glm::ivec3& cameraSectionMin,
                     const glm::ivec3& cameraSectionMax);

    // Bind SSBOs to the shader (binding=2 for light data, binding=3 for section map)
    void bindSSBOs() const;

    glm::ivec3 getCachedCameraSecMin() const { return m_cachedCamSecMin; }
    glm::ivec3 getCachedCameraSecRange() const {
        return m_cachedCamSecMax - m_cachedCamSecMin + 1;
    }

    size_t activeSectionCount() const { return m_caches.size(); }

    // Iterator support for clearing all caches before propagation
    auto begin() { return m_caches.begin(); }
    auto end()   { return m_caches.end(); }
    auto begin() const { return m_caches.begin(); }
    auto end()   const { return m_caches.end(); }

private:
    void rebuildSectionOffsets(const glm::ivec3& cameraSecMin,
                               const glm::ivec3& cameraSecMax);

    std::unordered_map<uint64_t, SectionLightCache> m_caches;

    GLuint m_lightSSBO      = 0;
    GLuint m_sectionMapSSBO = 0;

    GLsizeiptr m_lightSSBOSize      = 0;
    GLsizeiptr m_sectionMapSSBOSize = 0;

    glm::ivec3 m_cachedCamSecMin{0};
    glm::ivec3 m_cachedCamSecMax{0};
    bool m_initialized = false;
};

// ── 共享光照数据类型 ──────────────────────────────────────────────
// 与 SectionLightCache 内部 m_data 布局完全一致：4096 个 RGBA8 uint32_t，
// 索引 = (y * 16 + z) * 16 + x。用 shared_ptr 包裹，在多线程间零拷贝共享
//（模式与 BlockBox 的 shared_ptr<array<BlockState,4096>> 相同）。

#include <memory>

/// 单个 section 的光照数据（16³ = 4096 RGBA8 格）
using SectionLightData = std::array<uint32_t, SectionLightCache::CELLS>;

/// 一个 chunk 的 16 个 section 光照数据（shared_ptr 共享所有权）
using ChunkLightData = std::array<std::shared_ptr<SectionLightData>, 16>;
