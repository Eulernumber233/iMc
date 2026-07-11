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
// 每格光照缓存（SectionLightCache）同时存储 RGB 和到光源的曼哈顿距离
// （alpha 通道），使增量更新可以精确计算衰减而无需估算。

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

    /// 移除遮挡物后的去遮挡传播。
    /// 精确算法：清空受影响区域 + 重传播所有邻近光源，
    /// 替代了原来依赖估算的 6 向追踪 + 虚拟光源 BFS。
    static void propagateDeocclusion(const glm::ivec3& openedPos,
                                     SourceQuery sourceQuery,
                                     BlockQuery blockQuery,
                                     CacheGetter cacheGetter);

    /// 新增遮挡物（空气→不透明方块）的阴影传播。
    /// 精确算法：清空受影响区域 + 重传播所有邻近光源，
    /// 替代了原来基于邻居亮度估算阴影半径的近似方法。
    static void propagateOcclusion(const glm::ivec3& blockedPos,
                                   SourceQuery sourceQuery,
                                   BlockQuery blockQuery,
                                   CacheGetter cacheGetter);

    /// 判断方块是否阻挡光照传播
    static bool blocksLight(BlockType type);

    /// 获取方块的光衰减量（每格额外增加的等效距离）
    /// 返回 0（空气）、1–255（透明方块如水的衰减）、255（不透明方块）
    static int getLightOpacity(BlockType type);

private:

    // 多光源合并 BFS：所有种子同时入队，共享 visited + queue，每格只访问一次。
    // 每格缓存包含 RGB + 曼哈顿距离（alpha 通道）。
    static void propagateMultiSource(const std::vector<glm::ivec3>& sources,
                                     BlockQuery blockQuery,
                                     CacheGetter cacheGetter);

    struct QueueEntry {
        glm::ivec3 pos;
        int        dist;
        float      srcRadius;
        glm::vec3  srcBrightness;
    };

    static uint64_t toSectionKey(const glm::ivec3& worldPos);
    static glm::ivec3 toLocal(const glm::ivec3& worldPos);

    static void clearSectionCaches(const glm::ivec3& regionMin,
                                   const glm::ivec3& regionMax,
                                   CacheGetter cacheGetter);
};
