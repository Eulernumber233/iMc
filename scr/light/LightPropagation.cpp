#include "LightPropagation.h"
#include "../chunk/ChunkManager.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <climits>

uint64_t LightPropagation::toSectionKey(const glm::ivec3& worldPos) {
    int chunkX = (int)std::floor((float)worldPos.x / 16.0f);
    int chunkZ = (int)std::floor((float)worldPos.z / 16.0f);
    int sectionY = worldPos.y / 16;
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
    return !GetBlockProperties(type).isTransparent;
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

// ── 多光源合并 BFS ───────────────────────────────────────────────────
// 所有光源同时入队，共享 visited + queue，每格只访问一次。
// 用 flat vector<uint8_t> 替代 unordered_set 做 visited 追踪（零哈希，缓存友好）。
// 与 propagateIntraChunkLights（ChunkWorkerPool.cpp）采用相同的合并策略。

void LightPropagation::propagateMultiSource(const std::vector<glm::ivec3>& sources,
                                             BlockQuery blockQuery,
                                             CacheGetter cacheGetter) {
    if (sources.empty()) return;

    // ── 1. 计算包围盒（所有光源的最大影响范围）──
    glm::ivec3 bmin(INT_MAX), bmax(INT_MIN);
    for (const auto& src : sources) {
        BlockState st = blockQuery(src);
        const LightDef& def = isEmissive(st.type())
            ? getLightDefForBlock(st.type()) : getTorchLightDef();
        if (def.intensity <= 0.0f) continue;
        int r = (int)std::ceil(def.radius);
        bmin = glm::min(bmin, src - r);
        bmax = glm::max(bmax, src + r);
    }
    if (bmin.x > bmax.x) return;  // 无有效光源

    glm::ivec3 size = bmax - bmin + 1;
    int totalCells = size.x * size.y * size.z;
    // 安全上限：若区域过大（不太可能，但防御）
    if (totalCells > 256 * 256 * 256) return;

    // ── 2. flat visited 数组（uint8_t 比 vector<bool> 快，无位操作开销）──
    std::vector<uint8_t> visited(totalCells, 0);
    auto visitIdx = [&](const glm::ivec3& wp) -> int {
        glm::ivec3 lp = wp - bmin;
        return (lp.z * size.y + lp.y) * size.x + lp.x;
    };

    // ── 3. 环形缓冲 BFS（预分配，零运行时堆分配）──
    static constexpr size_t kRingSize = 65536;
    static thread_local std::vector<QueueEntry> s_ring(kRingSize);
    size_t head = 0, tail = 0;
    auto push = [&](const QueueEntry& e) { s_ring[tail++ % kRingSize] = e; };
    auto pop  = [&]() -> QueueEntry { return s_ring[head++ % kRingSize]; };
    auto qempty = [&]() -> bool { return head == tail; };

    static const glm::ivec3 dirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    // ── 4. 写入光照辅助：只有提升了亮度才返回 true ──
    auto maxWrite = [&](const glm::ivec3& wp, const glm::vec3& light) -> bool {
        uint64_t secKey = toSectionKey(wp);
        SectionLightCache* cache = cacheGetter(secKey);
        if (!cache) return false;
        glm::ivec3 local = toLocal(wp);
        glm::vec3 cur = cache->get(local.x, local.y, local.z);
        if (light.r <= cur.r && light.g <= cur.g && light.b <= cur.b) return false;
        cache->maxSet(local.x, local.y, local.z, light);
        cache->setHasLight(true);
        return true;
    };

    // ── 5. 所有光源同时入队（种子）──
    for (const auto& src : sources) {
        BlockState st = blockQuery(src);
        const LightDef& def = isEmissive(st.type())
            ? getLightDefForBlock(st.type()) : getTorchLightDef();
        if (def.intensity <= 0.0f) continue;

        glm::vec3 srcLight = def.color * def.intensity;
        int idx = visitIdx(src);
        maxWrite(src, srcLight);
        visited[idx] = 1;
        push({ src, 0, def.radius, srcLight });
    }

    // ── 6. 单次 BFS ──
    while (!qempty()) {
        QueueEntry cur = pop();

        if (cur.dist >= (int)cur.srcRadius) continue;

        int nextDist = cur.dist + 1;
        float falloff = 1.0f - (float)nextDist / cur.srcRadius;
        if (falloff <= 0.0f) continue;

        glm::vec3 nextLight = cur.srcBrightness * falloff;
        float maxComp = nextLight.r;
        if (nextLight.g > maxComp) maxComp = nextLight.g;
        if (nextLight.b > maxComp) maxComp = nextLight.b;
        if (maxComp < 0.005f) continue;

        for (const auto& dir : dirs) {
            glm::ivec3 nextPos = cur.pos + dir;

            int idx = visitIdx(nextPos);
            if (visited[idx]) continue;

            BlockState ns = blockQuery(nextPos);
            if (blocksLight(ns.type())) {
                visited[idx] = 1;
                continue;
            }

            // 只有提升了亮度才继续传播（多源 max 混合剪枝）
            if (!maxWrite(nextPos, nextLight)) continue;

            visited[idx] = 1;
            push({ nextPos, nextDist, cur.srcRadius, cur.srcBrightness });
        }
    }
}

// ── 单光源 BFS（薄封装，内部委托 propagateMultiSource）───────────────

void LightPropagation::propagateSingle(const glm::ivec3& srcPos,
                                        BlockQuery blockQuery,
                                        CacheGetter cacheGetter) {
    propagateMultiSource({ srcPos }, blockQuery, cacheGetter);
}

// ── 全量传播 ───────────────────────────────────────────────────────

void LightPropagation::propagateAll(const std::vector<glm::ivec3>& sources,
                                     BlockQuery blockQuery,
                                     CacheGetter cacheGetter) {
    propagateMultiSource(sources, blockQuery, cacheGetter);
}

// ── 区域边界对齐 ───────────────────────────────────────────────────

static void alignToSectionBounds(glm::ivec3& vmin, glm::ivec3& vmax) {
    vmin.x = (int)std::floor((float)vmin.x / 16.0f) * 16;
    vmin.y = (int)std::floor((float)vmin.y / 16.0f) * 16;
    vmin.z = (int)std::floor((float)vmin.z / 16.0f) * 16;
    vmax.x = ((int)std::floor((float)vmax.x / 16.0f) + 1) * 16 - 1;
    vmax.y = ((int)std::floor((float)vmax.y / 16.0f) + 1) * 16 - 1;
    vmax.z = ((int)std::floor((float)vmax.z / 16.0f) + 1) * 16 - 1;
}

// ── 增量：清空区域并从受影响光源重传播 ───────────────────────────

void LightPropagation::propagateRegion(const glm::ivec3& center, float maxRadius,
                                        SourceQuery sourceQuery,
                                        BlockQuery blockQuery,
                                        CacheGetter cacheGetter) {
    int r = (int)std::ceil(maxRadius);
    glm::ivec3 regionMin = center - glm::ivec3(r);
    glm::ivec3 regionMax = center + glm::ivec3(r);

    glm::ivec3 alignedMin = regionMin;
    glm::ivec3 alignedMax = regionMax;
    alignToSectionBounds(alignedMin, alignedMax);

    clearSectionCaches(alignedMin, alignedMax, cacheGetter);

    auto affected = sourceQuery(alignedMin, alignedMax);

    // 合并 BFS：所有受影响光源一次遍历
    propagateMultiSource(affected, blockQuery, cacheGetter);
}

// ── 增量：移除光源并重光照其区域 ─────────────────────────────────

void LightPropagation::removeAndReLight(const glm::ivec3& oldPos, float oldRadius,
                                         SourceQuery sourceQuery,
                                         BlockQuery blockQuery,
                                         CacheGetter cacheGetter) {
    int r = (int)std::ceil(oldRadius);
    glm::ivec3 regionMin = oldPos - glm::ivec3(r);
    glm::ivec3 regionMax = oldPos + glm::ivec3(r);

    glm::ivec3 alignedMin = regionMin;
    glm::ivec3 alignedMax = regionMax;
    alignToSectionBounds(alignedMin, alignedMax);

    clearSectionCaches(alignedMin, alignedMax, cacheGetter);

    auto affected = sourceQuery(alignedMin, alignedMax);

    // 合并 BFS：所有受影响光源一次遍历
    propagateMultiSource(affected, blockQuery, cacheGetter);
}
