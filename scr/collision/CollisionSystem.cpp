#include "CollisionSystem.h"
#include <iostream>
#include "../chunk/Chunk.h"
void CollisionSystem::update(float deltaTime) {
    checkEntityBlockCollisions();
    checkEntityEntityCollisions();
}

void CollisionSystem::checkEntityBlockCollisions() {
    auto view = registry.view<Transform, AABBCollider, Physics>();

    for (auto entity : view) {
        auto& transform = view.get<Transform>(entity);
        auto& collider = view.get<AABBCollider>(entity);
        auto& physics = view.get<Physics>(entity);

        // 获取实体AABB
        AABBCollider entityAABB = collider.transformed(transform);

        // 重置地面状态
        physics.isGrounded = false;

        // 获取可能碰撞的方块
        auto potentialBlocks = getPotentialCollidingBlocks(entityAABB);

        for (const auto& blockPos : potentialBlocks) {
            // 检查方块是否存在且不是空气
            auto chunk = chunkManager->getChunkAtWorld(glm::vec3(blockPos));
            if (chunk) {
                glm::ivec3 localPos = chunk->getWorldPos(blockPos.x, blockPos.y, blockPos.z);
                BlockType type = chunk->getBlock(localPos.x, localPos.y, localPos.z);

                if (type != BLOCK_AIR) {
                    // 创建方块AABB
                    AABBCollider blockAABB{
                        glm::vec3(blockPos),
                        glm::vec3(blockPos) + glm::vec3(1.0f),
                        glm::vec3(0.0f)
                    };

                    if (entityAABB.intersects(blockAABB)) {
                        auto [penetration, normal] = entityAABB.getPenetration(blockAABB);

                        // 解决碰撞
                        resolveCollision(entity, normal, penetration);

                        // 更新地面状态
                        if (normal.y > 0.5f) {
                            physics.isGrounded = true;
                        }
                    }
                }
            }
        }
    }
}

void CollisionSystem::checkEntityEntityCollisions() {
    auto view = registry.view<Transform, AABBCollider>();
    std::vector<entt::entity> entities(view.begin(), view.end());

    for (size_t i = 0; i < entities.size(); ++i) {
        for (size_t j = i + 1; j < entities.size(); ++j) {
            auto entityA = entities[i];
            auto entityB = entities[j];

            auto& transformA = registry.get<Transform>(entityA);
            auto& colliderA = registry.get<AABBCollider>(entityA);
            auto* physicsA = registry.try_get<Physics>(entityA);

            auto& transformB = registry.get<Transform>(entityB);
            auto& colliderB = registry.get<AABBCollider>(entityB);
            auto* physicsB = registry.try_get<Physics>(entityB);

            // 获取AABB
            AABBCollider aabbA = colliderA.transformed(transformA);
            AABBCollider aabbB = colliderB.transformed(transformB);

            if (aabbA.intersects(aabbB)) {
                auto [penetration, normal] = aabbA.getPenetration(aabbB);

                // 解决碰撞（各推开一半）
                float push = penetration * 0.5f;
                transformA.position -= normal * push;
                transformB.position += normal * push;

                // 更新物理状态
                if (physicsA && physicsB) {
                    // 简单弹性碰撞响应
                    glm::vec3 relativeVelocity = physicsB->velocity - physicsA->velocity;
                    float velocityAlongNormal = glm::dot(relativeVelocity, normal);

                    if (velocityAlongNormal < 0) {
                        float restitution = 0.3f;
                        float j = -(1.0f + restitution) * velocityAlongNormal;
                        j /= (1.0f / physicsA->mass + 1.0f / physicsB->mass);

                        glm::vec3 impulse = normal * j;
                        physicsA->velocity -= impulse / physicsA->mass;
                        physicsB->velocity += impulse / physicsB->mass;
                    }
                }
            }
        }
    }
}

void CollisionSystem::resolveCollision(entt::entity entity, const glm::vec3& normal, float penetration) {
    auto* transform = registry.try_get<Transform>(entity);
    auto* physics = registry.try_get<Physics>(entity);

    if (!transform || !physics) return;

    // 推开实体
    transform->position += normal * penetration;

    // 更新速度
    float velocityAlongNormal = glm::dot(physics->velocity, normal);

    if (velocityAlongNormal < 0) {
        // 移除法线方向的速度
        physics->velocity -= normal * velocityAlongNormal;

        // 应用摩擦力（简化）
        if (std::abs(normal.y) < 0.1f) { // 水平碰撞
            physics->velocity *= 0.8f;
        }
    }
}

std::vector<glm::ivec3> CollisionSystem::getPotentialCollidingBlocks(const AABBCollider& collider) {
    std::vector<glm::ivec3> blocks;

    // 获取AABB覆盖的方块范围
    glm::ivec3 minBlock = glm::floor(collider.min);
    glm::ivec3 maxBlock = glm::ceil(collider.max);

    // 扩大一点范围，确保不会漏掉边界
    minBlock -= glm::ivec3(1);
    maxBlock += glm::ivec3(1);

    // 遍历所有可能包含方块的区块
    for (int x = minBlock.x; x <= maxBlock.x; ++x) {
        for (int z = minBlock.z; z <= maxBlock.z; ++z) {
            for (int y = minBlock.y; y <= maxBlock.y; ++y) {
                if (y < 0 || y >= 256) continue;

                blocks.push_back(glm::ivec3(x, y, z));
            }
        }
    }

    return blocks;
}

bool CollisionSystem::checkAABBBlockCollision(const AABBCollider& aabb, const glm::ivec3& blockPos) {
    AABBCollider blockAABB{
        glm::vec3(blockPos),
        glm::vec3(blockPos) + glm::vec3(1.0f),
        glm::vec3(0.0f)
    };

    return aabb.intersects(blockAABB);
}