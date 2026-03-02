#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include "../chunk/ChunkManager.h"
#include "../ecs/Components.h"

class CollisionSystem {
private:
    entt::registry& registry;
    std::shared_ptr<ChunkManager> chunkManager;

public:
    CollisionSystem(entt::registry& reg, std::shared_ptr<ChunkManager> manager)
        : registry(reg), chunkManager(manager) {
    }

    void update(float deltaTime);

private:
    // 检测实体与方块的碰撞
    void checkEntityBlockCollisions();

    // 检测实体之间的碰撞
    void checkEntityEntityCollisions();

    // 解决碰撞
    void resolveCollision(entt::entity entity, const glm::vec3& normal, float penetration);

    // 获取实体可能碰撞的方块
    std::vector<glm::ivec3> getPotentialCollidingBlocks(const AABBCollider& collider);

    // 检查AABB与方块的碰撞
    bool checkAABBBlockCollision(const AABBCollider& aabb, const glm::ivec3& blockPos);
};