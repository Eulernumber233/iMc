#include "AABB.h"
#include <algorithm>
#include <cmath>

CollisionResult calculateAABBCollision(const AABB& moving, const AABB& stationary) {
    CollisionResult result;

    // 检查是否相交
    if (!moving.intersects(stationary)) {
        return result;
    }

    result.collided = true;

    // 计算在各个轴上的重叠深度
    glm::vec3 movingCenter = moving.getCenter();
    glm::vec3 stationaryCenter = stationary.getCenter();
    glm::vec3 movingHalfSize = moving.getHalfSize();
    glm::vec3 stationaryHalfSize = stationary.getHalfSize();

    // 计算中心点距离
    glm::vec3 delta = movingCenter - stationaryCenter;
    glm::vec3 totalHalfSize = movingHalfSize + stationaryHalfSize;

    // 计算在各个轴上的穿透深度
    glm::vec3 penetration;
    penetration.x = totalHalfSize.x - glm::abs(delta.x);
    penetration.y = totalHalfSize.y - glm::abs(delta.y);
    penetration.z = totalHalfSize.z - glm::abs(delta.z);

    // 找到最小穿透深度的轴（分离轴）
    // 使用一个小的偏置避免在角落处振荡
    const float bias = 0.001f;

    if (penetration.x < penetration.y && penetration.x < penetration.z) {
        // X轴穿透最小
        result.penetration = penetration.x + bias;
        result.normal = glm::vec3((delta.x > 0) ? 1.0f : -1.0f, 0.0f, 0.0f);
    } else if (penetration.y < penetration.z) {
        // Y轴穿透最小
        result.penetration = penetration.y + bias;
        result.normal = glm::vec3(0.0f, (delta.y > 0) ? 1.0f : -1.0f, 0.0f);
    } else {
        // Z轴穿透最小
        result.penetration = penetration.z + bias;
        result.normal = glm::vec3(0.0f, 0.0f, (delta.z > 0) ? 1.0f : -1.0f);
    }

    // 计算接触点（近似为移动AABB在碰撞方向上的表面点）
    glm::vec3 movingMin = moving.min;
    glm::vec3 movingMax = moving.max;

    if (result.normal.x > 0) {
        // 碰撞来自左侧，接触点在移动AABB的左侧面
        result.contactPoint = glm::vec3(movingMin.x, movingCenter.y, movingCenter.z);
    } else if (result.normal.x < 0) {
        // 碰撞来自右侧，接触点在移动AABB的右侧面
        result.contactPoint = glm::vec3(movingMax.x, movingCenter.y, movingCenter.z);
    } else if (result.normal.y > 0) {
        // 碰撞来自下方，接触点在移动AABB的底面
        result.contactPoint = glm::vec3(movingCenter.x, movingMin.y, movingCenter.z);
    } else if (result.normal.y < 0) {
        // 碰撞来自上方，接触点在移动AABB的顶面
        result.contactPoint = glm::vec3(movingCenter.x, movingMax.y, movingCenter.z);
    } else if (result.normal.z > 0) {
        // 碰撞来自前方，接触点在移动AABB的前面
        result.contactPoint = glm::vec3(movingCenter.x, movingCenter.y, movingMin.z);
    } else if (result.normal.z < 0) {
        // 碰撞来自后方，接触点在移动AABB的后面
        result.contactPoint = glm::vec3(movingCenter.x, movingCenter.y, movingMax.z);
    }

    return result;
}

CollisionResult calculateBlockCollision(const AABB& entityAABB, const glm::ivec3& blockPos) {
    AABB blockAABB = getBlockAABB(blockPos);
    CollisionResult result = calculateAABBCollision(entityAABB, blockAABB);
    result.blockPos = blockPos;
    return result;
}

// 分离轴定理的另一种实现（更精确）
bool AABB::intersects(const AABB& other) const {
    // 快速拒绝测试
    if (max.x < other.min.x || min.x > other.max.x) return false;
    if (max.y < other.min.y || min.y > other.max.y) return false;
    if (max.z < other.min.z || min.z > other.max.z) return false;
    return true;
}

// 扩展实现：获取AABB的8个顶点
void getAABBVertices(const AABB& aabb, glm::vec3 vertices[8]) {
    vertices[0] = glm::vec3(aabb.min.x, aabb.min.y, aabb.min.z);
    vertices[1] = glm::vec3(aabb.max.x, aabb.min.y, aabb.min.z);
    vertices[2] = glm::vec3(aabb.min.x, aabb.max.y, aabb.min.z);
    vertices[3] = glm::vec3(aabb.max.x, aabb.max.y, aabb.min.z);
    vertices[4] = glm::vec3(aabb.min.x, aabb.min.y, aabb.max.z);
    vertices[5] = glm::vec3(aabb.max.x, aabb.min.y, aabb.max.z);
    vertices[6] = glm::vec3(aabb.min.x, aabb.max.y, aabb.max.z);
    vertices[7] = glm::vec3(aabb.max.x, aabb.max.y, aabb.max.z);
}

// 扩展实现：射线与AABB相交测试
bool rayAABBIntersect(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                     const AABB& aabb, float& tMin, float& tMax) {
    glm::vec3 invDir = 1.0f / rayDir;
    glm::vec3 t1 = (aabb.min - rayOrigin) * invDir;
    glm::vec3 t2 = (aabb.max - rayOrigin) * invDir;

    glm::vec3 tMinVec = glm::min(t1, t2);
    glm::vec3 tMaxVec = glm::max(t1, t2);

    tMin = glm::max(glm::max(tMinVec.x, tMinVec.y), tMinVec.z);
    tMax = glm::min(glm::min(tMaxVec.x, tMaxVec.y), tMaxVec.z);

    return tMax >= tMin && tMax >= 0;
}