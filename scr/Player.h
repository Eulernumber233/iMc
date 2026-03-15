#pragma once
#include "Camera.h"
#include "Item.h"
#include "UI/UIHotbar.h"
#include "Data.h"
#include <memory>
#include <vector>
#include <functional>

// 前向声明
class ChunkManager;
class RenderSystem;

// 玩家类 - 集成摄像机、物品栏和输入管理
class Player {
public:
    // 构造函数
    Player(std::shared_ptr<Camera> camera, GLFWwindow* window);

    // 初始化
    void initialize();

    // 更新玩家状态
    void update(float deltaTime, ChunkManager& chunkManager, RenderSystem& renderSystem);

    // 输入处理
    void processMouseMovement(float xoffset, float yoffset);
    void processMouseButton(int button, int action);
    void processKey(int key, int action);
    void processMouseScroll(double xoffset, double yoffset);

    // 摄像机访问
    std::shared_ptr<Camera> getCamera() const { return m_camera; }
    glm::vec3 getPosition() const { return m_camera->Position; }

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

    // 设置/获取速度倍数
    void setSpeedMultiplier(float multiplier);
    float getSpeedMultiplier() const { return m_speedMultiplier; }

    // 获取UI热栏
    std::shared_ptr<UIHotbar> getHotbar() const { return m_hotbar; }

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

    // 物品栏
    std::vector<std::shared_ptr<Item>> m_hotbarItems;
    std::shared_ptr<UIHotbar> m_hotbar;

    // 输入状态
    bool m_leftMousePressed = false;
    bool m_rightMousePressed = false;
    bool m_firstMouse = true;
    float m_lastX = 0.0f;
    float m_lastY = 0.0f;

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
    float m_speedMultiplier = 1.0f;
    float m_speedChangeTimer = 0.0f;

    // 回调函数
    std::function<void(const glm::ivec3&)> m_onBlockSelectedCallback;
    std::function<void()> m_onBlockClearedCallback;

    // 私有方法
    void updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem);
    void handleMovementInput(float deltaTime);
    void updateSpeedMultiplier(float deltaTime);

    // 初始化默认物品
    void initDefaultItems();
};