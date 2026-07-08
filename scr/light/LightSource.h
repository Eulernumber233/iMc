#pragma once
#include "../core.h"
#include "../chunk/BlockType.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

// Light source data structures
//   PointLight - torch/redstone torch, attached to block surface
//   BlockLight - glowstone, block itself emits light

enum class LightType : uint8_t {
    Point,   // torch type: attached to surface, 6-direction spread
    Block    // glowstone type: block self-illuminating
};

struct LightSource {
    glm::ivec3 pos;
    glm::vec3  color;
    float      intensity;
    float      radius;
    LightType  type;

    LightSource() = default;
    LightSource(glm::ivec3 p, glm::vec3 c, float i, float r, LightType t = LightType::Point)
        : pos(p), color(c), intensity(i), radius(r), type(t) {}
};

// Light source registry - manages all active light sources.
// Single-thread access (triggered by main thread block changes), no lock needed.
class LightSourceRegistry {
public:
    static LightSourceRegistry& instance() {
        static LightSourceRegistry inst;
        return inst;
    }

    void addLight(const glm::ivec3& pos, const LightSource& src);
    void removeLight(const glm::ivec3& pos);
    bool hasLight(const glm::ivec3& pos) const;

    // Remove all light sources within a given chunk (called on chunk unload)
    void removeLightsInChunk(int chunkX, int chunkZ);

    // Block change callback: detect if block is a light source and add/remove
    void onBlockChanged(const glm::ivec3& pos,
                        BlockState oldState, BlockState newState);

    // Generate torch light source at the air cell position
    static LightSource makeTorchLight(const glm::ivec3& torchAirPos,
                                       BlockType torchType);

    const std::unordered_map<int64_t, LightSource>& all() const { return m_lights; }

    std::vector<LightSource> queryAffecting(const glm::ivec3& regionMin,
                                             const glm::ivec3& regionMax) const;

    size_t count() const { return m_lights.size(); }
    void clear() { m_lights.clear(); }

    // Maximum propagation radius among all known light source types.
    // Used for incremental update regions.
    static constexpr float kMaxLightRadius = 16.0f;

private:
    LightSourceRegistry() = default;

    static int64_t posKey(const glm::ivec3& p) {
        int64_t x = (int64_t)(uint32_t)p.x & 0xFFFFFFLL;
        int64_t y = (int64_t)(uint32_t)(p.y & 0xFFF);
        int64_t z = (int64_t)(uint32_t)p.z & 0xFFFFFFLL;
        return x | (y << 24) | (z << 36);
    }

    std::unordered_map<int64_t, LightSource> m_lights;
};

// Block type -> light source definition
inline LightSource getBlockLightDef(BlockType type, const glm::ivec3& pos) {
    switch (type) {
    case BLOCK_GLOWSTONE:
        return LightSource(pos, glm::vec3(1.0f, 0.95f, 0.65f), 1.0f, 15.0f, LightType::Block);
    default:
        return LightSource(pos, glm::vec3(0.0f), 0.0f, 0.0f);
    }
}

inline bool isLightBlock(BlockType type) {
    switch (type) {
    case BLOCK_GLOWSTONE:
        return true;
    default:
        return false;
    }
}
