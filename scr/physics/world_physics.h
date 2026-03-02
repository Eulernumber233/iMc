#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <array>
#include <optional>
#include "collider.h"
#include "../chunk/BlockType.h"

class ChunkManager; // 前向声明

// 体素世界物理查询
class WorldPhysics {
private:
    entt::registry& registry;
    ChunkManager& chunkManager;

    // 空间划分：将世界划分为16x16x16的格子
    struct SpatialCell {
        std::vector<entt::entity> entities;
        uint32_t timestamp = 0;  // 用于缓存验证
    };

    struct SpatialGrid {
        static constexpr int CELL_SIZE = 16;
        static constexpr int GRID_SIZE = 32;  // 覆盖512x512x512区域

        std::array<SpatialCell, GRID_SIZE* GRID_SIZE* GRID_SIZE> cells;
        uint32_t currentTimestamp = 0;

        // 世界坐标转网格坐标
        glm::ivec3 worldToGrid(const glm::vec3& pos) const {
            return glm::ivec3(
                std::floor(pos.x / CELL_SIZE) + GRID_SIZE / 2,
                std::floor(pos.y / CELL_SIZE) + GRID_SIZE / 2,
                std::floor(pos.z / CELL_SIZE) + GRID_SIZE / 2
            );
        }

        // 获取实体所在单元格
        SpatialCell& getCell(const glm::vec3& pos) {
            auto gridPos = worldToGrid(pos);
            int index = gridPos.x * GRID_SIZE * GRID_SIZE +
                gridPos.y * GRID_SIZE +
                gridPos.z;
            return cells[std::clamp(index, 0, (int)cells.size() - 1)];
        }

        // 更新实体位置
        void updateEntity(entt::entity entity, const glm::vec3& oldPos,
            const glm::vec3& newPos) {
            // 从旧单元格移除
            auto& oldCell = getCell(oldPos);
            oldCell.entities.erase(
                std::remove(oldCell.entities.begin(), oldCell.entities.end(), entity),
                oldCell.entities.end()
            );

            // 添加到新单元格
            auto& newCell = getCell(newPos);
            newCell.entities.push_back(entity);
            newCell.timestamp = currentTimestamp;
        }
    } spatialGrid;

public:
    WorldPhysics(entt::registry& reg, ChunkManager& cm)
        : registry(reg), chunkManager(cm) {
    }

    // 1. 方块查询（不实际创建碰撞体）
    struct VoxelQuery {
        struct BlockInfo {
            glm::ivec3 position;
            BlockType type;
            AABBCollider collider;
            CollisionMask mask;
        };

        // 获取一个AABB区域内的所有非空气方块
        std::vector<BlockInfo> queryBlocks(const AABBCollider& region) const {
            std::vector<BlockInfo> result;

            // 转换为整数方块坐标范围
            glm::ivec3 minBlock = glm::floor(region.min);
            glm::ivec3 maxBlock = glm::ceil(region.max);

            // 扩大一点范围，确保不会漏掉边界
            minBlock -= glm::ivec3(1);
            maxBlock += glm::ivec3(1);

            // 遍历所有可能包含方块的区块
            for (int x = minBlock.x; x <= maxBlock.x; ++x) {
                for (int z = minBlock.z; z <= maxBlock.z; ++z) {
                    // 获取区块
                    glm::ivec2 chunkPos(
                        std::floor(x / 16.0f),
                        std::floor(z / 16.0f)
                    );

                    // 从区块管理器获取区块（需要修改ChunkManager以支持查询）
                    // auto chunk = chunkManager->getChunk(chunkPos);
                    // if (!chunk) continue;

                    // 遍历Y轴
                    for (int y = minBlock.y; y <= maxBlock.y; ++y) {
                        if (y < 0 || y >= 256) continue; // 世界高度限制

                        // 获取方块类型（伪代码）
                        // BlockType type = chunk->getBlockLocal(
                        //     (x + 16) % 16,
                        //     y,
                        //     (z + 16) % 16
                        // );

                        // 这里简化：假设有一个函数可以直接获取方块类型
                        BlockType type = getBlockAtWorld(x, y, z);

                        if (type != BLOCK_AIR) {
                            BlockInfo info;
                            info.position = glm::ivec3(x, y, z);
                            info.type = type;
                            info.collider = {
                                glm::vec3(x, y, z),
                                glm::vec3(x + 1, y + 1, z + 1),
                                glm::vec3(0)
                            };
                            info.mask = getBlockCollisionMask(type);
                            result.push_back(info);
                        }
                    }
                }
            }

            return result;
        }

        // 获取单个方块的碰撞掩码
        CollisionMask getBlockCollisionMask(BlockType type) const {
            static const std::unordered_map<BlockType, CollisionMask> maskMap = {
                {BLOCK_AIR, {0, 0}},
                {BLOCK_STONE, {LAYER_STATIC, LAYER_ENTITY | LAYER_DYNAMIC | LAYER_ITEM}},
                {BLOCK_DIRT, {LAYER_STATIC, LAYER_ENTITY | LAYER_DYNAMIC}},
                {BLOCK_GRASS, {LAYER_STATIC, LAYER_ENTITY | LAYER_DYNAMIC}},
                {BLOCK_WATER, {LAYER_FLUID, LAYER_ENTITY | LAYER_ITEM | LAYER_PROJECTILE}},
                {BLOCK_WOOD, {LAYER_STATIC, LAYER_ENTITY | LAYER_DYNAMIC}},
                {BLOCK_LEAVES, {LAYER_DYNAMIC, LAYER_ENTITY}}, // 树叶可以穿过
            };

            auto it = maskMap.find(type);
            return it != maskMap.end() ? it->second : CollisionMask{ LAYER_STATIC, ALL };
        }

    private:
        // 从世界获取方块（需要接入你的区块系统）
        BlockType getBlockAtWorld(int x, int y, int z) const {
            // 这里需要调用你的区块管理器
            // 返回 BLOCK_AIR 表示没有方块
            return BLOCK_AIR;
        }
    } voxelQuery;

    // 2. 实体间碰撞查询
    class EntityQuery {
    private:
        entt::registry& registry;
        SpatialGrid& spatialGrid;

    public:
        EntityQuery(entt::registry& reg, SpatialGrid& grid)
            : registry(reg), spatialGrid(grid) {
        }

        // 查询一个区域内的所有实体
        template<typename Callback>
        void queryEntities(const AABBCollider& region, Callback&& callback) {
            // 获取区域覆盖的所有空间格子
            glm::ivec3 minGrid = spatialGrid.worldToGrid(region.min);
            glm::ivec3 maxGrid = spatialGrid.worldToGrid(region.max);

            for (int gx = minGrid.x; gx <= maxGrid.x; ++gx) {
                for (int gy = minGrid.y; gy <= maxGrid.y; ++gy) {
                    for (int gz = minGrid.z; gz <= maxGrid.z; ++gz) {
                        int index = gx * SpatialGrid::GRID_SIZE * SpatialGrid::GRID_SIZE +
                            gy * SpatialGrid::GRID_SIZE +
                            gz;

                        if (index >= 0 && index < spatialGrid.cells.size()) {
                            const auto& cell = spatialGrid.cells[index];

                            // 检查单元格时间戳（避免处理过时数据）
                            if (cell.timestamp == spatialGrid.currentTimestamp) {
                                for (auto entity : cell.entities) {
                                    if (registry.valid(entity) && registry.all_of<Collider>(entity)) {
                                        callback(entity);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // 射线检测实体
        struct RaycastHit {
            entt::entity entity;
            glm::vec3 point;
            glm::vec3 normal;
            float distance;
        };

        std::optional<RaycastHit> raycastEntities(const glm::vec3& origin,
            const glm::vec3& direction,
            float maxDistance,
            uint32_t layerMask) {
            std::optional<RaycastHit> closestHit;
            float closestDist = maxDistance;

            // 使用DDA算法遍历射线穿过的格子
            // 简化：遍历所有实体进行射线检测
            auto view = registry.view<Collider>();
            for (auto entity : view) {
                auto& collider = view.get<Collider>(entity);

                // 检查层掩码
                if (!collider.mask.canCollideWith(layerMask)) continue;

                // 获取实体的AABB
                auto transform = registry.try_get<Transform>(entity);
                if (!transform) continue;

                auto aabb = collider.getAABB(transform->getMatrix());

                // 简单AABB射线检测
                if (rayAABBIntersect(origin, direction, aabb, maxDistance)) {
                    // 计算精确交点（这里简化）
                    float t = 0.0f;
                    if (rayAABBIntersection(origin, direction, aabb, t) && t < closestDist) {
                        closestHit = RaycastHit{
                            entity,
                            origin + direction * t,
                            glm::vec3(0, 1, 0), // 简化法线
                            t
                        };
                        closestDist = t;
                    }
                }
            }

            return closestHit;
        }

    private:
        bool rayAABBIntersect(const glm::vec3& origin, const glm::vec3& dir,
            const AABBCollider& aabb, float maxDistance) {
            // 简化实现
            glm::vec3 invDir = 1.0f / dir;
            glm::vec3 t1 = (aabb.min - origin) * invDir;
            glm::vec3 t2 = (aabb.max - origin) * invDir;

            float tmin = std::max(std::max(std::min(t1.x, t2.x), std::min(t1.y, t2.y)), std::min(t1.z, t2.z));
            float tmax = std::min(std::min(std::max(t1.x, t2.x), std::max(t1.y, t2.y)), std::max(t1.z, t2.z));

            return tmax >= std::max(0.0f, tmin) && tmin <= maxDistance;
        }

        bool rayAABBIntersection(const glm::vec3& origin, const glm::vec3& dir,
            const AABBCollider& aabb, float& t) {
            // 更精确的相交测试
            t = 0.0f;
            float tmax = std::numeric_limits<float>::max();

            for (int i = 0; i < 3; ++i) {
                if (std::abs(dir[i]) < 1e-6) {
                    // 平行于轴
                    if (origin[i] < aabb.min[i] || origin[i] > aabb.max[i]) {
                        return false;
                    }
                }
                else {
                    float invDir = 1.0f / dir[i];
                    float t1 = (aabb.min[i] - origin[i]) * invDir;
                    float t2 = (aabb.max[i] - origin[i]) * invDir;

                    if (t1 > t2) std::swap(t1, t2);

                    t = std::max(t, t1);
                    tmax = std::min(tmax, t2);

                    if (t > tmax) return false;
                }
            }

            return true;
        }
    } entityQuery;

    // 3. 移动物体发起碰撞检测（你说得对！）
    struct MovementRequest {
        entt::entity entity;
        glm::vec3 desiredMovement;  // 期望的移动向量
        bool applyGravity = true;
        bool solveCollisions = true;

        // 结果
        glm::vec3 actualMovement;
        std::vector<entt::entity> collidedEntities;
        bool grounded = false;
    };

    // 处理单个实体的移动请求
    void processMovement(MovementRequest& request) {
        if (!registry.valid(request.entity)) return;

        auto* transform = registry.try_get<Transform>(request.entity);
        auto* collider = registry.try_get<Collider>(request.entity);
        auto* physics = registry.try_get<Physics>(request.entity);

        if (!transform || !collider) return;

        glm::vec3 position = transform->position;
        glm::vec3 velocity = request.desiredMovement;

        // 应用重力（如果需要）
        if (request.applyGravity && physics && physics->useGravity) {
            velocity.y += physics->gravityScale * -9.8f * 0.016f; // 假设60FPS
        }

        // 获取实体的碰撞体
        auto entityAABB = collider->getAABB(transform->getMatrix());

        // 分离轴测试：分别处理每个轴
        if (request.solveCollisions) {
            // X轴
            if (velocity.x != 0) {
                glm::vec3 testPos = position + glm::vec3(velocity.x, 0, 0);
                if (!canMoveTo(entityAABB, testPos, collider->mask)) {
                    velocity.x = 0;
                }
            }

            // Y轴（特殊处理：检测是否在地面）
            if (velocity.y != 0) {
                glm::vec3 testPos = position + glm::vec3(0, velocity.y, 0);
                if (!canMoveTo(entityAABB, testPos, collider->mask)) {
                    if (velocity.y < 0) {
                        request.grounded = true;
                    }
                    velocity.y = 0;
                }
            }

            // Z轴
            if (velocity.z != 0) {
                glm::vec3 testPos = position + glm::vec3(0, 0, velocity.z);
                if (!canMoveTo(entityAABB, testPos, collider->mask)) {
                    velocity.z = 0;
                }
            }

            // 角落情况：尝试对角移动
            if (velocity.x != 0 && velocity.z != 0) {
                glm::vec3 testPos = position + glm::vec3(velocity.x, 0, velocity.z);
                if (!canMoveTo(entityAABB, testPos, collider->mask)) {
                    // 先尝试X轴
                    glm::vec3 testPosX = position + glm::vec3(velocity.x, 0, 0);
                    if (canMoveTo(entityAABB, testPosX, collider->mask)) {
                        velocity.z = 0;
                    }
                    else {
                        // 再尝试Z轴
                        glm::vec3 testPosZ = position + glm::vec3(0, 0, velocity.z);
                        if (canMoveTo(entityAABB, testPosZ, collider->mask)) {
                            velocity.x = 0;
                        }
                        else {
                            velocity.x = velocity.z = 0;
                        }
                    }
                }
            }
        }

        // 更新位置
        request.actualMovement = velocity;
        transform->position += velocity;

        // 更新空间网格
        spatialGrid.updateEntity(request.entity, position, transform->position);
    }

private:
    // 检查实体是否可以移动到指定位置
    bool canMoveTo(const AABBCollider& entityAABB, const glm::vec3& newPos,
        const CollisionMask& mask) {
        // 1. 构建新位置的AABB
        AABBCollider testAABB = entityAABB;
        testAABB.min += newPos - glm::vec3(0.5f); // 假设实体中心在方块中心
        testAABB.max += newPos - glm::vec3(0.5f);

        // 2. 查询方块碰撞
        auto blocks = voxelQuery.queryBlocks(testAABB);
        for (const auto& block : blocks) {
            if (mask.canCollideWith(block.mask.belongsTo)) {
                if (testAABB.intersects(block.collider)) {
                    return false;
                }
            }
        }

        // 3. 查询实体碰撞
        std::vector<entt::entity> nearbyEntities;
        entityQuery.queryEntities(testAABB, [&](entt::entity other) {
            nearbyEntities.push_back(other);
            });

        for (auto other : nearbyEntities) {
            auto* otherCollider = registry.try_get<Collider>(other);
            auto* otherTransform = registry.try_get<Transform>(other);

            if (!otherCollider || !otherTransform) continue;

            // 检查层掩码
            if (!mask.canCollideWith(otherCollider->mask.belongsTo) ||
                !otherCollider->mask.canCollideWith(mask.belongsTo)) {
                continue;
            }

            // 获取其他实体的AABB
            auto otherAABB = otherCollider->getAABB(otherTransform->getMatrix());

            if (testAABB.intersects(otherAABB)) {
                return false;
            }
        }

        return true;
    }
};