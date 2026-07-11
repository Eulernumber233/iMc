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

int LightPropagation::getLightOpacity(BlockType type) {
    // 不透明方块：完全阻挡
    if (blocksLight(type)) return 255;
    // 空气：无衰减
    if (type == BLOCK_AIR || type == BLOCK_ERRER) return 0;
    // 透明方块：额外距离衰减（模拟光在水中/树叶中更快衰减）
    switch (type) {
    case BLOCK_WATER: return 2;    // 水：每格等效 3 格空气（1 + 2）
    case BLOCK_LEAVES: return 1;   // 树叶：每格等效 2 格空气（1 + 1）
    default: return 0;             // 其他透明方块（如火把占位）按空气处理
    }
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

// ── 多光源合并 BFS（含距离元数据 + 透明方块衰减）────────────────────
// 设计要点：
//   - 所有光源同时入队，共享 visited + queue，每格只访问一次。
//   - 每格缓存包含 RGB + 曼哈顿距离（alpha 通道），供增量更新精确计算衰减。
//   - 透明非空气方块（水、树叶）增加等效距离惩罚，模拟光在水中/树叶中更快衰减。
//   - 必须使用 visited 数组：SectionLightCache 以 byte 精度存储，get() 返回
//     字节量化 float。原始 float 与字节量化 float 的 <= 比较存在精度偏差
//     （如 0.68 > 173/255≈0.67843），仅靠 max-clamp 会导致已写入格被误判为
//     "亮度提升"，重新入队 → 无限重入循环。visited 保证每格每源只处理一次。

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

        // 写入辅助：量化后比较 RGB；距离取传入的 dist。
        auto maxWrite = [&](const glm::ivec3& wp, const glm::vec3& light, int dist) -> bool {
            uint64_t secKey = toSectionKey(wp);
            SectionLightCache* cache = cacheGetter(secKey);
            if (!cache) return false;
            glm::ivec3 local = toLocal(wp);
            glm::vec3 cur = cache->get(local.x, local.y, local.z);
            float qr = std::round(light.r * 255.0f) / 255.0f;
            float qg = std::round(light.g * 255.0f) / 255.0f;
            float qb = std::round(light.b * 255.0f) / 255.0f;
            if (qr <= cur.r && qg <= cur.g && qb <= cur.b) return false;
            cache->maxSet(local.x, local.y, local.z, light, (uint8_t)dist);
            cache->setHasLight(true);
            return true;
        };

        // 光源格（距离 = 0）
        maxWrite(src, srcLight, 0);
        s_visited[vIdx(src.x, src.y, src.z)] = 1;

        s_ring.clear();
        s_ring.push_back(QueueEntry{ src, 0, srcRadius, srcLight });
        size_t head = 0;

        while (head < s_ring.size()) {
            QueueEntry cur = s_ring[head++];

            if (cur.dist >= (int)cur.srcRadius) continue;

            int nextDist = cur.dist + 1;

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

                // ── 透明方块光衰减 ──
                // 透明非空气方块（水、树叶）增加等效距离，使光在其中更快衰减
                int opacity = getLightOpacity(ns.type());
                int effectiveDist = nextDist + opacity;
                if (effectiveDist >= (int)cur.srcRadius) continue;  // 衰减后超出范围

                float falloff = 1.0f - (float)effectiveDist / cur.srcRadius;
                if (falloff <= 0.0f) continue;

                glm::vec3 nextLight = cur.srcBrightness * falloff;
                float maxComp = nextLight.r;
                if (nextLight.g > maxComp) maxComp = nextLight.g;
                if (nextLight.b > maxComp) maxComp = nextLight.b;
                if (maxComp < 0.005f) continue;

                // max-clamp 跨源混合：仅当本源的入射亮度提升了缓存值才继续传播
                if (!maxWrite(nextPos, nextLight, effectiveDist)) continue;
                // 入队时使用实际距离（非等效距离），保持 BFS 步数准确
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

// ── 增量：移除遮挡物后的去遮挡传播（精确重传播）─────────────────────
// 不再使用估算的 6 向追踪 + 虚拟光源 BFS。
// 改为清空受影响区域并重传播所有邻近光源，结果始终准确。

void LightPropagation::propagateDeocclusion(const glm::ivec3& openedPos,
                                             SourceQuery sourceQuery,
                                             BlockQuery blockQuery,
                                             CacheGetter cacheGetter) {
    // 以 openedPos 为中心，清空最大光源半径范围内的缓存，
    // 然后找出该范围内所有光源并重传播。
    // 这保证无论光源在哪个方向、有几个光源、距离多远，结果都准确。
    propagateRegion(openedPos, kMaxLightRadius,
                    sourceQuery, blockQuery, cacheGetter);
}

// ── 增量：新增遮挡物的阴影传播（精确重传播）─────────────────────────
// 不再使用基于邻居亮度的阴影半径估算。
// 改为清空受影响区域并重传播所有邻近光源，结果始终准确。

void LightPropagation::propagateOcclusion(const glm::ivec3& blockedPos,
                                           SourceQuery sourceQuery,
                                           BlockQuery blockQuery,
                                           CacheGetter cacheGetter) {
    // 以 blockedPos 为中心，清空最大光源半径范围内的缓存，
    // 然后找出该范围内所有光源并重传播。
    // BFS 自动正确处理新的遮挡关系——不透明方块在 BFS 中天然阻挡光。
    propagateRegion(blockedPos, kMaxLightRadius,
                    sourceQuery, blockQuery, cacheGetter);
}
