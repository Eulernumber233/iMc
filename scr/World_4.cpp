#include "World_4.h"
#include "UI/UIManager.h"
#include "UI/UIHotbar.h"
#include <iostream>

World_4::World_4(std::shared_ptr<Camera> camera_, GLFWwindow* window_, unsigned int seed)
    : World(camera_, window_), _seed(seed)
{
    // ���ô����û�ָ�룬�Ա�ص��������Է������ʵ��
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetCursorPosCallback(window_, mouseCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetScrollCallback(window_, mouseScrollCallback);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // ��ʼ�����λ��
    //glfwGetCursorPos(window_, &lastX, &lastY);
}

int World_4::run() {
    // ��ʼ������������
    TextureMgr::GetInstance();
    BlockFaceType::init_type_map();

    // ���������ʼλ��
    camera->Position = glm::vec3(0.0f, 70.0f, 0.0f);

    // ������Ⱦϵͳ
    RenderSystem renderSystem(SCR_WIDTH, SCR_HEIGHT);
    if (!renderSystem.initialize()) {
        std::cerr << "Failed to initialize render system" << std::endl;
        return -1;
    }

    // ���ù��ղ���
    renderSystem.setLightDirection(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)));
    renderSystem.setLightColor(glm::vec3(1.0f, 0.95f, 0.9f));
    renderSystem.setLightIntensity(1.0f);
    renderSystem.setAmbientColor(glm::vec3(0.1f, 0.1f, 0.1f));

    // �������������
    ChunkManager chunkManager(_seed);
    chunkManager.initialize(4, camera->Position); // ��Ⱦ�뾶4
    chunkManager.printStats();

    // 初始化物品栏物品
    initHotbarItems();

    // ��ѭ��
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        static float lastFrame = 0.0f;
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // ��ʾFPS
        showFPS(window);

        // ���������ƶ�
        readKey_toMove(window, deltaTime);

        // ����������أ��������λ�ø��¿ɼ����飩
        chunkManager.update(camera);

        // ����ѡ�еķ��飨���߼�⣩
        updateBlockSelection(chunkManager, renderSystem);

        // --- ���齻�� ---
        if (leftMousePressed) {
            float now = static_cast<float>(glfwGetTime());
            if (now - m_lastBreakTime >= ACTION_COOLDOWN) {
                if (m_hasSelectedBlock && m_lastHitResult.blockType != BLOCK_AIR) {
                    // �ƻ����飺����Ϊ����
                    chunkManager.setBlock(m_lastHitResult.blockPos, BLOCK_AIR);
                    m_lastBreakTime = now;
                }
            }
        }

        if (rightMousePressed) {
            float now = static_cast<float>(glfwGetTime());
            if (now - m_lastPlaceTime >= ACTION_COOLDOWN) {
                if (m_hasSelectedBlock) {
                    glm::ivec3 placePos = m_lastHitResult.adjacentPos;
                    BlockType blockAtPlace = chunkManager.getBlockAt(placePos);
                    if (blockAtPlace == BLOCK_AIR) {
                        // 获取当前选中的物品
                        auto selectedItem = getSelectedItem();
                        if (selectedItem) {
                            // 调用物品的右键行为
                            if (selectedItem->onRightClick(placePos, &chunkManager)) {
                                m_lastPlaceTime = now; // 消耗点击，更新冷却时间
                            }
                        } else {
                            // 没有物品，默认放置石头（保持向后兼容）
                            chunkManager.setBlock(placePos, BLOCK_STONE);
                            m_lastPlaceTime = now;
                        }
                    }
                }
            }
        }

        // ������ͼ��ͶӰ����
        glm::mat4 view = camera->GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);

        // ��Ⱦ֡
        renderSystem.render(chunkManager, view, projection, camera, deltaTime);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

void World_4::updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem) {
    // ����Ļ���ķ�������
    std::shared_ptr<Ray> ray = camera->GetViewRay();
    m_lastHitResult = ray->cast(&chunkManager, m_interactionDistance);
    m_hasSelectedBlock = m_lastHitResult.hit;

    if (m_hasSelectedBlock) {
        m_selectedBlockPos = m_lastHitResult.blockPos;
        renderSystem.setSelectedBlock(m_selectedBlockPos);
    }
    else {
        renderSystem.clearSelectedBlock();
    }
}

void World_4::processMouse(double xpos, double ypos) {
    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }

    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos); // ��תY��

    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);

    camera->Yaw += xoffset * camera->MouseSensitivity;
    camera->Pitch += yoffset * camera->MouseSensitivity;

    if (camera->Pitch > 89.0f) camera->Pitch = 89.0f;
    if (camera->Pitch < -89.0f) camera->Pitch = -89.0f;

    glm::vec3 front;
    front.x = cos(glm::radians(camera->Yaw)) * cos(glm::radians(camera->Pitch));
    front.y = sin(glm::radians(camera->Pitch));
    front.z = sin(glm::radians(camera->Yaw)) * cos(glm::radians(camera->Pitch));
    camera->Front = glm::normalize(front);
    camera->Right = glm::normalize(glm::cross(camera->Front, glm::vec3(0.0f, 1.0f, 0.0f)));
}

void World_4::processMouseButton(int button, int action) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        leftMousePressed = (action == GLFW_PRESS);
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        rightMousePressed = (action == GLFW_PRESS);
    }
}

void World_4::processKey(int key, int action) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

void World_4::processMouseScroll(double xoffset, double yoffset) {
    // 鼠标滚轮切换物品栏选中项
    // 将滚轮事件传递给UI管理器
    UIManager::getInstance().handleMouseScroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
}

// ��̬�ص�����ʵ��
void World_4::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void World_4::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    World_4* world = static_cast<World_4*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouse(xpos, ypos);
    }
}

void World_4::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    World_4* world = static_cast<World_4*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouseButton(button, action);
    }
}

void World_4::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    World_4* world = static_cast<World_4*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processKey(key, action);
    }
}

void World_4::mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    World_4* world = static_cast<World_4*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouseScroll(xoffset, yoffset);
    }
}
void World_4::initHotbarItems() {
    // 清空向量
    m_hotbarItems.clear();
    m_hotbarItems.resize(10, nullptr); // 10个槽位

    // 槽位0: 石头
    m_hotbarItems[0] = std::make_shared<BlockItem>(BLOCK_STONE, "Stone", "Stone");
    // 槽位1: 桦木原木（使用BLOCK_WOOD类型）
    m_hotbarItems[1] = std::make_shared<BlockItem>(BLOCK_WOOD, "Birch Log", "Birch_Log");
    // 槽位2: 圆石（使用BLOCK_STONE类型，但纹理不同）
    m_hotbarItems[2] = std::make_shared<BlockItem>(BLOCK_STONE, "Cobblestone", "Cobblestone");
    // 槽位3: 橡木木板（使用BLOCK_WOOD类型）
    m_hotbarItems[3] = std::make_shared<BlockItem>(BLOCK_WOOD, "Oak Planks", "Oak_Planks");
    // 槽位4: 望远镜
    m_hotbarItems[4] = std::make_shared<SpyglassItem>("Spyglass", "spyglass");
    // 其余槽位为空
}

std::shared_ptr<Item> World_4::getSelectedItem() const {
    // 获取UI管理器实例
    auto& uiManager = UIManager::getInstance();
    auto hotbar = std::dynamic_pointer_cast<UIHotbar>(uiManager.getComponent("hotbar"));
    if (!hotbar) {
        return nullptr;
    }
    int selectedSlot = hotbar->getSelectedSlot();
    if (selectedSlot >= 0 && selectedSlot < m_hotbarItems.size()) {
        return m_hotbarItems[selectedSlot];
    }
    return nullptr;
}
