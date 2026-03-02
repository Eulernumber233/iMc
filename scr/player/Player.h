#pragma once
#include "../ecs/Components.h"
#include "../ecs/EntityManager.h"
#include "../collision/Ray.h"
#include "../chunk/ChunkManager.h"
#include <memory>

class Player {
private:
    entt::entity entity;
    std::shared_ptr<ChunkManager> chunkManager;

public:
    Player(std::shared_ptr<ChunkManager> manager);
    ~Player();

    void initialize(const glm::vec3& position);
    void update(float deltaTime);

    // 输入处理
    void processMovement(float deltaTime, bool forward, bool backward, bool left, bool right, bool jump);
    void processMouse(float deltaTime, float xoffset, float yoffset);

    // 方块交互
    void startBreaking();
    void stopBreaking();
    void placeBlock();

    // 射线检测
    Ray::HitResult raycast(float maxDistance);
    Ray::HitResult raycastFromScreen(float screenX, float screenY, int screenWidth, int screenHeight);

    // 获取组件
    Transform& getTransform();
    CameraComponent& getCamera();
    AABBCollider& getCollider();
    Physics& getPhysics();
    PlayerComponent& getPlayerComponent();
    SelectedBlock& getSelectedBlock();

    // 碰撞检测
    void checkCollisions();
    bool canMoveTo(const glm::vec3& position, const glm::vec3& direction);

private:
    void updateCameraVectors();
    void updateSelectedBlock();
    void handleBlockInteraction(float deltaTime);

    // 碰撞辅助函数
    //bool checkBlockCollision(const glm::vec3& position);
    std::vector<glm::ivec3> getCollidingBlocks(const AABBCollider& collider);
    //void resolveCollision(const glm::vec3& normal, float penetration);

    // 物理更新
    void updatePhysics(float deltaTime);
    void applyMovement(float deltaTime, const glm::vec3& moveInput);
};