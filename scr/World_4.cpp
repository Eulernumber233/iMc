#include "World_4.h"
#include <iostream>

World_4::World_4(std::shared_ptr<Camera> camera_, GLFWwindow* window_, unsigned int seed)
    : World(camera_, window_), _seed(seed)
{
    // 设置窗口用户指针，以便回调函数可以访问这个实例
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    glfwSetCursorPosCallback(window_, mouseCallback);
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetKeyCallback(window_, keyCallback);
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // 初始化鼠标位置
    //glfwGetCursorPos(window_, &lastX, &lastY);
}

int World_4::run() {
    // 初始化纹理管理器
    TextureMgr::GetInstance();
    BlockFaceType::init_type_map();

    // 设置相机初始位置
    camera->Position = glm::vec3(0.0f, 70.0f, 0.0f);

    // 创建渲染系统
    RenderSystem renderSystem(SCR_WIDTH, SCR_HEIGHT);
    if (!renderSystem.initialize()) {
        std::cerr << "Failed to initialize render system" << std::endl;
        return -1;
    }

    // 设置光照参数
    renderSystem.setLightDirection(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)));
    renderSystem.setLightColor(glm::vec3(1.0f, 0.95f, 0.9f));
    renderSystem.setLightIntensity(1.0f);
    renderSystem.setAmbientColor(glm::vec3(0.1f, 0.1f, 0.1f));

    // 创建区块管理器
    ChunkManager chunkManager(_seed);
    chunkManager.initialize(4, camera->Position); // 渲染半径4
    chunkManager.printStats();

    // 主循环
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        static float lastFrame = 0.0f;
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // 显示FPS
        showFPS(window);

        // 处理键盘移动
        readKey_toMove(window, deltaTime);

        // 处理区块加载（根据相机位置更新可见区块）
        chunkManager.update(camera);

        // 更新选中的方块（射线检测）
        updateBlockSelection(chunkManager, renderSystem);

        // --- 方块交互 ---
        if (leftMousePressed) {
            float now = static_cast<float>(glfwGetTime());
            if (now - m_lastBreakTime >= ACTION_COOLDOWN) {
                if (m_hasSelectedBlock && m_lastHitResult.blockType != BLOCK_AIR) {
                    // 破坏方块：设置为空气
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
                        // 放置石头
                        chunkManager.setBlock(placePos, BLOCK_STONE);
                        m_lastPlaceTime = now;
                    }
                }
            }
        }

        // 创建视图和投影矩阵
        glm::mat4 view = camera->GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);

        // 渲染帧
        renderSystem.render(chunkManager, view, projection, camera);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

void World_4::updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem) {
    // 从屏幕中心发射射线
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
    float yoffset = lastY - static_cast<float>(ypos); // 反转Y轴

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

// 静态回调函数实现
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