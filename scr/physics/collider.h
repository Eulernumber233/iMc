#pragma once
#include <glm/glm.hpp>
#include <variant>
#include <memory>
#include "physics_layers.h"

// 基础碰撞体类型
enum class ColliderType {
    NONE,
    AABB,      // 轴对齐包围盒（大多数方块和实体）
    SPHERE,    // 球体（抛射物、爆炸）
    CAPSULE,   // 胶囊体（角色控制器）
    MESH,      // 网格碰撞（复杂模型）
    COMPOUND,  // 复合碰撞体（多个简单碰撞体组合）
};

// AABB碰撞体（轻量级，不单独为每个方块存储）
struct AABBCollider {
    glm::vec3 min{ 0.0f };
    glm::vec3 max{ 1.0f };  // 默认1x1x1单位方块
    glm::vec3 offset{ 0.0f }; // 相对于实体中心的偏移

    // 获取变换后的AABB
    AABBCollider transform(const glm::mat4& matrix) const {
        glm::vec3 worldMin = glm::vec3(matrix * glm::vec4(min + offset, 1.0f));
        glm::vec3 worldMax = glm::vec3(matrix * glm::vec4(max + offset, 1.0f));
        return { worldMin, worldMax, glm::vec3(0) };
    }

    // 判断两个AABB是否相交
    bool intersects(const AABBCollider& other) const {
        return (min.x <= other.max.x && max.x >= other.min.x) &&
            (min.y <= other.max.y && max.y >= other.min.y) &&
            (min.z <= other.max.z && max.z >= other.min.z);
    }

    // 获取相交深度和法线（用于解决碰撞）
    std::pair<float, glm::vec3> getPenetration(const AABBCollider& other) const {
        glm::vec3 overlap(
            std::min(max.x, other.max.x) - std::max(min.x, other.min.x),
            std::min(max.y, other.max.y) - std::max(min.y, other.min.y),
            std::min(max.z, other.max.z) - std::max(min.z, other.min.z)
        );

        // 找到最小重叠的轴
        float minOverlap = std::min({ overlap.x, overlap.y, overlap.z });
        glm::vec3 normal(0.0f);

        if (minOverlap == overlap.x) {
            normal.x = (min.x < other.min.x) ? -1.0f : 1.0f;
        }
        else if (minOverlap == overlap.y) {
            normal.y = (min.y < other.min.y) ? -1.0f : 1.0f;
        }
        else {
            normal.z = (min.z < other.min.z) ? -1.0f : 1.0f;
        }

        return { minOverlap, glm::normalize(normal) };
    }
};

// 网格碰撞体引用（不存储实际顶点，只存引用）
struct MeshCollider {
    uint32_t meshId;  // 引用的网格ID
    glm::vec3 scale{ 1.0f };
    bool useSimplified = true;  // 是否使用简化碰撞网格
};

// 胶囊碰撞体（用于角色）
struct CapsuleCollider {
    float radius = 0.5f;
    float height = 1.8f;  // 标准玩家高度
    glm::vec3 offset{ 0.0f, height / 2.0f, 0.0f }; // 底部在y=0

    AABBCollider getAABB() const {
        return {
            glm::vec3(-radius, 0, -radius),
            glm::vec3(radius, height, radius),
            offset
        };
    }
};

// 复合碰撞体
struct CompoundCollider {
    struct SubCollider {
        AABBCollider aabb;
        glm::vec3 offset;
        CollisionMask mask;
    };

    std::vector<SubCollider> subColliders;

    // 获取总体AABB（用于粗略剔除）
    AABBCollider getOverallAABB() const {
        if (subColliders.empty()) return {};

        glm::vec3 overallMin = subColliders[0].aabb.min + subColliders[0].offset;
        glm::vec3 overallMax = subColliders[0].aabb.max + subColliders[0].offset;

        for (size_t i = 1; i < subColliders.size(); ++i) {
            const auto& collider = subColliders[i];
            overallMin = glm::min(overallMin, collider.aabb.min + collider.offset);
            overallMax = glm::max(overallMax, collider.aabb.max + collider.offset);
        }

        return { overallMin, overallMax, glm::vec3(0) };
    }
};

// 统一的碰撞体组件
struct Collider {
    ColliderType type = ColliderType::AABB;
    std::variant<AABBCollider, CapsuleCollider, MeshCollider, CompoundCollider> shape;
    CollisionMask mask{ LAYER_ENTITY, SOLID | LAYER_DYNAMIC };

    bool isTrigger = false;  // 是否是触发器（不阻挡运动）
    float friction = 0.6f;   // 摩擦力
    float restitution = 0.2f; // 弹性系数
    bool isStatic = false;   // 是否静态（位置不变）

    // 获取AABB（用于空间划分）
    AABBCollider getAABB(const glm::mat4& transform) const {
        return std::visit([&transform](auto&& arg) -> AABBCollider {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, AABBCollider>) {
                return arg.transform(transform);
            }
            else if constexpr (std::is_same_v<T, CapsuleCollider>) {
                auto aabb = arg.getAABB();
                return aabb.transform(transform);
            }
            else if constexpr (std::is_same_v<T, CompoundCollider>) {
                return arg.getOverallAABB().transform(transform);
            }
            else if constexpr (std::is_same_v<T, MeshCollider>) {
                // 网格碰撞体返回一个简化的AABB
                glm::vec3 scale = arg.scale;
                return AABBCollider{
                    glm::vec3(-0.5f) * scale,
                    glm::vec3(0.5f) * scale,
                    glm::vec3(0)
                }.transform(transform);
            }

            return {};
            }, shape);
    }
};