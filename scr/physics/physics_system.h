#pragma once
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "world_physics.h"

class PhysicsSystem {
private:
    entt::registry& registry;
    std::unique_ptr<WorldPhysics> worldPhysics;

    // 物理配置
    struct Config {
        float gravity = -9.8f;
        float airResistance = 0.01f;
        float maxVelocity = 100.0f;
        int velocityIterations = 8;  // 速度迭代次数
        int positionIterations = 3;  // 位置迭代次数
        bool enableSleeping = true;  // 是否启用休眠
    } config;

    // 物理组件
    struct Physics {
        glm::vec3 velocity{ 0.0f };
        glm::vec3 acceleration{ 0.0f };
        glm::vec3 force{ 0.0f };
        float mass = 1.0f;
        float drag = 0.0f;
        float angularDrag = 0.05f;
        bool useGravity = true;
        float gravityScale = 1.0f;
        bool isKinematic = false;  // 不受物理力影响
        bool isSleeping = false;   // 是否处于休眠状态
        float sleepTimer = 0.0f;

        void addForce(const glm::vec3& force) {
            this->force += force;
        }

        void addImpulse(const glm::vec3& impulse) {
            velocity += impulse / mass;
        }
    };

    // 运动体组件（只有这个才参与物理更新）
    struct Rigidbody {
        enum class Mode {
            STATIC,      // 完全不动
            KINEMATIC,   // 通过代码移动，但参与碰撞
            DYNAMIC      // 受物理力影响
        };

        Mode mode = Mode::DYNAMIC;
        bool continuousCollision = true;  // 连续碰撞检测
        bool freezeRotationX = false;
        bool freezeRotationY = false;
        bool freezeRotationZ = false;
        bool freezePositionX = false;
        bool freezePositionY = false;
        bool freezePositionZ = false;

        // 缓存上次位置，用于插值和连续碰撞
        glm::vec3 lastPosition{ 0.0f };
        glm::vec3 interpolatedPosition{ 0.0f };
    };

    // 接触信息
    struct Contact {
        entt::entity entityA;
        entt::entity entityB;
        glm::vec3 point;      // 接触点
        glm::vec3 normal;     // 从A指向B的法线
        float penetration;    // 穿透深度
        glm::vec3 relativeVelocity;

        struct Cache {
            glm::vec3 localPointA;
            glm::vec3 localPointB;
            float normalImpulse = 0.0f;
            float tangentImpulse = 0.0f;
        } cache;
    };

    // 等待处理的碰撞事件
    struct CollisionEvent {
        entt::entity entity;
        entt::entity other;
        glm::vec3 point;
        glm::vec3 normal;
        float impulse;
        bool enter;  // true=进入碰撞，false=离开碰撞
    };

    std::vector<CollisionEvent> collisionEvents;

public:
    PhysicsSystem(entt::registry& reg, ChunkManager* chunkManager)
        : registry(reg) {
        worldPhysics = std::make_unique<WorldPhysics>(registry, chunkManager);
        setupComponents();
    }

    void update(float deltaTime) {
        // 1. 唤醒休眠物体（如果附近有物体移动）
        wakeNearbyBodies(deltaTime);

        // 2. 收集所有运动体
        auto view = registry.view<Rigidbody, Transform, Physics, Collider>();
        std::vector<entt::entity> activeBodies;

        for (auto entity : view) {
            auto& rb = view.get<Rigidbody>(entity);
            auto& physics = view.get<Physics>(entity);

            // 跳过休眠物体
            if (config.enableSleeping && physics.isSleeping) {
                // 检查是否需要唤醒
                if (shouldWakeUp(entity)) {
                    physics.isSleeping = false;
                    physics.sleepTimer = 0.0f;
                }
                else {
                    continue;
                }
            }

            activeBodies.push_back(entity);
            rb.lastPosition = view.get<Transform>(entity).position;
        }

        // 3. 积分物理状态（应用力、计算速度）
        integrate(deltaTime, activeBodies);

        // 4. 碰撞检测和解决
        detectAndResolveCollisions(deltaTime, activeBodies);

        // 5. 更新位置
        updatePositions(deltaTime, activeBodies);

        // 6. 处理碰撞事件
        processCollisionEvents();

        // 7. 更新休眠状态
        updateSleepState(deltaTime, activeBodies);
    }

    // 投掷一个物品
    entt::entity spawnDroppedItem(glm::vec3 position, glm::vec3 velocity,
        uint32_t itemId, uint32_t quantity = 1) {
        auto entity = registry.create();

        // 变换
        registry.emplace<Transform>(entity, position);

        // 物理
        auto& physics = registry.emplace<Physics>(entity);
        physics.velocity = velocity;
        physics.mass = 0.5f;  // 物品较轻
        physics.useGravity = true;
        physics.drag = 0.1f;

        // 刚体
        auto& rb = registry.emplace<Rigidbody>(entity);
        rb.mode = Rigidbody::Mode::DYNAMIC;

        // 碰撞体（小方块）
        auto& collider = registry.emplace<Collider>(entity);
        collider.shape = AABBCollider{
            glm::vec3(-0.15f, 0, -0.15f),
            glm::vec3(0.15f, 0.15f, 0.15f),
            glm::vec3(0, 0.075f, 0)
        };
        collider.mask = { LAYER_ITEM, SOLID | LAYER_FLUID };
        collider.friction = 0.3f;
        collider.restitution = 0.4f;

        // 物品组件
        registry.emplace<ItemDrop>(entity, itemId, quantity);

        return entity;
    }

    // 简单的爆炸效果
    void createExplosion(glm::vec3 center, float radius, float force) {
        auto view = registry.view<Transform, Physics, Collider>();

        for (auto entity : view) {
            auto& transform = view.get<Transform>(entity);
            auto& physics = view.get<Physics>(entity);
            auto& collider = view.get<Collider>(entity);

            // 计算距离
            glm::vec3 delta = transform.position - center;
            float distance = glm::length(delta);

            if (distance < radius && distance > 0.1f) {
                // 爆炸力随距离衰减
                float strength = force * (1.0f - distance / radius);
                glm::vec3 direction = glm::normalize(delta);

                // 应用冲量
                physics.addImpulse(direction * strength);

                // 唤醒物体
                physics.isSleeping = false;
                physics.sleepTimer = 0.0f;
            }
        }
    }

private:
    void setupComponents() {
        // 注册组件序列化（如果需要）
    }

    void integrate(float deltaTime, const std::vector<entt::entity>& bodies) {
        for (auto entity : bodies) {
            auto& physics = registry.get<Physics>(entity);
            auto& transform = registry.get<Transform>(entity);

            if (physics.isKinematic) continue;

            // 应用重力
            if (physics.useGravity) {
                physics.acceleration.y += config.gravity * physics.gravityScale;
            }

            // 应用阻力
            if (physics.drag > 0) {
                physics.velocity *= (1.0f - physics.drag * deltaTime);
            }

            // 计算速度：v = v0 + a * dt
            physics.velocity += (physics.acceleration + physics.force / physics.mass) * deltaTime;

            // 限制最大速度
            float speed = glm::length(physics.velocity);
            if (speed > config.maxVelocity) {
                physics.velocity = physics.velocity / speed * config.maxVelocity;
            }

            // 重置力和加速度
            physics.acceleration = glm::vec3(0);
            physics.force = glm::vec3(0);
        }
    }

    void detectAndResolveCollisions(float deltaTime,
        const std::vector<entt::entity>& bodies) {
        std::vector<Contact> contacts;

        // 检测所有运动体之间的碰撞
        for (size_t i = 0; i < bodies.size(); ++i) {
            for (size_t j = i + 1; j < bodies.size(); ++j) {
                auto entityA = bodies[i];
                auto entityB = bodies[j];

                if (checkCollision(entityA, entityB, contacts)) {
                    // 存储接触信息
                }
            }

            // 检测运动体与静态世界的碰撞
            checkWorldCollision(bodies[i], contacts);
        }

        // 解决碰撞（迭代求解）
        solveContacts(contacts, deltaTime);
    }

    bool checkCollision(entt::entity a, entt::entity b,
        std::vector<Contact>& contacts) {
        auto* colliderA = registry.try_get<Collider>(a);
        auto* colliderB = registry.try_get<Collider>(b);
        auto* transformA = registry.try_get<Transform>(a);
        auto* transformB = registry.try_get<Transform>(b);

        if (!colliderA || !colliderB || !transformA || !transformB) {
            return false;
        }

        // 检查层掩码
        if (!colliderA->mask.canCollideWith(colliderB->mask.belongsTo) ||
            !colliderB->mask.canCollideWith(colliderA->mask.belongsTo)) {
            return false;
        }

        // 获取AABB
        auto aabbA = colliderA->getAABB(transformA->getMatrix());
        auto aabbB = colliderB->getAABB(transformB->getMatrix());

        // 检查相交
        if (aabbA.intersects(aabbB)) {
            // 计算穿透信息
            auto [penetration, normal] = aabbA.getPenetration(aabbB);

            // 创建接触信息
            Contact contact;
            contact.entityA = a;
            contact.entityB = b;
            contact.normal = normal;
            contact.penetration = penetration;

            // 计算接触点（中心点近似）
            contact.point = (aabbA.min + aabbA.max + aabbB.min + aabbB.max) * 0.25f;

            // 计算相对速度
            auto* physicsA = registry.try_get<Physics>(a);
            auto* physicsB = registry.try_get<Physics>(b);

            if (physicsA && physicsB) {
                contact.relativeVelocity = physicsB->velocity - physicsA->velocity;
            }

            contacts.push_back(contact);
            return true;
        }

        return false;
    }

    void checkWorldCollision(entt::entity entity, std::vector<Contact>& contacts) {
        auto* collider = registry.try_get<Collider>(entity);
        auto* transform = registry.try_get<Transform>(entity);

        if (!collider || !transform) return;

        // 获取实体AABB
        auto aabb = collider->getAABB(transform->getMatrix());

        // 查询周围的方块
        auto blocks = worldPhysics->voxelQuery.queryBlocks(aabb);

        for (const auto& block : blocks) {
            if (collider->mask.canCollideWith(block.mask.belongsTo)) {
                if (aabb.intersects(block.collider)) {
                    auto [penetration, normal] = aabb.getPenetration(block.collider);

                    Contact contact;
                    contact.entityA = entity;
                    contact.entityB = entt::null;  // 表示世界
                    contact.normal = normal;
                    contact.penetration = penetration;
                    contact.point = (aabb.min + aabb.max + block.collider.min + block.collider.max) * 0.25f;

                    contacts.push_back(contact);
                }
            }
        }
    }

    void solveContacts(std::vector<Contact>& contacts, float deltaTime) {
        // 简化的碰撞解决：直接推开
        for (auto& contact : contacts) {
            if (contact.entityB != entt::null) {
                // 实体间碰撞
                solveEntityContact(contact, deltaTime);
            }
            else {
                // 与世界碰撞
                solveWorldContact(contact, deltaTime);
            }
        }
    }

    void solveEntityContact(Contact& contact, float deltaTime) {
        auto* physicsA = registry.try_get<Physics>(contact.entityA);
        auto* transformA = registry.try_get<Transform>(contact.entityA);
        auto* physicsB = registry.try_get<Physics>(contact.entityB);
        auto* transformB = registry.try_get<Transform>(contact.entityB);

        if (!physicsA || !transformA || !physicsB || !transformB) return;

        // 简单推开：各推开一半
        float push = contact.penetration * 0.5f;

        transformA->position -= contact.normal * push;
        transformB->position += contact.normal * push;

        // 速度响应（简单弹性碰撞）
        float restitution = 0.3f;  // 简化
        float j = -(1.0f + restitution) * glm::dot(contact.relativeVelocity, contact.normal);
        j /= (1.0f / physicsA->mass + 1.0f / physicsB->mass);

        glm::vec3 impulse = contact.normal * j;

        physicsA->velocity -= impulse / physicsA->mass;
        physicsB->velocity += impulse / physicsB->mass;
    }

    void solveWorldContact(Contact& contact, float deltaTime) {
        auto* physics = registry.try_get<Physics>(contact.entityA);
        auto* transform = registry.try_get<Transform>(contact.entityA);

        if (!physics || !transform) return;

        // 推开实体
        transform->position += contact.normal * contact.penetration;

        // 速度响应（移除法线方向的速度）
        float normalVelocity = glm::dot(physics->velocity, contact.normal);
        if (normalVelocity < 0) {
            physics->velocity -= contact.normal * normalVelocity;

            // 应用摩擦力
            glm::vec3 tangentVelocity = physics->velocity - contact.normal * normalVelocity;
            physics->velocity -= tangentVelocity * 0.2f;  // 简化摩擦力
        }
    }

    void updatePositions(float deltaTime, const std::vector<entt::entity>& bodies) {
        for (auto entity : bodies) {
            auto& physics = registry.get<Physics>(entity);
            auto& transform = registry.get<Transform>(entity);
            auto& rb = registry.get<Rigidbody>(entity);

            // 只有动态刚体才通过速度移动
            if (rb.mode == Rigidbody::Mode::DYNAMIC) {
                transform.position += physics.velocity * deltaTime;
            }

            // 计算插值位置（用于平滑渲染）
            rb.interpolatedPosition = glm::mix(rb.lastPosition, transform.position, 0.5f);
        }
    }

    bool shouldWakeUp(entt::entity entity) {
        auto* transform = registry.try_get<Transform>(entity);
        if (!transform) return false;

        // 检查附近是否有移动的物体
        // 这里可以查询空间网格中附近的物体

        return false;  // 简化：总是返回false
    }

    void wakeNearbyBodies(float deltaTime) {
        // 遍历所有休眠物体，检查是否需要唤醒
        auto view = registry.view<Physics, Transform>();

        for (auto entity : view) {
            auto& physics = view.get<Physics>(entity);

            if (physics.isSleeping) {
                // 检查外部影响（如爆炸、玩家靠近等）
                // 这里可以检查事件队列
            }
        }
    }

    void updateSleepState(float deltaTime, const std::vector<entt::entity>& bodies) {
        if (!config.enableSleeping) return;

        for (auto entity : bodies) {
            auto& physics = registry.get<Physics>(entity);

            // 检查速度是否低于阈值
            float speed = glm::length(physics.velocity);
            if (speed < 0.1f) {
                physics.sleepTimer += deltaTime;

                if (physics.sleepTimer > 2.0f) {  // 2秒不动就休眠
                    physics.isSleeping = true;
                }
            }
            else {
                physics.sleepTimer = 0.0f;
            }
        }
    }

    void processCollisionEvents() {
        for (const auto& event : collisionEvents) {
            // 分发事件到ECS事件系统
            registry.ctx().get<entt::dispatcher>()
                .trigger<CollisionEvent>(event);
        }

        collisionEvents.clear();
    }
};