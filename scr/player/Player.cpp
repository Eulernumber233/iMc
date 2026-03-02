#include "Player.h"
#include "../collision/CollisionSystem.h"
#include <iostream>
#include "../chunk/Chunk.h"
Player::Player(std::shared_ptr<ChunkManager> manager)
    : chunkManager(manager) {
    entity = EntityManager::getInstance().createEntity();
}

Player::~Player() {
    EntityManager::getInstance().destroyEntity(entity);
}

void Player::initialize(const glm::vec3& position) {
    //static bool isInitialized = false;
    //     
    //if (isInitialized) {
    //    return; // 已初始化，直接返回，避免重复添加组件
    //}
    //isInitialized = true;
    auto& registry = EntityManager::getInstance().getRegistry();

    // 添加变换组件
    Transform transform;
    transform.position = position;
    transform.updateMatrix();
    registry.emplace<Transform>(entity, transform);

    // 添加摄像机组件
    CameraComponent camera;
    camera.front = glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f));
    camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    camera.fov = 45.0f;
    camera.aspectRatio = 16.0f / 9.0f;
    registry.emplace<CameraComponent>(entity, camera);

    // 添加碰撞体组件（玩家大小：0.6宽，1.8高）
    AABBCollider collider;
    collider.min = glm::vec3(-0.3f, 0.0f, -0.3f);
    collider.max = glm::vec3(0.3f, 1.8f, 0.3f);
    collider.offset = glm::vec3(0.0f, 0.0f, 0.0f);
    registry.emplace<AABBCollider>(entity, collider);

    // 添加物理组件
    Physics physics;
    physics.velocity = glm::vec3(0.0f);
    physics.mass = 80.0f; // 玩家体重
    physics.useGravity = true;
    physics.gravityScale = 1.0f;
    registry.emplace<Physics>(entity, physics);

    // 添加玩家组件
    PlayerComponent playerComp;
    playerComp.moveSpeed = 5.0f;
    playerComp.jumpForce = 8.0f;
    playerComp.interactionDistance = 8.0f;
    playerComp.breakCooldown = 0.2f;
    playerComp.placeCooldown = 0.2f;
    registry.emplace<PlayerComponent>(entity, playerComp);

    // 添加选中方块组件
    SelectedBlock selectedBlock;
    registry.emplace<SelectedBlock>(entity, selectedBlock);

    updateCameraVectors();
}

void Player::update(float deltaTime) {
    auto& physics = getPhysics();
    auto& playerComp = getPlayerComponent();

    // 更新物理
    updatePhysics(deltaTime);

    // 更新选中的方块
    updateSelectedBlock();

    // 处理方块交互
    handleBlockInteraction(deltaTime);

    // 更新变换矩阵
    auto& transform = getTransform();
    transform.updateMatrix();
}

void Player::processMovement(float deltaTime, bool forward, bool backward, bool left, bool right, bool jump) {
    auto& transform = getTransform();
    auto& camera = getCamera();
    auto& physics = getPhysics();
    auto& playerComp = getPlayerComponent();

    glm::vec3 moveDir(0.0f);

    if (forward) moveDir += camera.front;
    if (backward) moveDir -= camera.front;
    if (left) moveDir -= glm::normalize(glm::cross(camera.front, camera.up));
    if (right) moveDir += glm::normalize(glm::cross(camera.front, camera.up));

    // 处理跳跃
    if (jump && physics.isGrounded) {
        physics.velocity.y = playerComp.jumpForce;
        physics.isGrounded = false;
    }

    // 应用移动
    if (glm::length(moveDir) > 0.1f) {
        moveDir = glm::normalize(moveDir);
        moveDir.y = 0; // 保持水平移动

        applyMovement(deltaTime, moveDir);
    }
}

void Player::processMouse(float deltaTime, float xoffset, float yoffset) {
    auto& camera = getCamera();
    auto& transform = getTransform();

    static float yaw = -90.0f;
    static float pitch = 0.0f;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch -= yoffset;

    if (pitch > 89.0f) pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    // 更新摄像机方向
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    camera.front = glm::normalize(front);

    // 更新变换旋转
    transform.rotation.y = yaw;
    transform.rotation.x = -pitch;
}

void Player::updateCameraVectors() {
    auto& camera = getCamera();
    auto& transform = getTransform();

    glm::vec3 front;
    front.x = cos(glm::radians(transform.rotation.y)) * cos(glm::radians(transform.rotation.x));
    front.y = sin(glm::radians(transform.rotation.x));
    front.z = sin(glm::radians(transform.rotation.y)) * cos(glm::radians(transform.rotation.x));
    camera.front = glm::normalize(front);

    // 重新计算右向量和上向量
    camera.up = glm::normalize(glm::cross(glm::cross(camera.front, glm::vec3(0, 1, 0)), camera.front));
}

Ray::HitResult Player::raycast(float maxDistance) {
    auto& transform = getTransform();
    auto& camera = getCamera();
    auto& playerComp = getPlayerComponent();

    // 创建射线
    Ray ray(transform.position, camera.front);

    // 执行射线检测
    return ray.cast(chunkManager.get(), std::min(maxDistance, playerComp.interactionDistance));
}

Ray::HitResult Player::raycastFromScreen(float screenX, float screenY, int screenWidth, int screenHeight) {
    // 从屏幕坐标获取射线
    auto& transform = getTransform();
    auto& camera = getCamera();
    auto& playerComp = getPlayerComponent();

    // 将屏幕坐标转换为标准化设备坐标
    float ndcX = (2.0f * screenX) / screenWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * screenY) / screenHeight;

    // 获取投影和视图矩阵的逆矩阵
    glm::mat4 projection = camera.getProjectionMatrix();
    glm::mat4 view = camera.getViewMatrix(transform);
    glm::mat4 inverseProjView = glm::inverse(projection * view);

    // 创建在裁剪空间的射线端点
    glm::vec4 rayStartNDC(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 rayEndNDC(ndcX, ndcY, 0.0f, 1.0f);

    // 转换到世界空间
    glm::vec4 rayStartWorld = inverseProjView * rayStartNDC;
    rayStartWorld /= rayStartWorld.w;

    glm::vec4 rayEndWorld = inverseProjView * rayEndNDC;
    rayEndWorld /= rayEndWorld.w;

    // 计算射线方向
    glm::vec3 rayDir = glm::normalize(glm::vec3(rayEndWorld) - glm::vec3(rayStartWorld));

    // 创建射线并检测
    Ray ray(transform.position, rayDir);
    return ray.cast(chunkManager.get(), playerComp.interactionDistance);
}

void Player::updateSelectedBlock() {
    auto& selectedBlock = getSelectedBlock();
    auto& playerComp = getPlayerComponent();

    // 执行射线检测
    auto hit = raycast(playerComp.interactionDistance);

    if (hit.hit) {
        selectedBlock.position = hit.blockPos;
        selectedBlock.hitPoint = hit.hitPoint;
        selectedBlock.normal = hit.normal;
        selectedBlock.isSelected = true;
    }
    else {
        selectedBlock.isSelected = false;
    }
}

void Player::startBreaking() {
    auto& playerComp = getPlayerComponent();
    auto& selectedBlock = getSelectedBlock();

    if (!selectedBlock.isSelected) return;

    // 检查冷却时间
    float currentTime = static_cast<float>(glfwGetTime());
    if (currentTime - playerComp.lastBreakTime < playerComp.breakCooldown) {
        return;
    }

    // 开始破坏
    playerComp.breakProgress += 1.0f / playerComp.breakTime;

    if (playerComp.breakProgress >= 1.0f) {
        // 破坏完成
        chunkManager->setBlock(selectedBlock.position, BLOCK_AIR);
        playerComp.breakProgress = 0.0f;
        playerComp.lastBreakTime = currentTime;
    }
}

void Player::stopBreaking() {
    auto& playerComp = getPlayerComponent();
    playerComp.breakProgress = 0.0f;
}

void Player::placeBlock() {
    auto& playerComp = getPlayerComponent();
    auto& selectedBlock = getSelectedBlock();

    if (!selectedBlock.isSelected) return;

    // 检查冷却时间
    float currentTime = static_cast<float>(glfwGetTime());
    if (currentTime - playerComp.lastPlaceTime < playerComp.placeCooldown) {
        return;
    }

    // 计算放置位置（相邻位置）
    glm::ivec3 placePos = selectedBlock.position + glm::ivec3(selectedBlock.normal);

    // 检查放置位置是否可放置（不是玩家所在位置）
    auto& transform = getTransform();
    auto& collider = getCollider();

    AABBCollider playerAABB = collider.transformed(transform);
    AABBCollider blockAABB{
        glm::vec3(placePos),
        glm::vec3(placePos) + glm::vec3(1.0f),
        glm::vec3(0.0f)
    };

    if (!playerAABB.intersects(blockAABB)) {
        // 放置方块
        chunkManager->setBlock(placePos, BLOCK_STONE);
        playerComp.lastPlaceTime = currentTime;
    }
}

void Player::handleBlockInteraction(float deltaTime) {
    auto& playerComp = getPlayerComponent();

    // 如果破坏进度大于0但小于1，自动继续破坏
    if (playerComp.breakProgress > 0.0f && playerComp.breakProgress < 1.0f) {
        playerComp.breakProgress += deltaTime / playerComp.breakTime;

        if (playerComp.breakProgress >= 1.0f) {
            // 破坏完成
            auto& selectedBlock = getSelectedBlock();
            if (selectedBlock.isSelected) {
                chunkManager->setBlock(selectedBlock.position, BLOCK_AIR);
                playerComp.breakProgress = 0.0f;
                playerComp.lastBreakTime = static_cast<float>(glfwGetTime());
            }
        }
    }
}

void Player::checkCollisions() {
    auto& transform = getTransform();
    auto& collider = getCollider();
    auto& physics = getPhysics();

    // 获取玩家AABB
    AABBCollider playerAABB = collider.transformed(transform);

    // 检查地面碰撞
    physics.isGrounded = false;

    // 获取可能碰撞的方块
    auto collidingBlocks = getCollidingBlocks(playerAABB);

    for (const auto& blockPos : collidingBlocks) {
        AABBCollider blockAABB{
            glm::vec3(blockPos),
            glm::vec3(blockPos) + glm::vec3(1.0f),
            glm::vec3(0.0f)
        };

        if (playerAABB.intersects(blockAABB)) {
            auto [penetration, normal] = playerAABB.getPenetration(blockAABB);

            // 解决碰撞
            transform.position += normal * penetration;

            // 更新速度（防止穿透）
            if (normal.y > 0.5f) {
                // 地面碰撞
                physics.isGrounded = true;
                physics.velocity.y = std::max(physics.velocity.y, 0.0f);
            }
            else if (normal.y < -0.5f) {
                // 天花板碰撞
                physics.velocity.y = std::min(physics.velocity.y, 0.0f);
            }

            // 水平碰撞
            if (std::abs(normal.x) > 0.5f || std::abs(normal.z) > 0.5f) {
                physics.velocity.x *= (1.0f - std::abs(normal.x));
                physics.velocity.z *= (1.0f - std::abs(normal.z));
            }
        }
    }
}

std::vector<glm::ivec3> Player::getCollidingBlocks(const AABBCollider& collider) {
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
                if (y < 0 || y >= 256) continue; // 世界高度限制

                // 检查方块是否存在且不是空气
                auto chunk = chunkManager->getChunkAtWorld(glm::vec3(x, y, z));
                if (chunk) {
                    glm::ivec3 localPos = chunk->getWorldPos(x, y, z);
                    BlockType type = chunk->getBlock(localPos.x, localPos.y, localPos.z);

                    if (type != BLOCK_AIR) {
                        blocks.push_back(glm::ivec3(x, y, z));
                    }
                }
            }
        }
    }

    return blocks;
}

bool Player::canMoveTo(const glm::vec3& position, const glm::vec3& direction) {
    auto& collider = getCollider();

    // 创建测试AABB
    AABBCollider testAABB = collider;
    testAABB.min += position;
    testAABB.max += position;

    // 检查移动方向上的碰撞
    glm::vec3 testPos = position + direction * 0.1f; // 小步前进测试
    testAABB.min += direction * 0.1f;
    testAABB.max += direction * 0.1f;

    auto collidingBlocks = getCollidingBlocks(testAABB);

    for (const auto& blockPos : collidingBlocks) {
        AABBCollider blockAABB{
            glm::vec3(blockPos),
            glm::vec3(blockPos) + glm::vec3(1.0f),
            glm::vec3(0.0f)
        };

        if (testAABB.intersects(blockAABB)) {
            return false;
        }
    }

    return true;
}

void Player::updatePhysics(float deltaTime) {
    auto& physics = getPhysics();
    auto& transform = getTransform();

    // 应用重力
    if (physics.useGravity && !physics.isGrounded) {
        physics.velocity.y += -9.8f * physics.gravityScale * deltaTime;
    }

    // 限制最大下落速度
    physics.velocity.y = std::max(physics.velocity.y, -20.0f);

    // 应用空气阻力
    physics.velocity *= (1.0f - 0.1f * deltaTime);

    // 应用速度
    glm::vec3 oldPosition = transform.position;
    transform.position += physics.velocity * deltaTime;

    // 检查碰撞
    checkCollisions();
}

void Player::applyMovement(float deltaTime, const glm::vec3& moveInput) {
    auto& physics = getPhysics();
    auto& playerComp = getPlayerComponent();
    auto& transform = getTransform();

    // 计算移动速度
    glm::vec3 moveVelocity = moveInput * playerComp.moveSpeed;

    // 在地面时，可以立即改变水平速度
    if (physics.isGrounded) {
        physics.velocity.x = moveVelocity.x;
        physics.velocity.z = moveVelocity.z;
    }
    else {
        // 在空中时，逐渐改变水平速度
        physics.velocity.x = glm::mix(physics.velocity.x, moveVelocity.x, 0.1f);
        physics.velocity.z = glm::mix(physics.velocity.z, moveVelocity.z, 0.1f);
    }
}

// 获取组件的方法
Transform& Player::getTransform() {
    return EntityManager::getInstance().getComponent<Transform>(entity);
}

CameraComponent& Player::getCamera() {
    return EntityManager::getInstance().getComponent<CameraComponent>(entity);
}

AABBCollider& Player::getCollider() {
    return EntityManager::getInstance().getComponent<AABBCollider>(entity);
}

Physics& Player::getPhysics() {
    return EntityManager::getInstance().getComponent<Physics>(entity);
}

PlayerComponent& Player::getPlayerComponent() {
    return EntityManager::getInstance().getComponent<PlayerComponent>(entity);
}

SelectedBlock& Player::getSelectedBlock() {
    return EntityManager::getInstance().getComponent<SelectedBlock>(entity);
}