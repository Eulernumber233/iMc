#include "Player.h"
#include "chunk/ChunkManager.h"
#include "save/ChunkSaveManager.h"
#include "render/RenderSystem.h"
#include "UI/UIManager.h"
#include "item/ItemRegistry.h"
#include "entity/DroppedItemManager.h"
#include "collision/PhysicsConstants.h"
#include "collision/AABB.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <algorithm>

// ==================== 构造函数和初始化 ====================

Player::Player(std::shared_ptr<Camera> camera, GLFWwindow* window)
    : m_camera(camera), m_window(window) {
    // 初始化三速系统变量
    m_isRunning = false;
    m_lastForwardPressTime = 0.0f;
    m_doubleTapThreshold = PhysicsConstants::DOUBLE_TAP_THRESHOLD;
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

void Player::initialize(UIManager& uiManager, const std::string& skinPath) {
    m_uiManager = &uiManager;

    // 初始化物品栏
    initHotbarItems();

    // 创建UI热栏（像素完美，参照原版 MC 度量），9 格对齐原版
    m_hotbar = std::make_shared<UIHotbar>("hotbar", HOTBAR_SIZE);
    m_hotbar->setGuiScaleForScreen(SCR_WIDTH, SCR_HEIGHT);
    m_hotbar->anchor = glm::vec2(0.5f, 0.0f);
    m_hotbar->setPosition(SCR_WIDTH * 0.5f, static_cast<float>(2 * m_hotbar->getGuiScale()));
    m_hotbar->zIndex = 50;

    // 背包界面（默认隐藏，E 键由 World 切换）
    m_inventoryUI = std::make_shared<UIInventory>("inventory_screen");
    m_inventoryUI->setScreenSize(SCR_WIDTH, SCR_HEIGHT);
    m_inventoryUI->visible = false;
    m_inventoryUI->zIndex = 60; // 高于 hotbar(50)

    // 同步 hotbar + 背包面板图标 / 数量 / 耐久
    syncHotbarUI();

    // 注册到UI管理器
    m_uiManager->addComponent(m_hotbar);
    m_uiManager->addComponent(m_inventoryUI);

    // 创建并加载玩家模型
    m_model = std::make_unique<PlayerModel>();
    if (!m_model->initialize(skinPath)) {
        std::cerr << "Player: Failed to initialize PlayerModel" << std::endl;
        m_model.reset();
    }
}

// ==================== 物理属性访问 ====================

void Player::setPosition(const glm::vec3& position) {
    m_position = position;
    updateCameraPosition();
}

PlayerSaveData Player::getSaveData() const {
    PlayerSaveData d;
    d.posX = m_position.x;
    d.posY = m_position.y;
    d.posZ = m_position.z;
    d.yaw = m_camera->Yaw;
    d.pitch = m_camera->Pitch;
    return d;
}

void Player::loadSaveData(const PlayerSaveData& data) {
    m_position = glm::vec3(data.posX, data.posY, data.posZ);
    m_camera->Yaw = data.yaw;
    m_camera->Pitch = data.pitch;
    m_camera->UpdateCameraVectors();
    updateCameraPosition();
    m_velocity = glm::vec3(0.0f);
}

AABB Player::getAABB() const {
    glm::vec3 halfSize(m_collisionBox.width/2.0f, m_collisionBox.height/2.0f, m_collisionBox.depth/2.0f);
    return AABB::fromCenterHalfSize(m_position, halfSize);
}

void Player::placeOnGround(ChunkManager& chunkManager) {
    // 获取玩家AABB
    AABB playerAABB = getAABB();

    // 从玩家脚底向下搜索地面
    float searchDistance = 10.0f; // 最大搜索距离
    glm::vec3 playerBottom = playerAABB.min;

    for (float y = playerBottom.y; y > playerBottom.y - searchDistance; y -= 0.1f) {
        // 检查玩家脚底位置的方块
        glm::ivec3 blockPos = worldToBlockCoord(glm::vec3(m_position.x, y, m_position.z));
        BlockType blockType = chunkManager.getBlockAt(blockPos).type();

        if (blockType != BLOCK_AIR) {
            // 找到地面，将玩家放置在地面上方
            float groundY = blockPos.y + 1.0f; // 方块顶部Y坐标
            // 玩家中心Y坐标 = 地面Y坐标 + 玩家碰撞箱高度的一半
            m_position.y = groundY + m_collisionBox.height/2.0f;
            m_onGround = true;
            updateCameraPosition();
            return;
        }
    }
}

// ==================== 物理控制 ====================

void Player::jump() {
    if (m_onGround && !m_isCrouching) {
        m_velocity.y = m_jumpVelocity;
        m_onGround = false;
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

void Player::getAllCollisions(ChunkManager& chunkManager, std::vector<CollisionResult>& collisions) const {
    AABB playerAABB = getAABB();

    glm::ivec3 minBlock, maxBlock;
    expandBlockRange(playerAABB, minBlock, maxBlock, 1);

    collisions.clear();

    for (int x = minBlock.x; x <= maxBlock.x; x++) {
        for (int y = minBlock.y; y <= maxBlock.y; y++) {
            for (int z = minBlock.z; z <= maxBlock.z; z++) {
                glm::ivec3 blockPos(x, y, z);
                if (chunkManager.getBlockAt(blockPos).type() == BLOCK_AIR) continue;

                CollisionResult collision = calculateBlockCollision(playerAABB, blockPos);
                if (collision.collided) {
                    collisions.push_back(collision);
                }
            }
        }
    }
}

// 分轴移动：沿单个轴移动并解算碰撞
// axis: 0=X, 1=Y, 2=Z
void Player::moveAxis(int axis, float displacement, ChunkManager& chunkManager) {
    if (glm::abs(displacement) < 1e-7f) return;

    // 应用位移
    m_position[axis] += displacement;

    // 检测碰撞
    glm::ivec3 minBlock, maxBlock;
    AABB expandAABB = getAABB();
    expandBlockRange(expandAABB, minBlock, maxBlock, 1);

    for (int bx = minBlock.x; bx <= maxBlock.x; bx++) {
        for (int by = minBlock.y; by <= maxBlock.y; by++) {
            for (int bz = minBlock.z; bz <= maxBlock.z; bz++) {
                glm::ivec3 blockPos(bx, by, bz);
                if (chunkManager.getBlockAt(blockPos).type() == BLOCK_AIR) continue;

                AABB blockAABB = getBlockAABB(blockPos);
                // 重新获取当前玩家AABB（位置可能已被之前的block修正）
                AABB curAABB = getAABB();

                // 严格相交检测：所有三个轴都必须有真正的重叠（不含epsilon容差）
                // 这样刚好贴着地面的方块不会在水平轴上误判为碰撞
                glm::vec3 curCenter = curAABB.getCenter();
                glm::vec3 blockCenter = blockAABB.getCenter();
                glm::vec3 curHalf = curAABB.getHalfSize();
                glm::vec3 blockHalf = blockAABB.getHalfSize();

                // 计算三个轴上的重叠
                float overlapX = (curHalf.x + blockHalf.x) - glm::abs(curCenter.x - blockCenter.x);
                float overlapY = (curHalf.y + blockHalf.y) - glm::abs(curCenter.y - blockCenter.y);
                float overlapZ = (curHalf.z + blockHalf.z) - glm::abs(curCenter.z - blockCenter.z);

                // 所有轴必须有足够的重叠才算真正碰撞
                // 非当前轴要求至少有 PENETRATION_SLOP 的重叠，避免仅仅"贴着"就误判
                const float minOverlap = PhysicsConstants::PENETRATION_SLOP; // 0.005m
                for (int a = 0; a < 3; a++) {
                    float ov = (a == 0) ? overlapX : (a == 1) ? overlapY : overlapZ;
                    if (a == axis) {
                        // 当前移动轴：只要有重叠就算碰撞
                        if (ov <= 0.0f) goto next_block;
                    } else {
                        // 其他轴：需要至少 minOverlap 的重叠
                        if (ov <= minOverlap) goto next_block;
                    }
                }

                {
                    // 当前轴上的重叠即为穿透深度
                    float overlap = (axis == 0) ? overlapX : (axis == 1) ? overlapY : overlapZ;

                    // 推出方向：沿当前轴，远离方块中心
                    float pushDir = (curCenter[axis] > blockCenter[axis]) ? 1.0f : -1.0f;
                    m_position[axis] += pushDir * (overlap + PhysicsConstants::PHYSICS_BIAS);

                    // 清零该轴速度
                    if (axis == 1) {
                        if (displacement < 0.0f) {
                            m_onGround = true;
                            m_velocity.y = 0.0f;
                        } else {
                            m_velocity.y = 0.0f;
                        }
                    } else {
                        m_velocity[axis] = 0.0f;
                        // MC风格：撞墙停止跑步
                        if (m_isRunning) {
                            m_isRunning = false;
                        }
                    }
                }
                next_block:;
            }
        }
    }
}

// ==================== 物理更新 ====================

void Player::updateCameraPosition() {
    glm::vec3 cameraOffset = m_isCrouching ? m_cameraOffsetCrouching : m_cameraOffsetStanding;
    glm::vec3 eyePos = m_position + cameraOffset;
    if (m_thirdPerson) {
        // 第三人称：相机沿当前 Front 反方向退后 m_thirdPersonDistance
        // 不做碰撞检测，允许穿墙（简单实现）
        m_camera->Position = eyePos - m_camera->Front * m_thirdPersonDistance;
    } else {
        m_camera->Position = eyePos;
    }
}

glm::vec3 Player::getModelFootPosition() const {
    // 玩家碰撞箱中心 -> 脚底：Y 减去当前半身高
    // 蹲伏时碰撞箱较矮，脚底仍在同一水平面上（中心 - 半高）
    glm::vec3 foot = m_position;
    foot.y -= m_collisionBox.height * 0.5f;
    return foot;
}

void Player::clampVelocity() {
    // 水平速度上限：取常量和跑步速度*1.2中的较大值，避免调参时被意外钳制
    float maxH = std::max(PhysicsConstants::MAX_HORIZONTAL_SPEED, m_runSpeed * 1.2f);
    glm::vec3 hVel(m_velocity.x, 0.0f, m_velocity.z);
    float hSpeed = glm::length(hVel);
    if (hSpeed > maxH) {
        float scale = maxH / hSpeed;
        m_velocity.x *= scale;
        m_velocity.z *= scale;
    }
    if (m_velocity.y < -PhysicsConstants::MAX_FALL_SPEED) {
        m_velocity.y = -PhysicsConstants::MAX_FALL_SPEED;
    }
    if (m_velocity.y > PhysicsConstants::MAX_VERTICAL_SPEED) {
        m_velocity.y = PhysicsConstants::MAX_VERTICAL_SPEED;
    }
}

void Player::updatePhysics(float deltaTime, ChunkManager& chunkManager) {
    if (deltaTime > 0.05f) deltaTime = 0.05f;

    // ---- 1. 处理跳跃 ----
    if (m_moveInput.jump) {
        if (m_isCrouching) {
            // 下蹲时不能跳跃，直接吞掉请求
            m_moveInput.jump = false;
        } else if (m_onGround) {
            jump();
            m_moveInput.jump = false;
        } else if (m_velocity.y < -2.0f) {
            // 空中坠落中，清除跳跃请求
            m_moveInput.jump = false;
        }
        // 其他情况保留请求，着地后自动触发
    }

    // ---- 2. 水平移动：读取输入，计算目标速度，加速/减速 ----
    bool hasForward = m_moveInput.forward;
    bool hasBackward = m_moveInput.backward;
    bool hasLeft = m_moveInput.left;
    bool hasRight = m_moveInput.right;

    // 反方向抵消
    if (hasForward && hasBackward) { hasForward = false; hasBackward = false; }
    if (hasLeft && hasRight) { hasLeft = false; hasRight = false; }

    // 计算世界空间水平移动方向
    glm::vec3 moveDir(0.0f);
    if (hasForward)  moveDir += m_camera->Front;
    if (hasBackward) moveDir -= m_camera->Front;
    if (hasLeft)     moveDir -= m_camera->Right;
    if (hasRight)    moveDir += m_camera->Right;
    moveDir.y = 0.0f;
    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
    }

    bool hasInput = (hasForward || hasBackward || hasLeft || hasRight);

    // 选择目标速度
    // 空中时保持起跳前的速度模式（跑步跳跃=跑步速度，行走跳跃=行走速度）
    float targetSpeed = 0.0f;
    if (hasInput) {
        if (m_isCrouching) {
            targetSpeed = m_crouchSpeed;
        } else if (m_isRunning) {
            targetSpeed = m_runSpeed;
            if (hasBackward) {
                m_isRunning = false;
                targetSpeed = m_walkSpeed;
            }
        } else {
            targetSpeed = m_walkSpeed;
        }
    }

    // 当前水平速度
    glm::vec3 hVel(m_velocity.x, 0.0f, m_velocity.z);
    float hSpeed = glm::length(hVel);

    float accel = m_onGround ? m_groundAccel : m_airAccel;
    float decel = m_onGround ? m_groundDecel : m_airDecel;

    if (hasInput) {
        // 反方向检测：方向相反时立即清零
        if (hSpeed > 0.1f) {
            glm::vec3 hDir = hVel / hSpeed;
            if (glm::dot(moveDir, hDir) < -0.5f) {
                m_velocity.x = 0.0f;
                m_velocity.z = 0.0f;
                hSpeed = 0.0f;
            }
        }

        // 向目标速度加速
        glm::vec3 targetVel = moveDir * targetSpeed;
        glm::vec3 diff = targetVel - glm::vec3(m_velocity.x, 0.0f, m_velocity.z);
        float diffLen = glm::length(diff);
        if (diffLen > 0.001f) {
            float step = accel * deltaTime;
            if (step > diffLen) step = diffLen;
            glm::vec3 accelVec = (diff / diffLen) * step;
            m_velocity.x += accelVec.x;
            m_velocity.z += accelVec.z;
        }
    } else {
        // 无输入：减速至停止
        if (hSpeed > 0.001f) {
            float step = decel * deltaTime;
            if (step >= hSpeed) {
                m_velocity.x = 0.0f;
                m_velocity.z = 0.0f;
            } else {
                float scale = (hSpeed - step) / hSpeed;
                m_velocity.x *= scale;
                m_velocity.z *= scale;
            }
        } else {
            m_velocity.x = 0.0f;
            m_velocity.z = 0.0f;
        }
    }

    // ---- 3. 重力 ----
    // 始终施加重力。在地面上碰撞检测会将 m_velocity.y 清零并设置 m_onGround。
    // 每帧都施加重力可以确保：
    //   - 走到悬崖边时立刻开始下落
    //   - 站在地面上时，微小的向下速度确保碰撞检测持续触发着地判定
    m_velocity.y -= m_gravity * deltaTime;

    // ---- 4. 速度限制 ----
    clampVelocity();

    // ---- 5. 分轴移动 + 碰撞解算 ----
    // 重置地面状态，碰撞检测会重新设置
    m_onGround = false;

    float dx = m_velocity.x * deltaTime;
    float dy = m_velocity.y * deltaTime;
    float dz = m_velocity.z * deltaTime;

    // Y轴优先（重力/着地），然后水平轴
    moveAxis(1, dy, chunkManager);
    moveAxis(0, dx, chunkManager);
    moveAxis(2, dz, chunkManager);

    // ---- 6. 更新摄像机 ----
    updateCameraPosition();
}

// ==================== 观察者模式更新 ====================

void Player::updateSpectator(float deltaTime) {
    // 计算移动方向（六方向矢量相加）
    glm::vec3 moveDir(0.0f);
    if (m_moveInput.forward)  moveDir += m_camera->Front;
    if (m_moveInput.backward) moveDir -= m_camera->Front;
    if (m_moveInput.left)     moveDir -= m_camera->Right;
    if (m_moveInput.right)    moveDir += m_camera->Right;
    if (m_spectatorInput.up)   moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
    if (m_spectatorInput.down) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

    // 归一化方向（保持速度恒定）
    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
    }

    // 速度 = 基础速度 × 倍率 × 方向
    float speed = m_spectatorBaseSpeed * m_spectatorSpeedMul;
    m_velocity = moveDir * speed;

    // 直接移动位置（无碰撞）
    m_position += m_velocity * deltaTime;

    // 更新摄像机
    updateCameraPosition();
}

// ==================== 主更新循环 ====================

void Player::update(float deltaTime, ChunkManager& chunkManager, RenderSystem& renderSystem) {
    // 第一次更新时，确保玩家在地面上
    static bool firstUpdate = true;
    if (firstUpdate) {
        firstUpdate = false;
        placeOnGround(chunkManager);
    }

    if (m_moveMode == MoveMode::Spectator) {
        updateSpectator(deltaTime);
        // 观察者模式仍然更新方块选择和交互
        updateBlockSelection(chunkManager, renderSystem);
        handleBlockInteraction(chunkManager);
    } else {
        // ---- 普通模式 ----
        handleMovementInput(deltaTime);

        // 跑动状态自动退出：松开前进键或下蹲时停止跑动
        if (m_isRunning && (!m_moveInput.forward || m_isCrouching)) {
            m_isRunning = false;
        }

        // 更新物理状态
        updatePhysics(deltaTime, chunkManager);

        // 更新方块选择
        updateBlockSelection(chunkManager, renderSystem);

        // 处理方块交互
        handleBlockInteraction(chunkManager);
    }

    // ---- 动画更新（观察者和普通模式都推进，保证模型姿态合理） ----
    PlayerAnimator::Input animIn;
    animIn.horizontalVelocity = glm::vec3(m_velocity.x, 0.0f, m_velocity.z);
    animIn.walkSpeed = m_walkSpeed;
    animIn.onGround = m_onGround;
    animIn.crouching = m_isCrouching;
    animIn.running = m_isRunning;
    animIn.cameraYaw = m_camera->Yaw;
    animIn.cameraPitch = m_camera->Pitch;
    m_animator.update(deltaTime, animIn);
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
    m_camera->Yaw = std::fmod(m_camera->Yaw, 360.0f); // 保持在 [0, 360) 范围内
    m_camera->Pitch += yoffsetActual * m_camera->MouseSensitivity;

    if (m_camera->Pitch > 89.0f) m_camera->Pitch = 89.0f;
    if (m_camera->Pitch < -89.0f) m_camera->Pitch = -89.0f;

    // 更新摄像机向量
    m_camera->UpdateCameraVectors();

    // 第三人称：朝向改变后需要重新计算相机位置（绕玩家公转）
    if (m_thirdPerson) {
        updateCameraPosition();
    }
}

void Player::processMouseButton(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        m_leftMousePressed = (action == GLFW_PRESS);
        // 左键按下瞬间即播放一次挥手（即便未命中方块/在冷却中，保证点击反馈）
        // 长按持续破坏时，handleBlockInteraction 在每次破坏成功时会再次触发重置
        if (action == GLFW_PRESS) {
            m_animator.triggerSwingArm();
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        m_rightMousePressed = (action == GLFW_PRESS);
        // 右键不在按下时挥手：按用户要求，仅在成功放置/交互时（handleBlockInteraction）触发
    }
}

void Player::processKey(int key, int action) {
    // TAB切换移动模式
    if (key == GLFW_KEY_TAB && action == GLFW_PRESS) {
        if (m_moveMode == MoveMode::Normal) {
            m_moveMode = MoveMode::Spectator;
            m_velocity = glm::vec3(0.0f);
            m_spectatorSpeedMul = 1.0f;
        } else {
            m_moveMode = MoveMode::Normal;
            m_velocity = glm::vec3(0.0f);
            m_onGround = false;
            m_isRunning = false;
            if (m_isCrouching) setCrouching(false);
        }
        // 清除所有输入状态
        m_moveInput = {};
        m_spectatorInput = {};
        return;
    }

    // WASD 方向键（两种模式通用）
    if (key == GLFW_KEY_W) {
        m_moveInput.forward = (action != GLFW_RELEASE);
        // 普通模式：双击W进入奔跑
        if (m_moveMode == MoveMode::Normal && action == GLFW_PRESS) {
            float now = static_cast<float>(glfwGetTime());
            if (m_forwardWasReleased && m_lastForwardPressTime > 0.0f
                && (now - m_lastForwardPressTime) < m_doubleTapThreshold) {
                m_isRunning = true;
            }
            m_lastForwardPressTime = now;
            m_forwardWasReleased = false;
        }
        if (action == GLFW_RELEASE) {
            m_forwardWasReleased = true;
        }
    }
    if (key == GLFW_KEY_S) m_moveInput.backward = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_A) m_moveInput.left = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_D) m_moveInput.right = (action != GLFW_RELEASE);

    // 模式相关按键
    if (m_moveMode == MoveMode::Normal) {
        if (key == GLFW_KEY_SPACE) m_moveInput.jump = (action == GLFW_PRESS);
        if (key == GLFW_KEY_LEFT_CONTROL) {
            m_moveInput.crouch = (action != GLFW_RELEASE);
            setCrouching(m_moveInput.crouch);
        }
    } else {
        // 观察者模式
        if (key == GLFW_KEY_SPACE) m_spectatorInput.up = (action != GLFW_RELEASE);
        if (key == GLFW_KEY_LEFT_CONTROL) m_spectatorInput.down = (action != GLFW_RELEASE);
        if (action == GLFW_PRESS) {
            if (key == GLFW_KEY_Q) m_spectatorSpeedMul -= m_spectatorMulStep;
            if (key == GLFW_KEY_E) m_spectatorSpeedMul += m_spectatorMulStep;
        }
    }

    // 数字键选择物品栏（两种模式通用），1-9 对应 hotbar 槽 0-8
    if (action == GLFW_PRESS) {
        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
            setSelectedSlot(key - GLFW_KEY_1);
        }
    }

    // F：丢弃当前选中格一个物品（背包界面开启时由 World 路由到悬停格）
    if (key == GLFW_KEY_F && action == GLFW_PRESS) {
        dropFromSlot(-1, 1);
    }
}

void Player::processMouseScroll(double xoffset, double yoffset) {
    if (m_hotbar) {
        m_hotbar->scroll(static_cast<float>(yoffset));
    }
}

void Player::handleMovementInput(float deltaTime) {
    // 这个方法现在只更新m_moveInput状态
    // 实际的移动计算在updatePhysics()中完成
}

// ==================== 方块交互 ====================

void Player::handleBlockInteraction(ChunkManager& chunkManager) {
    // 处理方块破坏：长按时每 cooldown 挥一次（MC 风格）
    if (m_leftMousePressed) {
        float now = static_cast<float>(glfwGetTime());
        if (now - m_lastBreakTime >= ACTION_COOLDOWN) {
            if (tryBreakBlock(chunkManager)) {
                m_lastBreakTime = now;
                m_animator.triggerSwingArm();
            }
        }
    }

    // 处理方块放置：成功放置时挥手（将来用于其他交互也一样）
    if (m_rightMousePressed) {
        float now = static_cast<float>(glfwGetTime());
        if (now - m_lastPlaceTime >= ACTION_COOLDOWN) {
            if (tryPlaceBlock(chunkManager)) {
                m_lastPlaceTime = now;
                m_animator.triggerSwingArm();        // 第三人称 / 远程：复用挖方块那套挥手
                if (m_model) m_model->triggerHandSwing();  // 第一人称手部：复用左键挥手动画
            }
        }
    }
}

// 方块类型 → 破坏掉落的物品 id（原版风格）。返回 nullptr 表示不掉落。
// 注意：这些 id 需要在 item_registry.json 中存在且调试模式下已加载（load_in_debug）。
static const char* dropItemIdForBlock(BlockType t) {
    switch (t) {
    case BLOCK_STONE:  return "stone";
    case BLOCK_DIRT:   return "dirt";
    case BLOCK_GRASS:  return "dirt";     // 草方块破坏掉泥土（原版行为）
    case BLOCK_SAND:   return "sand";
    case BLOCK_WOOD:   return "oak_log";
    case BLOCK_LEAVES: return nullptr;    // 树叶暂不掉落
    default:           return nullptr;
    }
}

bool Player::tryBreakBlock(ChunkManager& chunkManager) {
    if (!m_selection.hasSelected || m_selection.blockState.type() == BLOCK_AIR) {
        return false;
    }

    PlacementContext ctx{ m_selection.blockPos, m_selection.hitFace, m_camera->Front };

    // 获取当前选中的物品栈
    ItemStack* selected = getSelectedStack();
    if (selected && !selected->empty() && selected->def && selected->def->behaviorObj
        && selected->def->behaviorObj->onLeftClick(*selected, ctx, &chunkManager)) {
        // 物品处理了左键点击（如工具加速破坏）
        return true;
    }

    // 默认破坏行为：写入空气
    BlockType broken = m_selection.blockState.type();
    chunkManager.setBlock(m_selection.blockPos, BlockState(BLOCK_AIR));

    // 破坏掉落：按方块类型映射到掉落物 id（原版风格：草→泥土等），在原位生成掉落物
    if (m_droppedItems) {
        const char* dropId = dropItemIdForBlock(broken);
        const ItemDefinition* def = dropId ? ItemRegistry::instance().get(dropId) : nullptr;
        if (def) {
            glm::vec3 c = glm::vec3(m_selection.blockPos) + glm::vec3(0.5f);
            m_droppedItems->spawn(ItemStack(def, 1), c, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }
    return true;
}

bool Player::tryPlaceBlock(ChunkManager& chunkManager) {
    if (!m_selection.hasSelected) {
        return false;
    }

    glm::ivec3 placePos = m_selection.adjacentPos;
    if (chunkManager.getBlockAt(placePos).type() != BLOCK_AIR) {
        return false; // 位置已被占用
    }

    // 获取当前选中的物品栈。adjacentPos = 放置位置；hitFace 是玩家点中的那一面，
    // 带轴方块（如原木）据此决定 orient。
    ItemStack* selected = getSelectedStack();
    if (selected && !selected->empty() && selected->def && selected->def->behaviorObj) {
        PlacementContext ctx{ placePos, m_selection.hitFace, m_camera->Front };
        bool consumed = selected->def->behaviorObj->onRightClick(*selected, ctx, &chunkManager);
        if (consumed) syncHotbarUI(); // 数量可能变化，刷新角标
        return consumed;
    }

    return false;
}

// ==================== 物品栏管理 ====================

void Player::initHotbarItems() {
    // 36 格背包（slot 0-8 = hotbar）
    m_inventory.assign(INVENTORY_SIZE, ItemStack{});
    // 添加默认物品
    initDefaultItems();
}

ItemStack* Player::getSelectedStack() {
    if (!m_hotbar) return nullptr;
    int slot = m_hotbar->getSelectedSlot();
    if (slot >= 0 && slot < (int)m_inventory.size()) return &m_inventory[slot];
    return nullptr;
}

void Player::syncHotbarUI() {
    if (!m_hotbar) return;
    for (int i = 0; i < HOTBAR_SIZE; ++i) {
        const ItemStack& st = m_inventory[i];
        if (st.empty() || !st.def) {
            m_hotbar->setSlot(i, "", 0, -1.0f);
        } else {
            float durRatio = (st.def->hasDurability && st.def->maxDurability > 0)
                ? (float)st.durability / (float)st.def->maxDurability
                : -1.0f;
            m_hotbar->setSlot(i, st.def->iconName, st.count, durRatio, st.def->guiIconTexture);
        }
    }
    // 背包面板与 hotbar 共用同一 m_inventory 数据，一并刷新
    if (m_inventoryUI) m_inventoryUI->syncFrom(m_inventory);
}

void Player::moveStack(int src, int dst) {
    int n = (int)m_inventory.size();
    if (src < 0 || src >= n || dst < 0 || dst >= n || src == dst) return;
    ItemStack& a = m_inventory[src];
    ItemStack& b = m_inventory[dst];
    if (a.empty()) return;

    if (b.empty()) {
        // 目标空 → 整栈移动
        b = a;
        a.clear();
    } else if (a.sameItem(b)) {
        // 同类合并到 dst，余量留 src
        int space = b.maxStack() - b.count;
        int move = std::min(space, a.count);
        b.count += move;
        a.count -= move;
        if (a.count <= 0) a.clear();
    } else {
        // 不同 → 交换
        std::swap(a, b);
    }
    syncHotbarUI();
}

void Player::dropFromSlot(int slot, int count) {
    if (!m_droppedItems) return;
    if (slot < 0) slot = getSelectedSlot();
    if (slot < 0 || slot >= (int)m_inventory.size()) return;
    ItemStack& st = m_inventory[slot];
    if (st.empty() || !st.def) return;

    int n = std::min(count, st.count);
    if (n <= 0) return;

    // 沿视线在眼前抛出
    glm::vec3 eye = m_position + (m_isCrouching ? m_cameraOffsetCrouching : m_cameraOffsetStanding);
    glm::vec3 dir = glm::normalize(m_camera->Front);
    glm::vec3 pos = eye + dir * 0.4f;
    glm::vec3 vel = dir * 4.0f + glm::vec3(0.0f, 1.5f, 0.0f);

    ItemStack dropped(st.def, n);
    dropped.durability = st.durability;
    m_droppedItems->spawn(dropped, pos, vel);

    st.count -= n;
    if (st.count <= 0) st.clear();
    syncHotbarUI();
}

void Player::pickUpToCursor(int slot) {
    if (!m_cursorStack.empty()) return;                 // 光标已占用
    if (slot < 0 || slot >= (int)m_inventory.size()) return;
    ItemStack& st = m_inventory[slot];
    if (st.empty()) return;
    m_cursorStack = st;      // 整栈拿到光标
    st.clear();              // 源格清空（背包腾出一格）
    syncHotbarUI();
}

void Player::placeCursorToSlot(int slot) {
    if (m_cursorStack.empty()) return;
    if (slot < 0 || slot >= (int)m_inventory.size()) return;
    ItemStack& dst = m_inventory[slot];
    if (dst.empty()) {
        dst = m_cursorStack;
        m_cursorStack.clear();
    } else if (dst.sameItem(m_cursorStack)) {
        int space = dst.maxStack() - dst.count;
        int move = std::min(space, m_cursorStack.count);
        dst.count += move;
        m_cursorStack.count -= move;
        if (m_cursorStack.count <= 0) m_cursorStack.clear();
    } else {
        std::swap(dst, m_cursorStack); // 异类交换，光标改持原格物品
    }
    syncHotbarUI();
}

void Player::dropCursorStack() {
    if (m_cursorStack.empty()) return;
    if (m_droppedItems) {
        glm::vec3 eye = m_position + (m_isCrouching ? m_cameraOffsetCrouching : m_cameraOffsetStanding);
        glm::vec3 dir = glm::normalize(m_camera->Front);
        glm::vec3 pos = eye + dir * 0.4f;
        glm::vec3 vel = dir * 4.0f + glm::vec3(0.0f, 1.5f, 0.0f);
        m_droppedItems->spawn(m_cursorStack, pos, vel);
    }
    m_cursorStack.clear();
    syncHotbarUI();
}

void Player::returnCursorToInventory() {
    if (m_cursorStack.empty()) return;
    addToInventory(m_cursorStack);   // 就地减少
    if (!m_cursorStack.empty())      // 塞不下的丢到世界
        dropCursorStack();
    else
        m_cursorStack.clear();
    syncHotbarUI();
}

bool Player::addToInventory(ItemStack& stack) {
    if (stack.empty() || !stack.def) return true;
    // 1) 先合并进同类未满栈
    for (auto& slot : m_inventory) {
        if (stack.count <= 0) break;
        if (!slot.empty() && slot.sameItem(stack) && slot.count < slot.maxStack()) {
            int space = slot.maxStack() - slot.count;
            int move = std::min(space, stack.count);
            slot.count += move;
            stack.count -= move;
        }
    }
    // 2) 再填空格
    for (auto& slot : m_inventory) {
        if (stack.count <= 0) break;
        if (slot.empty()) {
            int move = std::min(stack.def->maxStack, stack.count);
            slot = ItemStack(stack.def, move);
            slot.durability = stack.durability;
            stack.count -= move;
        }
    }
    syncHotbarUI();
    return stack.count <= 0;
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

// 速度倍数功能已移除，改为三速系统

// ==================== 方块选择更新 ====================

void Player::updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem) {
    // 射线必须从玩家眼睛位置发出，而不是 m_camera->Position。
    // 第三人称下相机已退后到玩家身后，若用相机位置会让交互距离凭空增加。
    glm::vec3 cameraOffset = m_isCrouching ? m_cameraOffsetCrouching : m_cameraOffsetStanding;
    glm::vec3 eyePos = m_position + cameraOffset;
    auto ray = std::make_shared<Ray>(eyePos, m_camera->Front);
    Ray::HitResult hitResult = ray->cast(&chunkManager, m_interactionDistance);

    m_selection.hasSelected = hitResult.hit;

    if (m_selection.hasSelected) {
        m_selection.blockPos = hitResult.blockPos;
        m_selection.adjacentPos = hitResult.adjacentPos;
        m_selection.hitFace    = hitResult.face;
        m_selection.blockState = hitResult.blockState;

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
    const ItemRegistry& reg = ItemRegistry::instance();
    // 按 id 从注册表取定义组装物品栈。调试模式下这些 id 都标了 load_in_debug。
    auto give = [&](int slot, const std::string& id, int count) {
        if (slot < 0 || slot >= (int)m_inventory.size()) return;
        const ItemDefinition* def = reg.get(id);
        if (def) m_inventory[slot] = ItemStack(def, count);
    };
    give(0, "stone", 64);
    give(1, "oak_log", 32);
    give(2, "dirt", 64);
    give(3, "sand", 16);
    give(4, "spyglass", 1);
    give(5, "diamond_sword", 1);
    give(6, "apple", 5);
    give(7, "diamond", 12);
    give(8, "stick", 1);   // 数量 1 不显示角标
}