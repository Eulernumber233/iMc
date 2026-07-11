#include "LightPropagation.h"
#include "../chunk/ChunkManager.h"
#include <cmath>

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

// ── 逐光源 BFS（每源独立 visited 数组）──────────────────────────────
// 设计要点：
//   - 每个光源独立 BFS，写入共享 SectionLightCache。
//   - **必须使用 visited 数组**：SectionLightCache 以 byte 精度存储，get() 返回
//     字节量化 float。原始 float 与字节量化 float 的 <= 比较存在精度偏差
//     （如 0.68 > 173/255≈0.67843），仅靠 max-clamp 会导致已写入格被误判为
//     "亮度提升"，重新入队 → 无限重入循环 → 直到 1M 防御上限才截断，实测每次
//     约 5 秒。visited 保证每格每源只处理一次，彻底消除此问题。
//   - 写入比较时先将入射光量化为字节精度，再与缓存值比较，确保"无变化则停止"。
//   - 与 lightBuildOne（ChunkWorkerPool.cpp Task 3）采用相同的策略。

void LightPropagation::propagateMultiSource(const std::vector<glm::ivec3>& sources,
                                             BlockQuery blockQuery,
                                             CacheGetter cacheGetter) {
    if (sources.empty()) return;

    static const glm::ivec3 dirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    static thread_local std::vector<QueueEntry> s_ring;
    s_ring.reserve(65536);
    static thread_local std::vector<uint8_t> s_visited;

    for (const auto& src : sources) {
        BlockState st = blockQuery(src);
        const LightDef& def = isEmissive(st.type())
            ? getLightDefForBlock(st.type()) : getTorchLightDef();
        if (def.intensity <= 0.0f) continue;

        glm::vec3 srcLight = def.color * def.intensity;
        float srcRadius = def.radius;
        int srcR = (int)std::ceil(srcRadius);

        // ── 每源包围盒 + visited ──
        int bminX = src.x - srcR, bmaxX = src.x + srcR;
        int bminY = src.y - srcR, bmaxY = src.y + srcR;
        int bminZ = src.z - srcR, bmaxZ = src.z + srcR;
        int bw = bmaxX - bminX + 1, bh = bmaxY - bminY + 1, bd = bmaxZ - bminZ + 1;
        s_visited.assign((size_t)bw * bh * bd, 0);
        auto vIdx = [&](int wx, int wy, int wz) -> size_t {
            return (size_t)((wy - bminY) * bd + (wz - bminZ)) * bw + (wx - bminX);
        };

        // 写入辅助：将入射光量化到字节精度后再比较，消除 float↔byte 精度偏差
        auto maxWrite = [&](const glm::ivec3& wp, const glm::vec3& light) -> bool {
            uint64_t secKey = toSectionKey(wp);
            SectionLightCache* cache = cacheGetter(secKey);
            if (!cache) return false;
            glm::ivec3 local = toLocal(wp);
            glm::vec3 cur = cache->get(local.x, local.y, local.z);
            float qr = std::round(light.r * 255.0f) / 255.0f;
            float qg = std::round(light.g * 255.0f) / 255.0f;
            float qb = std::round(light.b * 255.0f) / 255.0f;
            if (qr <= cur.r && qg <= cur.g && qb <= cur.b) return false;
            cache->maxSet(local.x, local.y, local.z, light);
            cache->setHasLight(true);
            return true;
        };

        // 光源格
        maxWrite(src, srcLight);
        s_visited[vIdx(src.x, src.y, src.z)] = 1;

        s_ring.clear();
        s_ring.push_back(QueueEntry{ src, 0, srcRadius, srcLight });
        size_t head = 0;

        while (head < s_ring.size()) {
            QueueEntry cur = s_ring[head++];

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

                // 包围盒检查
                if (nextPos.x < bminX || nextPos.x > bmaxX) continue;
                if (nextPos.y < bminY || nextPos.y > bmaxY) continue;
                if (nextPos.z < bminZ || nextPos.z > bmaxZ) continue;

                size_t idx = vIdx(nextPos.x, nextPos.y, nextPos.z);
                if (s_visited[idx]) continue;

                BlockState ns = blockQuery(nextPos);
                if (blocksLight(ns.type())) {
                    s_visited[idx] = 1;  // 不透明块标 visited，不传播
                    continue;
                }

                s_visited[idx] = 1;
                // max-clamp 跨源混合：仅当本源的入射亮度提升了缓存值才继续传播
                if (!maxWrite(nextPos, nextLight)) continue;
                s_ring.push_back(QueueEntry{ nextPos, nextDist, srcRadius, cur.srcBrightness });
            }
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

// ── 增量：移除遮挡物后的去遮挡传播 ─────────────────────────────────
// 沿 6 方向追踪（最多 kMaxLightRadius 步），穿过透明无光格，
// 找到最近带光照的格。取最大光照作为虚拟光源向新打开的空间 BFS。

void LightPropagation::propagateDeocclusion(const glm::ivec3& openedPos,
                                             BlockQuery blockQuery,
                                             CacheGetter cacheGetter) {
    static const glm::ivec3 dirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    // 1. 沿 6 方向追踪：穿过透明无光格，找到最近带光照的透明格
    glm::vec3 maxLight(0.0f);
    constexpr int kMaxTrace = (int)kMaxLightRadius;

    for (const auto& d : dirs) {
        for (int dist = 1; dist <= kMaxTrace; ++dist) {
            glm::ivec3 np = openedPos + d * dist;
            BlockState ns = blockQuery(np);
            if (blocksLight(ns.type())) break; // 遇到不透明方块，此方向终止

            uint64_t sk = toSectionKey(np);
            SectionLightCache* cache = cacheGetter(sk);
            if (!cache) continue;
            glm::ivec3 loc = toLocal(np);
            glm::vec3 l = cache->get(loc.x, loc.y, loc.z);
            float comp = l.r;
            if (l.g > comp) comp = l.g;
            if (l.b > comp) comp = l.b;
            if (comp < 0.005f) continue; // 透明但无光，继续追踪
            // 找到光照，取 max 并停止本方向
            if (l.r > maxLight.r) maxLight.r = l.r;
            if (l.g > maxLight.g) maxLight.g = l.g;
            if (l.b > maxLight.b) maxLight.b = l.b;
            break;
        }
    }

    float maxComp = maxLight.r;
    if (maxLight.g > maxComp) maxComp = maxLight.g;
    if (maxLight.b > maxComp) maxComp = maxLight.b;
    if (maxComp < 0.005f) return; // 所有方向都没找到光源

    // 2. 估算虚拟光源半径（与取到的光照亮度成比例）
    float effectiveRadius = maxComp * kMaxLightRadius;
    if (effectiveRadius < 1.0f) effectiveRadius = 1.0f;
    int effR = (int)std::ceil(effectiveRadius);

    // ── 包围盒 + visited（消除 float↔byte 精度误差引起的重复入队循环）──
    int bminX = openedPos.x - effR, bmaxX = openedPos.x + effR;
    int bminY = openedPos.y - effR, bmaxY = openedPos.y + effR;
    int bminZ = openedPos.z - effR, bmaxZ = openedPos.z + effR;
    int bw = bmaxX - bminX + 1, bh = bmaxY - bminY + 1, bd = bmaxZ - bminZ + 1;

    static thread_local std::vector<QueueEntry> s_ring;
    s_ring.reserve(65536);
    static thread_local std::vector<uint8_t> s_visited;
    s_visited.assign((size_t)bw * bh * bd, 0);
    auto vIdx = [&](int wx, int wy, int wz) -> size_t {
        return (size_t)((wy - bminY) * bd + (wz - bminZ)) * bw + (wx - bminX);
    };

    // 写入辅助：量化后比较
    auto maxWrite = [&](const glm::ivec3& wp, const glm::vec3& light) -> bool {
        uint64_t secKey = toSectionKey(wp);
        SectionLightCache* cache = cacheGetter(secKey);
        if (!cache) return false;
        glm::ivec3 local = toLocal(wp);
        glm::vec3 cur = cache->get(local.x, local.y, local.z);
        float qr = std::round(light.r * 255.0f) / 255.0f;
        float qg = std::round(light.g * 255.0f) / 255.0f;
        float qb = std::round(light.b * 255.0f) / 255.0f;
        if (qr <= cur.r && qg <= cur.g && qb <= cur.b) return false;
        cache->maxSet(local.x, local.y, local.z, light);
        cache->setHasLight(true);
        return true;
    };

    // 3. BFS 从 openedPos 出发，将光照送入新打开的空间
    maxWrite(openedPos, maxLight);
    s_visited[vIdx(openedPos.x, openedPos.y, openedPos.z)] = 1;

    s_ring.clear();
    s_ring.push_back(QueueEntry{ openedPos, 0, effectiveRadius, maxLight });
    size_t head = 0;

    while (head < s_ring.size()) {
        QueueEntry cur = s_ring[head++];

        if (cur.dist >= (int)cur.srcRadius) continue;

        int nextDist = cur.dist + 1;
        float falloff = 1.0f - (float)nextDist / cur.srcRadius;
        if (falloff <= 0.0f) continue;

        glm::vec3 nextLight = cur.srcBrightness * falloff;
        float nc = nextLight.r;
        if (nextLight.g > nc) nc = nextLight.g;
        if (nextLight.b > nc) nc = nextLight.b;
        if (nc < 0.005f) continue;

        for (const auto& d : dirs) {
            glm::ivec3 np = cur.pos + d;

            if (np.x < bminX || np.x > bmaxX) continue;
            if (np.y < bminY || np.y > bmaxY) continue;
            if (np.z < bminZ || np.z > bmaxZ) continue;

            size_t idx = vIdx(np.x, np.y, np.z);
            if (s_visited[idx]) continue;

            BlockState ns = blockQuery(np);
            if (blocksLight(ns.type())) {
                s_visited[idx] = 1;
                continue;
            }

            s_visited[idx] = 1;
            if (!maxWrite(np, nextLight)) continue;
            s_ring.push_back(QueueEntry{ np, nextDist, cur.srcRadius, cur.srcBrightness });
        }
    }
}

// ── 增量：新增遮挡物的阴影传播 ─────────────────────────────────────
// 替代全量 clearSectionCaches + propagateMultiSource。
// 根据邻居光照亮度计算阴影半径，仅清空受影响小球体，只重传播邻近光源。

void LightPropagation::propagateOcclusion(const glm::ivec3& blockedPos,
                                           SourceQuery sourceQuery,
                                           BlockQuery blockQuery,
                                           CacheGetter cacheGetter) {
    static const glm::ivec3 dirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    // 1. 取 6 邻居的最大光照，决定阴影影响半径
    glm::vec3 maxNeighborLight(0.0f);
    for (const auto& d : dirs) {
        glm::ivec3 np = blockedPos + d;
        BlockState ns = blockQuery(np);
        if (blocksLight(ns.type())) continue;
        uint64_t sk = toSectionKey(np);
        SectionLightCache* cache = cacheGetter(sk);
        if (!cache) continue;
        glm::ivec3 loc = toLocal(np);
        glm::vec3 l = cache->get(loc.x, loc.y, loc.z);
        if (l.r > maxNeighborLight.r) maxNeighborLight.r = l.r;
        if (l.g > maxNeighborLight.g) maxNeighborLight.g = l.g;
        if (l.b > maxNeighborLight.b) maxNeighborLight.b = l.b;
    }

    float maxComp = maxNeighborLight.r;
    if (maxNeighborLight.g > maxComp) maxComp = maxNeighborLight.g;
    if (maxNeighborLight.b > maxComp) maxComp = maxNeighborLight.b;
    if (maxComp < 0.005f) return; // 周围无光，无阴影可投射

    // 2. 阴影半径 = 邻居亮度 × 最大光源半径（光照越亮 → 阴影越长）
    int shadowR = (int)std::ceil(maxComp * kMaxLightRadius);
    if (shadowR < 1) shadowR = 1;

    // 3. 清空小球体内的光照缓存
    glm::ivec3 sphereMin = blockedPos - glm::ivec3(shadowR);
    glm::ivec3 sphereMax = blockedPos + glm::ivec3(shadowR);
    glm::ivec3 alignedMin = sphereMin;
    glm::ivec3 alignedMax = sphereMax;
    alignToSectionBounds(alignedMin, alignedMax);
    clearSectionCaches(alignedMin, alignedMax, cacheGetter);

    // 4. 找出影响范围内的光源并重传播
    //    搜索范围比清空范围大一个光源半径，确保边界光源被包含
    int queryR = shadowR + (int)std::ceil(kMaxLightRadius);
    glm::ivec3 queryMin = blockedPos - glm::ivec3(queryR);
    glm::ivec3 queryMax = blockedPos + glm::ivec3(queryR);
    glm::ivec3 queryAlignedMin = queryMin;
    glm::ivec3 queryAlignedMax = queryMax;
    alignToSectionBounds(queryAlignedMin, queryAlignedMax);

    auto affected = sourceQuery(queryAlignedMin, queryAlignedMax);
    propagateMultiSource(affected, blockQuery, cacheGetter);
}
