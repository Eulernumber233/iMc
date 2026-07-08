#pragma once
#include "../core.h"
#include "LightSource.h"
#include "LightCache.h"
#include <glm/glm.hpp>
#include <functional>
#include <unordered_set>

class SectionLightCache;  // 前向声明（供 std::function 指针类型使用）

// ── 体素洪水填充光照传播 ──────────────────────────────────────────
// 从光源出发向 6 邻域 BFS，遇不透明方块停止。
// 衰减：线性 falloff light(d) = source * max(0, 1 - d/radius)。
// 多光源：逐分量取 max 叠加。
//
// 不再依赖 LightSourceRegistry（已移除）。
// 增量更新时调用方通过 SourceQuery 回调提供影响区域内的光源列表。

class LightPropagation {
public:
    using BlockQuery  = std::function<BlockState(const glm::ivec3&)>;
    using CacheGetter = std::function<SectionLightCache*(uint64_t)>;
    using SourceQuery = std::function<std::vector<glm::ivec3>(const glm::ivec3&, const glm::ivec3&)>;

    /// 从单个光源位置做 BFS（光源属性从该位置方块类型查表获取）
    static void propagateSingle(const glm::ivec3& srcPos,
                                BlockQuery blockQuery,
                                CacheGetter cacheGetter);

    /// 清空区域并重传播所有影响该区域的光源
    static void propagateRegion(const glm::ivec3& center, float maxRadius,
                                SourceQuery sourceQuery,
                                BlockQuery blockQuery,
                                CacheGetter cacheGetter);

    /// 全量传播（初始加载/传送）
    /// sources: 所有光源的世界坐标列表
    static void propagateAll(const std::vector<glm::ivec3>& sources,
                             BlockQuery blockQuery,
                             CacheGetter cacheGetter);

    /// 移除光源后重光照其区域
    static void removeAndReLight(const glm::ivec3& oldPos, float oldRadius,
                                 SourceQuery sourceQuery,
                                 BlockQuery blockQuery,
                                 CacheGetter cacheGetter);

private:
    static bool blocksLight(BlockType type);

    // 多光源合并 BFS：所有种子同时入队，共享 visited + queue，每格只访问一次。
    // 替代逐光源独立 BFS（O(N×volume) → O(volume)）。
    static void propagateMultiSource(const std::vector<glm::ivec3>& sources,
                                     BlockQuery blockQuery,
                                     CacheGetter cacheGetter);

    struct QueueEntry {
        glm::ivec3 pos;
        int        dist;
        float      srcRadius;
        glm::vec3  srcBrightness;  // color * intensity
    };

    static uint64_t toSectionKey(const glm::ivec3& worldPos);
    static glm::ivec3 toLocal(const glm::ivec3& worldPos);

    static void clearSectionCaches(const glm::ivec3& regionMin,
                                   const glm::ivec3& regionMax,
                                   CacheGetter cacheGetter);
};
