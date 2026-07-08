#pragma once
#include "../core.h"
#include "LightSource.h"
#include "LightCache.h"
#include <glm/glm.hpp>
#include <functional>
#include <unordered_set>

// Voxel flood-fill light propagation.
// Spreads from each light source to 6 neighbors, stopping at opaque blocks.
// Attenuation: linear falloff light(d) = source * max(0, 1 - d/radius).
// Multiple sources accumulate via max(RGB) per component.

class LightPropagation {
public:
    using BlockQuery  = std::function<BlockState(const glm::ivec3&)>;
    using CacheGetter = std::function<SectionLightCache*(uint64_t sectionKey)>;

    // Propagate from a single source (used for incremental updates)
    static void propagateSingle(const LightSource& src,
                                BlockQuery blockQuery,
                                CacheGetter cacheGetter);

    // Clear light in a region and re-propagate from all sources affecting it.
    // Used for incremental updates when a block changes (wall placed/removed,
    // light source placed/removed).
    static void propagateRegion(const glm::ivec3& center, float maxRadius,
                                const LightSourceRegistry& registry,
                                BlockQuery blockQuery,
                                CacheGetter cacheGetter);

    // Full clear + re-propagate (used for initial world load / camera teleport)
    static void propagateAll(const LightSourceRegistry& registry,
                             BlockQuery blockQuery,
                             CacheGetter cacheGetter);

    // Clear a single source and re-propagate its previous area from other sources
    static void removeAndReLight(const glm::ivec3& oldPos, float oldRadius,
                                 const LightSourceRegistry& registry,
                                 BlockQuery blockQuery,
                                 CacheGetter cacheGetter);

private:
    static glm::vec3 attenuate(const LightSource& src, int distance);
    static bool blocksLight(BlockType type);

    struct QueueEntry {
        glm::ivec3 pos;
        glm::vec3  light;  // light reaching this cell
        int        dist;   // Manhattan distance from source
    };

    static uint64_t toSectionKey(const glm::ivec3& worldPos);
    static glm::ivec3 toLocal(const glm::ivec3& worldPos);

    // Clear section caches in a world-space bounding box
    static void clearSectionCaches(const glm::ivec3& regionMin,
                                   const glm::ivec3& regionMax,
                                   CacheGetter cacheGetter);
};
