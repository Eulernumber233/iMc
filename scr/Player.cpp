#include "Player.h"
#include "chunk/ChunkManager.h"
#include "render/RenderSystem.h"
#include "UI/UIManager.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>

// ==================== 构造函数和初始化 ====================

Player::Player(std::shared_ptr<Camera> camera, GLFWwindow* window)
    : m_camera(camera), m_window(window) {
    // 初始化默认值
    m_lastX = SCR_WIDTH / 2.0f;
    m_lastY = SCR_HEIGHT / 2.0f;

    // 初始化玩家位置（使用摄像机位置作为起点）
    m_position = camera->Position;

    // 初始化物理变量（重要！避免未初始化导致随机值）
    m_velocity = glm::vec3(0.0f);
    m_acceleration = glm::vec3(0.0f);
    m_onGround = false;
    m_isCrouching = false;

    // 计算摄像机偏移
    // 站立时：摄像机在玩家碰撞箱顶部上方
    m_cameraOffsetStanding = glm::vec3(0.0f, m_collisionBox.standingHeight/2.0f +
        PhysicsConstants::CAMERA_OFFSET_Y_STANDING, 0.0f);
    // 蹲伏时：摄像机在碰撞箱内部
    m_cameraOffsetCrouching = glm::vec3(0.0f, m_collisionBox.crouchingHeight/2.0f +
        PhysicsConstants::CAMERA_OFFSET_Y_CROUCHING, 0.0f);

    // 初始摄像机位置
    updateCameraPosition();
}

void Player::initialize() {
    // 初始化物品栏
    initHotbarItems();

    // 创建UI热栏
    m_hotbar = std::make_shared<UIHotbar>("hotbar", 10);

    // 设置热栏位置（屏幕底部居中）
    float hotbarWidth = 500.0f;
    float hotbarHeight = 60.0f;
    float hotbarX = (SCR_WIDTH - hotbarWidth) / 2.0f;
    float hotbarY = 20.0f;

    m_hotbar->setPosition(hotbarX, hotbarY);
    m_hotbar->setSize(hotbarWidth, hotbarHeight);

    // 设置物品图标
    for (int i = 0; i < m_hotbarItems.size() && i < 10; ++i) {
        if (m_hotbarItems[i] == nullptr) {
            m_hotbar->setSlotItem(i, "");
        }
        else m_hotbar->setSlotItem(i, m_hotbarItems[i]->getIconTextureName());
    }
    // 注册到UI管理器
    UIManager::getInstance().addComponent(m_hotbar);
}

// ==================== 物理属性访问 ====================

void Player::setPosition(const glm::vec3& position) {
    m_position = position;
    updateCameraPosition();
}

AABB Player::getAABB() const {
    glm::vec3 halfSize(m_collisionBox.width/2.0f, m_collisionBox.height/2.0f, m_collisionBox.depth/2.0f);
    return AABB::fromCenterHalfSize(m_position, halfSize);
}

// ==================== 物理控制 ====================

void Player::jump() {
    // 允许在贴墙时跳跃（墙跳）
    // 条件：在地面或者速度很小（贴墙状态）
    bool canJump = false;

    if (m_onGround && !m_isCrouching) {
        canJump = true;
    } else if (!m_onGround && glm::abs(m_velocity.y) < 0.1f) {
        // 贴墙状态：垂直速度很小，可能贴着墙
        // 检查是否贴着墙（水平速度很小）
        glm::vec3 horizontalVel(m_velocity.x, 0.0f, m_velocity.z);
        if (glm::length(horizontalVel) < 0.5f) {
            canJump = true;
        }
    }

    if (canJump) {
        m_velocity.y = m_jumpForce;
        m_onGround = false;

        // 如果贴墙跳跃，给一个小的水平推力避免卡墙
        if (!m_onGround) {
            // 检查移动输入，给一个小的水平推力
            glm::vec3 wallJumpPush(0.0f);
            if (m_moveInput.forward) wallJumpPush += m_camera->Front;
            if (m_moveInput.backward) wallJumpPush -= m_camera->Front;
            if (m_moveInput.left) wallJumpPush -= m_camera->Right;
            if (m_moveInput.right) wallJumpPush += m_camera->Right;

            // 归一化并应用水平推力
            if (glm::length(wallJumpPush) > 0.0f) {
                wallJumpPush.y = 0.0f;
                wallJumpPush = glm::normalize(wallJumpPush);
                m_velocity.x += wallJumpPush.x * 2.0f; // 小的水平推力
                m_velocity.z += wallJumpPush.z * 2.0f;
            }
        }
    }
}

void Player::setCrouching(bool crouching) {
    if (m_isCrouching != crouching) {
        m_isCrouching = crouching;

        // 更新碰撞箱高度
        if (m_isCrouching) {
            m_collisionBox.height = m_collisionBox.crouchingHeight;
        } else {
            m_collisionBox.height = m_collisionBox.standingHeight;
        }

        updateCameraPosition();
    }
}

void Player::applyForce(const glm::vec3& force) {
    m_acceleration += force / m_mass;
}

void Player::setVelocity(const glm::vec3& velocity) {
    m_velocity = velocity;
}

// ==================== 碰撞检测核心算法 ====================

CollisionResult Player::checkCollisionWithBlocks(ChunkManager& chunkManager) const {
    AABB playerAABB = getAABB();

    // 获取玩家AABB覆盖的方块范围（扩展1个方块以处理边缘情况）
    glm::ivec3 minBlock, maxBlock;
    expandBlockRange(playerAABB, minBlock, maxBlock, 1);

    CollisionResult finalResult;
    std::vector<CollisionResult> collisions;

    // 遍历所有可能碰撞的方块
    for (int x = minBlock.x; x <= maxBlock.x; x++) {
        for (int y = minBlock.y; y <= maxBlock.y; y++) {
            for (int z = minBlock.z; z <= maxBlock.z; z++) {
                glm::ivec3 blockPos(x, y, z);
                BlockType blockType = chunkManager.getBlockAt(blockPos);

                // 跳过空气方块
                if (blockType == BLOCK_AIR) continue;

                // 计算碰撞
                CollisionResult collision = calculateBlockCollision(playerAABB, blockPos);
                if (collision.collided) {
                    collisions.push_back(collision);
                }
            }
        }
    }

    // 处理多个碰撞
    if (!collisions.empty()) {
        // 如果有多个碰撞，找到最重要的碰撞
        // 优先处理垂直碰撞（Y轴），然后是水平碰撞
        CollisionResult* verticalCollision = nullptr;
        CollisionResult* horizontalCollision = nullptr;

        for (auto& collision : collisions) {
            if (collision.isFromTop() || collision.isFromBottom()) {
                // 垂直碰撞
                if (!verticalCollision || collision.penetration > verticalCollision->penetration) {
                    verticalCollision = &collision;
                }
            } else {
                // 水平碰撞
                if (!horizontalCollision || collision.penetration > horizontalCollision->penetration) {
                    horizontalCollision = &collision;
                }
            }
        }

        // 优先返回垂直碰撞
        if (verticalCollision) {
            finalResult = *verticalCollision;
        } else if (horizontalCollision) {
            finalResult = *horizontalCollision;
        } else {
            // 返回第一个碰撞
            finalResult = collisions[0];
        }
    }

    return finalResult;
}

// ==================== 碰撞响应处理 ====================

void Player::resolveCollision(const CollisionResult& collision) {
    if (!collision.collided) return;

    // 位置修正：将玩家推出穿透深度
    m_position += collision.normal * collision.penetration;

    // 速度反射：根据碰撞法线反射速度
    // 如果速度方向与法线方向相反（朝向碰撞面），清零该方向速度
    float velocityDotNormal = glm::dot(m_velocity, collision.normal);
    if (velocityDotNormal < 0) {
        // 将速度投影到碰撞平面上
        m_velocity -= collision.normal * velocityDotNormal;

        // 添加能量损失（阻尼）
        float damping = 0.8f;
        m_velocity *= damping;
    }

    // 特殊处理：如果是从上方碰撞（站在方块上）
    if (collision.isFromTop()) {
        m_velocity.y = std::min(m_velocity.y, 0.0f); // 确保不向下穿透
        m_onGround = true;
    }

    // 特殊处理：如果是从下方碰撞（头顶撞到方块）
    if (collision.isFromBottom()) {
        m_velocity.y = std::max(m_velocity.y, 0.0f); // 确保不向上穿透
    }

    // 更新摄像机位置
    updateCameraPosition();
}

void Player::resolveMultipleCollisions(std::vector<CollisionResult>& collisions) {
    // 按穿透深度排序（从大到小）
    std::sort(collisions.begin(), collisions.end(),
        [](const CollisionResult& a, const CollisionResult& b) {
            return a.penetration > b.penetration;
        });

    // 依次处理所有碰撞
    for (const auto& collision : collisions) {
        resolveCollision(collision);
    }
}

// ==================== 物理更新 ====================

void Player::updateCameraPosition() {
    glm::vec3 cameraOffset = m_isCrouching ? m_cameraOffsetCrouching : m_cameraOffsetStanding;
    m_camera->Position = m_position + cameraOffset;
}

void Player::applyMovement(float deltaTime) {
    if (!m_onGround) {
        // 空中控制（受限）
        m_airControl = 0.2f;
    } else {
        m_airControl = 1.0f;
    }

    // 计算移动方向（基于摄像机方向）
    glm::vec3 moveDirection(0.0f);

    // 检查是否有移动输入
    bool hasForwardInput = m_moveInput.forward;
    bool hasBackwardInput = m_moveInput.backward;
    bool hasLeftInput = m_moveInput.left;
    bool hasRightInput = m_moveInput.right;

    // 计算输入方向
    if (hasForwardInput) moveDirection += m_camera->Front;
    if (hasBackwardInput) moveDirection -= m_camera->Front;
    if (hasLeftInput) moveDirection -= m_camera->Right;
    if (hasRightInput) moveDirection += m_camera->Right;

    // 归一化水平移动方向
    if (glm::length(moveDirection) > 0.0f) {
        moveDirection = glm::normalize(moveDirection);
        // 确保只在水平面移动
        moveDirection.y = 0.0f;
        moveDirection = glm::normalize(moveDirection);
    }

    // 计算目标速度
    float targetSpeed = m_walkSpeed;
    if (m_moveInput.sprint) targetSpeed = m_runSpeed;
    if (m_isCrouching) targetSpeed = m_crouchSpeed;

    // 应用速度倍数
    targetSpeed *= m_speedMultiplier;

    // 获取当前水平速度
    glm::vec3 horizontalVel(m_velocity.x, 0.0f, m_velocity.z);
    float currentHorizontalSpeed = glm::length(horizontalVel);

    // 检查是否有任何移动输入
    bool hasAnyMovementInput = hasForwardInput || hasBackwardInput || hasLeftInput || hasRightInput;

    if (hasAnyMovementInput) {
        // 有移动输入：计算目标速度
        glm::vec3 targetVelocity = moveDirection * targetSpeed;

        // 检查是否按下了反方向键
        // 如果当前有速度且输入方向与速度方向相反，立即停止并反向加速
        if (currentHorizontalSpeed > 0.1f) {
            glm::vec3 currentDir = glm::normalize(horizontalVel);
            float dotProduct = glm::dot(moveDirection, currentDir);

            // 如果方向相反（夹角大于90度），立即停止当前速度
            if (dotProduct < -0.5f) {
                // 立即停止当前速度，开始反向加速
                m_velocity.x = 0.0f;
                m_velocity.z = 0.0f;
                currentHorizontalSpeed = 0.0f;
            }
        }

        // 计算速度差并应用加速度
        glm::vec3 velocityDiff = targetVelocity - glm::vec3(m_velocity.x, 0.0f, m_velocity.z);

        // 使用加速度系数：8.0表示大约0.125秒内达到目标速度（快速响应）
        float accelerationFactor = 8.0f;
        glm::vec3 acceleration = velocityDiff * accelerationFactor * m_airControl;

        m_acceleration.x += acceleration.x;
        m_acceleration.z += acceleration.z;
    } else {
        // 没有移动输入：应用快速减速
        if (currentHorizontalSpeed > 0.0f) {
            // 快速减速：在0.1秒内停止
            float decelerationFactor = 10.0f; // 10倍重力加速度的减速
            glm::vec3 decelerationDir = -glm::normalize(horizontalVel);
            glm::vec3 deceleration = decelerationDir * decelerationFactor;

            // 确保减速不会导致反向速度
            float decelMagnitude = glm::length(deceleration) * deltaTime;
            if (decelMagnitude >= currentHorizontalSpeed) {
                // 如果减速会超过当前速度，直接停止
                m_velocity.x = 0.0f;
                m_velocity.z = 0.0f;
            } else {
                m_acceleration.x += deceleration.x;
                m_acceleration.z += deceleration.z;
            }
        }
    }

    // 处理跳跃输入
    if (m_moveInput.jump) {
        jump();
        m_moveInput.jump = false; // 单次触发
    }
}

void Player::applyGravity(float deltaTime) {
    // 总是应用重力，但在地面上时可能会有碰撞响应
    // 这样可以确保玩家从高处落下时能正确下落
    m_acceleration.y -= m_gravity;

    // 如果在地面上且垂直速度向下很小，抵消重力避免下沉
    if (m_onGround && m_velocity.y > -0.1f && m_velocity.y < 0.1f) {
        // 在地面上，抵消重力避免下沉
        m_acceleration.y += m_gravity;
    }
}

void Player::applyFriction(float deltaTime) {
    // 摩擦力作为加速度应用，而不是直接修改速度
    // 这样可以保持物理一致性
    float frictionAccel = m_onGround ? m_frictionGround : m_frictionAir;

    // 应用摩擦力到水平速度（与速度方向相反）
    glm::vec3 horizontalVel(m_velocity.x, 0.0f, m_velocity.z);
    float horizontalSpeed = glm::length(horizontalVel);

    if (horizontalSpeed > 0.0f) {
        // 摩擦力方向与速度方向相反
        glm::vec3 frictionDir = -glm::normalize(horizontalVel);
        glm::vec3 frictionAcceleration = frictionDir * frictionAccel;

        // 应用摩擦力加速度
        m_acceleration.x += frictionAcceleration.x;
        m_acceleration.z += frictionAcceleration.z;
    }
}

void Player::clampVelocity() {
    // 限制最大水平速度
    glm::vec3 horizontalVel(m_velocity.x, 0.0f, m_velocity.z);
    float horizontalSpeed = glm::length(horizontalVel);

    if (horizontalSpeed > PhysicsConstants::MAX_HORIZONTAL_SPEED) {
        float scale = PhysicsConstants::MAX_HORIZONTAL_SPEED / horizontalSpeed;
        m_velocity.x *= scale;
        m_velocity.z *= scale;
    }

    // 限制最大下落速度（防止下落过快）
    if (m_velocity.y < -PhysicsConstants::MAX_FALL_SPEED) {
        m_velocity.y = -PhysicsConstants::MAX_FALL_SPEED;
    }

    // 限制最大上升速度
    if (m_velocity.y > PhysicsConstants::MAX_VERTICAL_SPEED) {
        m_velocity.y = PhysicsConstants::MAX_VERTICAL_SPEED;
    }
}

void Player::updatePhysics(float deltaTime, ChunkManager& chunkManager) {
    // 限制deltaTime，避免物理计算错误
    if (deltaTime > 0.1f) {
        deltaTime = 0.1f;
    }

    // 调试：记录更新前的状态
    static int frameCount = 0;
    frameCount++;
    if (frameCount <= 3) {
        std::cout << "Frame " << frameCount << ": deltaTime=" << deltaTime
                  << ", pos=(" << m_position.x << "," << m_position.y << "," << m_position.z
                  << "), vel=(" << m_velocity.x << "," << m_velocity.y << "," << m_velocity.z
                  << "), acc=(" << m_acceleration.x << "," << m_acceleration.y << "," << m_acceleration.z
                  << "), onGround=" << m_onGround << std::endl;
    }

    // 在每次物理更新开始时，假设玩家不在空中
    // 只有在碰撞检测中确认站在地面上时才会设置为true
    m_onGround = false;

    // 重置加速度
    m_acceleration = glm::vec3(0.0f);

    // 应用移动输入
    applyMovement(deltaTime);

    // 应用重力
    applyGravity(deltaTime);

    // 计算新速度：v = v0 + a * dt
    m_velocity += m_acceleration * deltaTime;

    // 应用摩擦力
    applyFriction(deltaTime);

    // 限制速度
    clampVelocity();

    // 计算期望位置
    glm::vec3 desiredPosition = m_position + m_velocity * deltaTime;

    // 保存旧位置
    glm::vec3 oldPosition = m_position;

    // 先尝试移动到期望位置
    m_position = desiredPosition;

    // 检测碰撞
    CollisionResult collision = checkCollisionWithBlocks(chunkManager);

    // 处理碰撞
    if (collision.collided) {
        resolveCollision(collision);

        // 如果碰撞后仍然有速度朝向碰撞面，尝试再次解决
        // 这可以处理多个连续碰撞的情况
        int maxIterations = 3;
        for (int i = 0; i < maxIterations; i++) {
            collision = checkCollisionWithBlocks(chunkManager);
            if (!collision.collided) break;
            resolveCollision(collision);
        }
    }

    // 更新摄像机位置
    updateCameraPosition();
}

// ==================== 主更新循环 ====================

void Player::update(float deltaTime, ChunkManager& chunkManager, RenderSystem& renderSystem) {
    // 处理移动输入（更新m_moveInput状态）
    handleMovementInput(deltaTime);

    // 更新速度倍数
    updateSpeedMultiplier(deltaTime);

    // 更新物理状态
    updatePhysics(deltaTime, chunkManager);

    // 更新方块选择
    updateBlockSelection(chunkManager, renderSystem);

    // 处理方块交互
    handleBlockInteraction(chunkManager);
}

// ==================== 输入处理 ====================

void Player::processMouseMovement(float xoffset, float yoffset) {
    if (m_firstMouse) {
        m_lastX = static_cast<float>(xoffset);
        m_lastY = static_cast<float>(yoffset);
        m_firstMouse = false;
        return;
    }

    float xoffsetActual = static_cast<float>(xoffset) - m_lastX;
    float yoffsetActual = m_lastY - static_cast<float>(yoffset); // 反转Y轴

    m_lastX = static_cast<float>(xoffset);
    m_lastY = static_cast<float>(yoffset);

    m_camera->Yaw += xoffsetActual * m_camera->MouseSensitivity;
    m_camera->Pitch += yoffsetActual * m_camera->MouseSensitivity;

    if (m_camera->Pitch > 89.0f) m_camera->Pitch = 89.0f;
    if (m_camera->Pitch < -89.0f) m_camera->Pitch = -89.0f;

    // 更新摄像机向量
    m_camera->UpdateCameraVectors();
}

void Player::processMouseButton(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        m_leftMousePressed = (action == GLFW_PRESS);
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        m_rightMousePressed = (action == GLFW_PRESS);
    }
}

void Player::processKey(int key, int action) {
    // 移动输入
    if (key == GLFW_KEY_W) m_moveInput.forward = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_S) m_moveInput.backward = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_A) m_moveInput.left = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_D) m_moveInput.right = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_SPACE) m_moveInput.jump = (action == GLFW_PRESS);
    if (key == GLFW_KEY_LEFT_SHIFT) m_moveInput.sprint = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_LEFT_CONTROL) {
        m_moveInput.crouch = (action != GLFW_RELEASE);
        setCrouching(m_moveInput.crouch);
    }

    // 玩家特定的按键处理
    if (key == GLFW_KEY_Q && action == GLFW_PRESS) {
        // 增加速度
        m_speedMultiplier += 0.33f;
        if (m_speedMultiplier > 10.0f) m_speedMultiplier = 10.0f;
    }

    if (key == GLFW_KEY_E && action == GLFW_PRESS) {
        // 减少速度
        m_speedMultiplier -= 0.33f;
        if (m_speedMultiplier < 0.1f) m_speedMultiplier = 0.1f;
    }

    // 数字键选择物品栏
    if (action == GLFW_PRESS) {
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
            int slot = key - GLFW_KEY_1;
            setSelectedSlot(slot);
        } else if (key == GLFW_KEY_0) {
            setSelectedSlot(9); // 0键选择第10个槽位
        }
    }
}

void Player::processMouseScroll(double xoffset, double yoffset) {
    if (m_hotbar) {
        m_hotbar->scroll(static_cast<float>(yoffset));
    }
}

void Player::handleMovementInput(float deltaTime) {
    // 这个方法现在只更新m_moveInput状态
    // 实际的移动计算在applyMovement()中完成
}

// ==================== 方块交互 ====================

void Player::handleBlockInteraction(ChunkManager& chunkManager) {
    // 处理方块破坏
    if (m_leftMousePressed) {
        float now = static_cast<float>(glfwGetTime());
        if (now - m_lastBreakTime >= ACTION_COOLDOWN) {
            if (tryBreakBlock(chunkManager)) {
                m_lastBreakTime = now;
            }
        }
    }

    // 处理方块放置
    if (m_rightMousePressed) {
        float now = static_cast<float>(glfwGetTime());
        if (now - m_lastPlaceTime >= ACTION_COOLDOWN) {
            if (tryPlaceBlock(chunkManager)) {
                m_lastPlaceTime = now;
            }
        }
    }
}

bool Player::tryBreakBlock(ChunkManager& chunkManager) {
    if (!m_selection.hasSelected || m_selection.blockType == BLOCK_AIR) {
        return false;
    }

    // 获取当前选中的物品
    auto selectedItem = getSelectedItem();
    if (selectedItem && selectedItem->onLeftClick(m_selection.blockPos, &chunkManager)) {
        // 物品处理了左键点击（如工具加速破坏）
        return true;
    }

    // 默认破坏行为
    chunkManager.setBlock(m_selection.blockPos, BLOCK_AIR);
    return true;
}

bool Player::tryPlaceBlock(ChunkManager& chunkManager) {
    if (!m_selection.hasSelected) {
        return false;
    }

    glm::ivec3 placePos = m_selection.adjacentPos;
    BlockType blockAtPlace = chunkManager.getBlockAt(placePos);
    if (blockAtPlace != BLOCK_AIR) {
        return false; // 位置已被占用
    }

    // 获取当前选中的物品
    auto selectedItem = getSelectedItem();
    if (selectedItem) {
        // 调用物品的右键行为
        return selectedItem->onRightClick(placePos, &chunkManager);
    }

    return false;
}

// ==================== 物品栏管理 ====================

void Player::initHotbarItems() {
    // 清空现有物品
    m_hotbarItems.clear();
    m_hotbarItems.resize(10, nullptr);
    // 添加默认物品
    initDefaultItems();
}

std::shared_ptr<Item> Player::getSelectedItem() const {
    if (!m_hotbar) return nullptr;

    int selectedSlot = m_hotbar->getSelectedSlot();
    if (selectedSlot >= 0 && selectedSlot < m_hotbarItems.size()) {
        return m_hotbarItems[selectedSlot];
    }

    return nullptr;
}

int Player::getSelectedSlot() const {
    if (!m_hotbar) return 0;
    return m_hotbar->getSelectedSlot();
}

void Player::setSelectedSlot(int slot) {
    if (m_hotbar) {
        m_hotbar->setSelectedSlot(slot);
    }
}

void Player::setSpeedMultiplier(float multiplier) {
    m_speedMultiplier = multiplier;
    if (m_speedMultiplier < 0.1f) m_speedMultiplier = 0.1f;
    if (m_speedMultiplier > 10.0f) m_speedMultiplier = 10.0f;
}

void Player::updateSpeedMultiplier(float deltaTime) {
    // 保持与World基类兼容的速度控制逻辑
    static float change_speed_delta_time = 0.0f;

    if (glfwGetKey(m_window, GLFW_KEY_Q) == GLFW_PRESS) {
        change_speed_delta_time += deltaTime;
        if (change_speed_delta_time > 0.033f) {
            m_speedMultiplier += 0.33f;
            if (m_speedMultiplier > 10.0f) m_speedMultiplier = 10.0f;
            change_speed_delta_time = 0.0f;
        }
    }

    if (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS) {
        change_speed_delta_time += deltaTime;
        if (change_speed_delta_time > 0.033f) {
            m_speedMultiplier -= 0.33f;
            if (m_speedMultiplier < 0.1f) m_speedMultiplier = 0.1f;
            change_speed_delta_time = 0.0f;
        }
    }
}

// ==================== 方块选择更新 ====================

void Player::updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem) {
    // 从摄像机获取视线
    std::shared_ptr<Ray> ray = m_camera->GetViewRay();
    Ray::HitResult hitResult = ray->cast(&chunkManager, m_interactionDistance);

    m_selection.hasSelected = hitResult.hit;

    if (m_selection.hasSelected) {
        m_selection.blockPos = hitResult.blockPos;
        m_selection.adjacentPos = hitResult.adjacentPos;
        m_selection.blockType = hitResult.blockType;

        // 通知渲染系统
        renderSystem.setSelectedBlock(m_selection.blockPos);

        // 调用回调
        if (m_onBlockSelectedCallback) {
            m_onBlockSelectedCallback(m_selection.blockPos);
        }
    } else {
        m_selection.hasSelected = false;
        renderSystem.clearSelectedBlock();

        // 调用回调
        if (m_onBlockClearedCallback) {
            m_onBlockClearedCallback();
        }
    }
}

// ==================== 默认物品初始化 ====================

void Player::initDefaultItems() {
    m_hotbarItems[0] = std::make_shared<BlockItem>(BLOCK_STONE, "Stone", "Stone");
    // 槽位1: 桦木原木（使用BLOCK_WOOD类型）
    m_hotbarItems[1] = std::make_shared<BlockItem>(BLOCK_WOOD, "Birch Log", "Birch_Log");
    // 槽位2: 圆石（使用BLOCK_STONE类型，但纹理不同）
    m_hotbarItems[2] = std::make_shared<BlockItem>(BLOCK_STONE, "Cobblestone", "Cobblestone");
    // 槽位3: 橡木木板（使用BLOCK_WOOD类型）
    m_hotbarItems[3] = std::make_shared<BlockItem>(BLOCK_WOOD, "Oak Planks", "Oak_Planks");
    // 槽位4: 望远镜
    m_hotbarItems[4] = std::make_shared<SpyglassItem>("Spyglass", "spyglass");
}