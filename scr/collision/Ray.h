// Ray.h 简化版
#pragma once
#include <glm/glm.hpp>
#include "../chunk/BlockType.h"
#include <memory>

class ChunkManager;

class Ray {
public:
    struct HitResult {
        bool hit = false;
        glm::ivec3 blockPos;      // 被击中方块的世界坐标（整数）
        glm::ivec3 adjacentPos;   // 相邻位置（用于放置方块）
        glm::vec3 hitPoint;       // 射线与方块的交点（精确）
        glm::vec3 normal;         // 被击中的面法线（从被击中方块指向外部）
        BlockFace face;           // 被击中的面
        float distance;           // 击中距离
        BlockType blockType;      // 方块类型
    };

    Ray(const glm::vec3& origin, const glm::vec3& direction);

    // 主要射线检测方法
    HitResult cast(ChunkManager* chunkManager, float maxDistance = 8.0f);

private:
    glm::vec3 m_origin;
    glm::vec3 m_direction;
    glm::vec3 m_invDirection;  // 方向分量的倒数，用于快速计算

    // DDA算法实现
    bool ddaCast(ChunkManager* chunkManager, HitResult& result, float maxDistance);

    // 获取方块在区块中的局部坐标
    static glm::ivec3 worldToLocal(const glm::ivec3& worldPos);
};