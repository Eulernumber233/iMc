#include "LightPropagation.h"
#include "../chunk/ChunkManager.h"
#include <cmath>
#include <deque>
#include <cstring>

uint64_t LightPropagation::toSectionKey(const glm::ivec3& worldPos) {
    int chunkW = 16;
    int chunkD = 16;
    int secH   = 16;
    int chunkX = (int)std::floor((float)worldPos.x / (float)chunkW);
    int chunkZ = (int)std::floor((float)worldPos.z / (float)chunkD);
    int sectionY = worldPos.y / secH;
    if (worldPos.y < 0) sectionY--;
    return ChunkManager::makeSectionKey(chunkX, chunkZ, sectionY);
}

glm::ivec3 LightPropagation::toLocal(const glm::ivec3& worldPos) {
    int lx = ((worldPos.x % 16) + 16) % 16;
    int ly = ((worldPos.y % 16) + 16) % 16;
    int lz = ((worldPos.z % 16) + 16) % 16;
    return glm::ivec3(lx, ly, lz);
}

bool LightPropagation::blocksLight(BlockType type) {
    if (type == BLOCK_AIR || type == BLOCK_ERRER) return false;
    BlockProperties props = GetBlockProperties(type);
    return !props.isTransparent;
}

glm::vec3 LightPropagation::attenuate(const LightSource& src, int distance) {
    if (distance <= 0) return src.color * src.intensity;
    float d = (float)distance;
    float falloff = 1.0f - d / src.radius;
    if (falloff <= 0.0f) return glm::vec3(0.0f);
    return src.color * src.intensity * falloff;
}

void LightPropagation::clearSectionCaches(const glm::ivec3& regionMin,
                                           const glm::ivec3& regionMax,
                                           CacheGetter cacheGetter) {
    int secMinX = (int)std::floor((float)regionMin.x / 16.0f);
    int secMinY = regionMin.y / 16;
    int secMinZ = (int)std::floor((float)regionMin.z / 16.0f);
    int secMaxX = (int)std::floor((float)regionMax.x / 16.0f);
    int secMaxY = regionMax.y / 16;
    int secMaxZ = (int)std::floor((float)regionMax.z / 16.0f);

    for (int sx = secMinX; sx <= secMaxX; ++sx) {
        for (int sy = secMinY; sy <= secMaxY; ++sy) {
            for (int sz = secMinZ; sz <= secMaxZ; ++sz) {
                uint64_t key = ChunkManager::makeSectionKey(sx, sz, sy);
                SectionLightCache* c = cacheGetter(key);
                if (c) c->clear();
            }
        }
    }
}

// ---- Main propagation ----

void LightPropagation::propagateSingle(const LightSource& src,
                                        BlockQuery blockQuery,
                                        CacheGetter cacheGetter) {
    std::deque<QueueEntry> queue;

    // CRITICAL: Write light at the source position first.
    // For Block-type sources (glowstone), the source cell itself must
    // be illuminated so that faces of the block and faces of adjacent
    // air blocks looking into it both see the light.
    {
        uint64_t secKey = toSectionKey(src.pos);
        SectionLightCache* cache = cacheGetter(secKey);
        if (cache) {
            glm::ivec3 local = toLocal(src.pos);
            cache->maxSet(local.x, local.y, local.z, src.color * src.intensity);
            cache->setHasLight(true);
        }
    }

    // BFS: from source to surrounding cells
    queue.push_back({ src.pos, src.color * src.intensity, 0 });

    static const glm::ivec3 dirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 },
        { 0, 1, 0 }, { 0, -1, 0 },
        { 0, 0, 1 }, { 0, 0, -1 }
    };

    // Track visited cells to avoid redundant queue entries;
    // key = pack(worldPos) for dedup within this propagation.
    std::unordered_set<uint64_t> visited;
    auto packPos = [](const glm::ivec3& p) -> uint64_t {
        uint64_t x = (uint64_t)(uint32_t)p.x & 0xFFFFFFu;
        uint64_t y = (uint64_t)(uint32_t)(p.y & 0xFFF);
        uint64_t z = (uint64_t)(uint32_t)p.z & 0xFFFFFFu;
        return x | (y << 24) | (z << 36);
    };
    visited.insert(packPos(src.pos));

    while (!queue.empty()) {
        QueueEntry cur = queue.front();
        queue.pop_front();

        if (cur.dist >= (int)src.radius) continue;
        float maxComp = glm::max(cur.light.r, glm::max(cur.light.g, cur.light.b));
        if (maxComp < 0.005f) continue;

        int nextDist = cur.dist + 1;
        glm::vec3 nextLight = attenuate(src, nextDist);
        if (nextLight.r <= 0.0f && nextLight.g <= 0.0f && nextLight.b <= 0.0f)
            continue;

        for (const auto& dir : dirs) {
            glm::ivec3 nextPos = cur.pos + dir;

            // Skip already visited
            if (visited.count(packPos(nextPos))) continue;

            // Check: does the DESTINATION cell block light?
            BlockState ns = blockQuery(nextPos);
            if (blocksLight(ns.type())) {
                // The neighbor is opaque. Mark visited but don't
                // write light or continue BFS from it.
                visited.insert(packPos(nextPos));
                continue;
            }

            visited.insert(packPos(nextPos));

            // Write light to this cell
            uint64_t secKey = toSectionKey(nextPos);
            SectionLightCache* cache = cacheGetter(secKey);
            if (cache) {
                glm::ivec3 local = toLocal(nextPos);
                cache->maxSet(local.x, local.y, local.z, nextLight);
                cache->setHasLight(true);
            }

            // Continue propagation
            queue.push_back({ nextPos, nextLight, nextDist });
        }
    }
}

// ---- New: Propagate all sources ----

void LightPropagation::propagateAll(const LightSourceRegistry& registry,
                                     BlockQuery blockQuery,
                                     CacheGetter cacheGetter) {
    for (const auto& [key, src] : registry.all()) {
        if (src.intensity <= 0.0f) continue;
        propagateSingle(src, blockQuery, cacheGetter);
    }
}

// ---- Align world-coordinate bounding box to section boundaries ----
// clearSectionCaches works at section granularity (uses floor conversion),
// so we expand the region to section boundaries before clearing,
// ensuring queryAffecting and clearSectionCaches use identical ranges.
static void alignToSectionBounds(glm::ivec3& vmin, glm::ivec3& vmax) {
    // Use std::floor for negative coordinates: C++ integer division
    // truncates toward zero, but section boundaries need floor semantics
    // (consistent with clearSectionCaches).
    vmin.x = (int)std::floor((float)vmin.x / 16.0f) * 16;
    vmin.y = (int)std::floor((float)vmin.y / 16.0f) * 16;
    vmin.z = (int)std::floor((float)vmin.z / 16.0f) * 16;
    // vmax: round up to the next section boundary (end of current section)
    vmax.x = ((int)std::floor((float)vmax.x / 16.0f) + 1) * 16 - 1;
    vmax.y = ((int)std::floor((float)vmax.y / 16.0f) + 1) * 16 - 1;
    vmax.z = ((int)std::floor((float)vmax.z / 16.0f) + 1) * 16 - 1;
}

// ---- Incremental: clear region and re-propagate from affected sources ----

void LightPropagation::propagateRegion(const glm::ivec3& center, float maxRadius,
                                        const LightSourceRegistry& registry,
                                        BlockQuery blockQuery,
                                        CacheGetter cacheGetter) {
    // Clear only the affected region
    int r = (int)std::ceil(maxRadius);
    glm::ivec3 regionMin = center - glm::ivec3(r);
    glm::ivec3 regionMax = center + glm::ivec3(r);

    // Align to section boundaries so clearSectionCaches and queryAffecting agree
    glm::ivec3 alignedMin = regionMin;
    glm::ivec3 alignedMax = regionMax;
    alignToSectionBounds(alignedMin, alignedMax);

    clearSectionCaches(alignedMin, alignedMax, cacheGetter);

    // Find sources that could affect this region (using aligned bounds)
    auto affected = registry.queryAffecting(alignedMin, alignedMax);

    // Re-propagate from each affected source
    for (const auto& src : affected) {
        if (src.intensity <= 0.0f) continue;
        propagateSingle(src, blockQuery, cacheGetter);
    }
}

// ---- Incremental: remove a source and re-light its area ----

void LightPropagation::removeAndReLight(const glm::ivec3& oldPos, float oldRadius,
                                         const LightSourceRegistry& registry,
                                         BlockQuery blockQuery,
                                         CacheGetter cacheGetter) {
    // Clear the old source's affected area
    int r = (int)std::ceil(oldRadius);
    glm::ivec3 regionMin = oldPos - glm::ivec3(r);
    glm::ivec3 regionMax = oldPos + glm::ivec3(r);

    // Align to section boundaries
    glm::ivec3 alignedMin = regionMin;
    glm::ivec3 alignedMax = regionMax;
    alignToSectionBounds(alignedMin, alignedMax);

    clearSectionCaches(alignedMin, alignedMax, cacheGetter);

    // Find other sources that overlap this area (using aligned bounds)
    auto affected = registry.queryAffecting(alignedMin, alignedMax);

    // Re-propagate from each
    for (const auto& src : affected) {
        if (src.intensity <= 0.0f) continue;
        propagateSingle(src, blockQuery, cacheGetter);
    }
}
