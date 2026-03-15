#include "Player.h"
#include "chunk/ChunkManager.h"
#include "render/RenderSystem.h"
#include "UI/UIManager.h"
#include <GLFW/glfw3.h>
#include <iostream>

Player::Player(std::shared_ptr<Camera> camera, GLFWwindow* window)
    : m_camera(camera), m_window(window) {
    // 初始化默认值
    m_lastX = SCR_WIDTH / 2.0f;
    m_lastY = SCR_HEIGHT / 2.0f;
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

void Player::update(float deltaTime, ChunkManager& chunkManager, RenderSystem& renderSystem) {
    // 处理移动输入
    handleMovementInput(deltaTime);

    // 更新速度倍数
    updateSpeedMultiplier(deltaTime);

    // 更新方块选择
    updateBlockSelection(chunkManager, renderSystem);

    // 处理方块交互
    if (m_leftMousePressed) {
        float now = static_cast<float>(glfwGetTime());
        if (now - m_lastBreakTime >= ACTION_COOLDOWN) {
            if (tryBreakBlock(chunkManager)) {
                m_lastBreakTime = now;
            }
        }
    }

    if (m_rightMousePressed) {
        float now = static_cast<float>(glfwGetTime());
        if (now - m_lastPlaceTime >= ACTION_COOLDOWN) {
            if (tryPlaceBlock(chunkManager)) {
                m_lastPlaceTime = now;
            }
        }
    }
}

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
    // 玩家特定的按键处理
    if (key == GLFW_KEY_Q && action == GLFW_PRESS) {
        // 增加速度
        m_speedMultiplier += 0.33f;
        if (m_speedMultiplier > 10.0f) m_speedMultiplier = 10.0f;
        m_camera->MovementSpeed = m_camera->BasicMovementSpeed * m_speedMultiplier;
    }

    if (key == GLFW_KEY_E && action == GLFW_PRESS) {
        // 减少速度
        m_speedMultiplier -= 0.33f;
        if (m_speedMultiplier < 0.1f) m_speedMultiplier = 0.1f;
        m_camera->MovementSpeed = m_camera->BasicMovementSpeed * m_speedMultiplier;
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

void Player::setSpeedMultiplier(float multiplier) {
    m_speedMultiplier = multiplier;
    if (m_speedMultiplier < 0.1f) m_speedMultiplier = 0.1f;
    if (m_speedMultiplier > 10.0f) m_speedMultiplier = 10.0f;
    m_camera->MovementSpeed = m_camera->BasicMovementSpeed * m_speedMultiplier;
}

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

void Player::handleMovementInput(float deltaTime) {
    float speed = m_camera->MovementSpeed * deltaTime;

    if (glfwGetKey(m_window, GLFW_KEY_W) == GLFW_PRESS)
        m_camera->Position += speed * m_camera->Front;
    if (glfwGetKey(m_window, GLFW_KEY_S) == GLFW_PRESS)
        m_camera->Position -= speed * m_camera->Front;
    if (glfwGetKey(m_window, GLFW_KEY_A) == GLFW_PRESS)
        m_camera->Position -= speed * m_camera->Right;
    if (glfwGetKey(m_window, GLFW_KEY_D) == GLFW_PRESS)
        m_camera->Position += speed * m_camera->Right;
    if (glfwGetKey(m_window, GLFW_KEY_SPACE) == GLFW_PRESS)
        m_camera->Position += speed * m_camera->Up;
    if (glfwGetKey(m_window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
        m_camera->Position -= speed * m_camera->Up;
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
            m_camera->MovementSpeed = m_camera->BasicMovementSpeed * m_speedMultiplier;
        }
    }

    if (glfwGetKey(m_window, GLFW_KEY_E) == GLFW_PRESS) {
        change_speed_delta_time += deltaTime;
        if (change_speed_delta_time > 0.033f) {
            m_speedMultiplier -= 0.33f;
            if (m_speedMultiplier < 0.1f) m_speedMultiplier = 0.1f;
            change_speed_delta_time = 0.0f;
            m_camera->MovementSpeed = m_camera->BasicMovementSpeed * m_speedMultiplier;
        }
    }
}

void Player::initDefaultItems() {
    // 添加一些默认物品
    // 这里可以根据需要添加更多物品类型
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