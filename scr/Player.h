#pragma once
#include "Camera.h"
#include "Item.h"
#include "UI/UIHotbar.h"
#include "collision/AABB.h"
#include "collision/PhysicsConstants.h"
#include "Data.h"
#include <memory>
#include <vector>
#include <functional>

// 前向声明
class ChunkManager;
class RenderSystem;

// 前向声明
class ChunkManager;
class RenderSystem;

// 玩家类 - 集成摄像机、物品栏、输入管理和物理系统
class Player {
public:
    // 构造函数
    Player(std::shared_ptr<Camera> camera, GLFWwindow* window);

    // 初始化
    void initialize();

    // 更新玩家状态（包括物理更新）
    void update(float deltaTime, ChunkManager& chunkManager, RenderSystem& renderSystem);

    // 物理更新（单独调用，用于精细控制）
    void updatePhysics(float deltaTime, ChunkManager& chunkManager);

    // 输入处理
    void processMouseMovement(float xoffset, float yoffset);
    void processMouseButton(int button, int action);
    void processKey(int key, int action);
    void processMouseScroll(double xoffset, double yoffset);

    // 摄像机访问
    std::shared_ptr<Camera> getCamera() const { return m_camera; }

    // 玩家物理属性访问
    glm::vec3 getPosition() const { return m_position; }  // 玩家碰撞箱中心
    glm::vec3 getVelocity() const { return m_velocity; }
    glm::vec3 getAcceleration() const { return m_acceleration; }
    bool isOnGround() const { return m_onGround; }
    bool isCrouching() const { return m_isCrouching; }

    // 设置玩家位置（用于传送等）
    void setPosition(const glm::vec3& position);

    // 将玩家放置在地面上
    void placeOnGround(ChunkManager& chunkManager);

    // 获取玩家AABB碰撞箱
    AABB getAABB() const;

    // 物品栏管理
    void initHotbarItems();
    std::shared_ptr<Item> getSelectedItem() const;
    int getSelectedSlot() const;
    void setSelectedSlot(int slot);

    // 方块交互
    bool tryBreakBlock(ChunkManager& chunkManager);
    bool tryPlaceBlock(ChunkManager& chunkManager);

    // 获取交互距离
    float getInteractionDistance() const { return m_interactionDistance; }

    // 三速系统参数设置（开放参数，方便调整）
    void setAirSpeed(float speed) { m_airSpeed = speed; }
    void setWalkSpeed(float speed) { m_walkSpeed = speed; }
    void setRunSpeed(float speed) { m_runSpeed = speed; }
    void setCrouchSpeed(float speed) { m_crouchSpeed = speed; }
    void setDoubleTapThreshold(float threshold) { m_doubleTapThreshold = threshold; }
    void setGroundAccel(float accel) { m_groundAccel = accel; }
    void setGroundDecel(float decel) { m_groundDecel = decel; }
    void setAirAccel(float accel) { m_airAccel = accel; }
    void setAirDecel(float decel) { m_airDecel = decel; }
    void setGravity(float g) { m_gravity = g; }
    void setJumpVelocity(float v) { m_jumpVelocity = v; }

    // 获取当前速度参数
    float getAirSpeed() const { return m_airSpeed; }
    float getWalkSpeed() const { return m_walkSpeed; }
    float getRunSpeed() const { return m_runSpeed; }
    float getCrouchSpeed() const { return m_crouchSpeed; }
    float getDoubleTapThreshold() const { return m_doubleTapThreshold; }
    float getGroundAccel() const { return m_groundAccel; }
    float getGroundDecel() const { return m_groundDecel; }
    float getAirAccel() const { return m_airAccel; }
    float getAirDecel() const { return m_airDecel; }
    bool isRunning() const { return m_isRunning; }

    // 获取UI热栏
    std::shared_ptr<UIHotbar> getHotbar() const { return m_hotbar; }

    // 物理控制
    void jump();
    void setCrouching(bool crouching);
    void applyForce(const glm::vec3& force);
    void setVelocity(const glm::vec3& velocity);

    // 回调函数设置
    void setOnBlockSelectedCallback(std::function<void(const glm::ivec3&)> callback) {
        m_onBlockSelectedCallback = callback;
    }

    void setOnBlockClearedCallback(std::function<void()> callback) {
        m_onBlockClearedCallback = callback;
    }

private:
    // 摄像机
    std::shared_ptr<Camera> m_camera;
    GLFWwindow* m_window;

    // 玩家物理属性
    glm::vec3 m_position;      // 玩家碰撞箱中心（世界坐标）
    glm::vec3 m_velocity;      // 当前速度
    glm::vec3 m_acceleration;  // 当前加速度
    bool m_onGround = false;   // 是否站在地面上
    bool m_isCrouching = false; // 是否处于蹲伏状态

    // 摄像机偏移（相对于玩家中心）
    glm::vec3 m_cameraOffsetStanding = glm::vec3(0.0f, 0.0f, 0.0f);
    glm::vec3 m_cameraOffsetCrouching = glm::vec3(0.0f, 0.0f, 0.0f);

    // 碰撞箱尺寸（宽×高×深）
    struct {
        float width = PhysicsConstants::PLAYER_WIDTH;          // X轴
        float height = PhysicsConstants::PLAYER_HEIGHT_STANDING; // Y轴（当前高度，初始为站立高度）
        float depth = PhysicsConstants::PLAYER_DEPTH;          // Z轴
        float standingHeight = PhysicsConstants::PLAYER_HEIGHT_STANDING; // 站立高度
        float crouchingHeight = PhysicsConstants::PLAYER_HEIGHT_CROUCHING; // 蹲伏高度
    } m_collisionBox;

    // 物理参数（开放参数，可通过setter调整）
    float m_mass = PhysicsConstants::PLAYER_MASS;
    float m_gravity = PhysicsConstants::GRAVITY;
    float m_jumpVelocity = PhysicsConstants::PLAYER_JUMP_VELOCITY; // 跳跃初速度 (m/s)
    float m_airSpeed = PhysicsConstants::PLAYER_AIR_SPEED;         // 空中水平目标速度 (m/s)
    float m_walkSpeed = PhysicsConstants::PLAYER_WALK_SPEED;       // 行走目标速度 (m/s)
    float m_runSpeed = PhysicsConstants::PLAYER_RUN_SPEED;         // 跑步目标速度 (m/s)
    float m_crouchSpeed = PhysicsConstants::PLAYER_CROUCH_SPEED;   // 蹲伏目标速度 (m/s)
    float m_groundAccel = PhysicsConstants::GROUND_ACCEL;          // 地面加速度 (m/s²)
    float m_groundDecel = PhysicsConstants::GROUND_DECEL;          // 地面减速度 (m/s²)
    float m_airAccel = PhysicsConstants::AIR_ACCEL;                // 空中加速度 (m/s²)
    float m_airDecel = PhysicsConstants::AIR_DECEL;                // 空中减速度 (m/s²)

    // 物品栏
    std::vector<std::shared_ptr<Item>> m_hotbarItems;
    std::shared_ptr<UIHotbar> m_hotbar;

    // 输入状态
    bool m_leftMousePressed = false;
    bool m_rightMousePressed = false;
    bool m_firstMouse = true;
    float m_lastX = 0.0f;
    float m_lastY = 0.0f;

    // 移动输入
    struct {
        bool forward = false;
        bool backward = false;
        bool left = false;
        bool right = false;
        bool jump = false;
        bool sprint = false;
        bool crouch = false;
    } m_moveInput;

    // 方块选择
    struct {
        bool hasSelected = false;
        glm::ivec3 blockPos;
        glm::ivec3 adjacentPos;
        BlockType blockType = BLOCK_AIR;
    } m_selection;

    // 交互距离
    float m_interactionDistance = 8.0f;

    // 动作冷却
    float m_lastBreakTime = 0.0f;
    float m_lastPlaceTime = 0.0f;
    const float ACTION_COOLDOWN = 0.25f;

    // 速度控制
    // 三速系统相关变量
    bool m_isRunning = false;
    float m_lastForwardPressTime = 0.0f;
    float m_doubleTapThreshold = PhysicsConstants::DOUBLE_TAP_THRESHOLD; // 双击检测阈值（秒）

    // 回调函数
    std::function<void(const glm::ivec3&)> m_onBlockSelectedCallback;
    std::function<void()> m_onBlockClearedCallback;

    // 私有方法
    void updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem);
    void handleMovementInput(float deltaTime);

    // 物理相关方法
    void updateCameraPosition();
    void clampVelocity();

    // 碰撞检测 - 分轴移动+碰撞解算
    void getAllCollisions(ChunkManager& chunkManager, std::vector<CollisionResult>& collisions) const;
    void moveAxis(int axis, float displacement, ChunkManager& chunkManager);

    // 方块交互处理
    void handleBlockInteraction(ChunkManager& chunkManager);

    // 初始化默认物品
    void initDefaultItems();
};