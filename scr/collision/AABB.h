#pragma once
#include <glm/glm.hpp>

// 轴对齐包围盒 (Axis-Aligned Bounding Box)
struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    // 默认构造函数
    AABB() : min(0.0f), max(0.0f) {}

    // 通过最小点和最大点构造
    AABB(const glm::vec3& min_, const glm::vec3& max_)
        : min(min_), max(max_) {}

    // 通过中心点和半尺寸构造
    static AABB fromCenterHalfSize(const glm::vec3& center, const glm::vec3& halfSize) {
        return AABB(center - halfSize, center + halfSize);
    }

    // 通过中心点和完整尺寸构造
    static AABB fromCenterSize(const glm::vec3& center, const glm::vec3& size) {
        glm::vec3 halfSize = size * 0.5f;
        return AABB(center - halfSize, center + halfSize);
    }

    // 获取中心点
    glm::vec3 getCenter() const {
        return (min + max) * 0.5f;
    }

    // 获取尺寸
    glm::vec3 getSize() const {
        return max - min;
    }

    // 获取半尺寸
    glm::vec3 getHalfSize() const {
        return (max - min) * 0.5f;
    }

    // 检查是否与另一个AABB相交
    bool intersects(const AABB& other) const;

    // 检查点是否在AABB内
    bool contains(const glm::vec3& point) const {
        return (point.x >= min.x && point.x <= max.x) &&
               (point.y >= min.y && point.y <= max.y) &&
               (point.z >= min.z && point.z <= max.z);
    }

    // 扩展AABB以包含点
    void expandToInclude(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    // 扩展AABB以包含另一个AABB
    void expandToInclude(const AABB& other) {
        min = glm::min(min, other.min);
        max = glm::max(max, other.max);
    }

    // 获取AABB的体积
    float getVolume() const {
        glm::vec3 size = getSize();
        return size.x * size.y * size.z;
    }

    // 获取AABB的表面积
    float getSurfaceArea() const {
        glm::vec3 size = getSize();
        return 2.0f * (size.x * size.y + size.x * size.z + size.y * size.z);
    }

    // 移动AABB
    void translate(const glm::vec3& translation) {
        min += translation;
        max += translation;
    }

    // 获取平移后的AABB
    AABB translated(const glm::vec3& translation) const {
        return AABB(min + translation, max + translation);
    }

    // 检查AABB是否有效（min <= max）
    bool isValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }
};

// 碰撞结果结构
struct CollisionResult {
    bool collided = false;
    glm::vec3 normal = glm::vec3(0.0f);      // 碰撞法线，指向碰撞面外
    float penetration = 0.0f;                // 穿透深度
    glm::vec3 contactPoint = glm::vec3(0.0f); // 接触点（近似）
    glm::ivec3 blockPos = glm::ivec3(0);     // 碰撞的方块位置

    // 重置结果
    void reset() {
        collided = false;
        normal = glm::vec3(0.0f);
        penetration = 0.0f;
        contactPoint = glm::vec3(0.0f);
        blockPos = glm::ivec3(0);
    }

    // 检查是否是从上方碰撞（站在方块上）
    bool isFromTop() const {
        return normal.y > 0.5f; // Y轴正方向，且足够接近垂直向上
    }

    // 检查是否是从下方碰撞（头顶撞到方块）
    bool isFromBottom() const {
        return normal.y < -0.5f; // Y轴负方向
    }

    // 检查是否是水平方向碰撞
    bool isHorizontal() const {
        return glm::abs(normal.y) < 0.5f;
    }
};

// 计算两个AABB之间的碰撞细节
CollisionResult calculateAABBCollision(const AABB& moving, const AABB& stationary);

// 计算AABB与单位方块的碰撞
CollisionResult calculateBlockCollision(const AABB& entityAABB, const glm::ivec3& blockPos);

// 工具函数：获取方块的世界坐标AABB
inline AABB getBlockAABB(const glm::ivec3& blockPos) {
    glm::vec3 minPos(blockPos.x, blockPos.y, blockPos.z);
    glm::vec3 maxPos(blockPos.x + 1.0f, blockPos.y + 1.0f, blockPos.z + 1.0f);
    return AABB(minPos, maxPos);
}

// 工具函数：将世界坐标转换为方块坐标
inline glm::ivec3 worldToBlockCoord(const glm::vec3& worldPos) {
    return glm::ivec3(
        static_cast<int>(glm::floor(worldPos.x)),
        static_cast<int>(glm::floor(worldPos.y)),
        static_cast<int>(glm::floor(worldPos.z))
    );
}

// 工具函数：获取AABB覆盖的方块范围
inline void getBlocksInAABB(const AABB& aabb, glm::ivec3& minBlock, glm::ivec3& maxBlock) {
    minBlock = worldToBlockCoord(aabb.min);
    maxBlock = worldToBlockCoord(aabb.max);
}

// 工具函数：扩展方块范围（考虑碰撞箱可能稍微超出）
inline void expandBlockRange(const AABB& aabb, glm::ivec3& minBlock, glm::ivec3& maxBlock, int padding = 1) {
    getBlocksInAABB(aabb, minBlock, maxBlock);
    minBlock -= glm::ivec3(padding);
    maxBlock += glm::ivec3(padding);
}