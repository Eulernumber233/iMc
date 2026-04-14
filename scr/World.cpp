#include "World.h"
#include "UI/UIManager.h"
#include "Data.h"
#include <iostream>

World::World(GLFWwindow* window_, unsigned int seed)
    : m_window(window_), m_seed(seed)
{
    // 创建摄像机
    auto camera = std::make_shared<Camera>();
    camera->Position = glm::vec3(0.0f, 62.0f, 0.0f);

    // 创建玩家对象
    m_player = std::make_shared<Player>(camera, m_window);

    // 设置窗口用户指针，以便回调函数可以访问当前实例
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetCursorPosCallback(m_window, mouseCallback);
    glfwSetMouseButtonCallback(m_window, mouseButtonCallback);
    glfwSetKeyCallback(m_window, keyCallback);
    glfwSetScrollCallback(m_window, mouseScrollCallback);
    glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

World::~World() {
    // 清理资源
    if (m_renderSystem) {
        // 注意：RenderSystem是在栈上创建的，不需要手动删除
        // 但我们需要确保指针不再被使用
        m_renderSystem = nullptr;
    }
}

int World::run() {
    // 初始化纹理管理器
    TextureMgr::GetInstance();
    BlockFaceType::init_type_map();

    // 初始化渲染系统
    RenderSystem renderSystem(SCR_WIDTH, SCR_HEIGHT);
    m_renderSystem = &renderSystem;
    if (!renderSystem.initialize()) {
        std::cerr << "Failed to initialize render system" << std::endl;
        return -1;
    }

    // 设置光照参数
    renderSystem.setLightDirection(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)));
    renderSystem.setLightColor(glm::vec3(1.0f, 0.95f, 0.9f));
    renderSystem.setLightIntensity(1.0f);
    renderSystem.setAmbientColor(glm::vec3(0.1f, 0.1f, 0.1f));

    // 初始化区块管理器
    m_chunkManager = std::make_shared<ChunkManager>(m_seed);
    m_chunkManager->initialize(4, m_player->getCamera()->Position); // 渲染半径4
    m_chunkManager->printStats();

    // 初始化玩家
    m_player->initialize();

    // 设置方块选择回调
    m_player->setOnBlockSelectedCallback([&renderSystem](const glm::ivec3& blockPos) {
        renderSystem.setSelectedBlock(blockPos);
    });

    m_player->setOnBlockClearedCallback([&renderSystem]() {
        renderSystem.clearSelectedBlock();
    });

    // 主循环
    while (!glfwWindowShouldClose(m_window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        static float lastFrame = currentFrame; // 初始化为当前时间，避免第一帧deltaTime过大
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // 限制deltaTime，避免卡顿导致物理计算错误
        if (deltaTime > 0.1f) {
            deltaTime = 0.1f; // 最大100ms
        }

        // 显示FPS
        showFPS();

        // 更新玩家状态（包括移动、交互等）
        m_player->update(deltaTime, *m_chunkManager, renderSystem);
        auto camera = m_player->getCamera();

        // 更新区块管理器（根据玩家位置更新可见区块）
        m_chunkManager->update(camera);

        // 获取视图和投影矩阵
        glm::mat4 view = camera->GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);

        // 渲染帧
        renderSystem.render(*m_chunkManager, view, projection, camera, deltaTime, m_player.get());

        glfwSwapBuffers(m_window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

void World::processMouse(double xpos, double ypos) {
    if (m_player) {
        m_player->processMouseMovement(xpos, ypos);
    }
}

void World::processMouseButton(int button, int action) {
    if (m_player) {
        m_player->processMouseButton(button, action);
    }
}

void World::processKey(int key, int action) {
    // 全局按键处理
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(m_window, GLFW_TRUE);
    }
    if (key == GLFW_KEY_G && action == GLFW_PRESS) {
        if (m_renderSystem) {
            m_renderSystem->toggleWeather();
        }
    }
    if (key == GLFW_KEY_F3 && action == GLFW_PRESS) {
        if (m_player) {
            m_player->toggleThirdPerson();
        }
    }

    // 转发给玩家处理
    if (m_player) {
        m_player->processKey(key, action);
    }
}

void World::processMouseScroll(double xoffset, double yoffset) {
    if (m_player) {
        m_player->processMouseScroll(xoffset, yoffset);
    }
}

// 静态回调函数实现
void World::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void World::mouseCallback(GLFWwindow* window, double xpos, double ypos) {
    World* world = static_cast<World*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouse(xpos, ypos);
    }
}

void World::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    World* world = static_cast<World*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouseButton(button, action);
    }
}

void World::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    World* world = static_cast<World*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processKey(key, action);
    }
}

void World::mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
    World* world = static_cast<World*>(glfwGetWindowUserPointer(window));
    if (world) {
        world->processMouseScroll(xoffset, yoffset);
    }
}

void World::showFPS() {
    static int frameCount = 0;
    static double fpsUpdateInterval = 0.5; // 每0.5秒更新
    static double fpsLastUpdate = glfwGetTime();

    double currentTime = glfwGetTime();
    frameCount++;
    if (currentTime - fpsLastUpdate >= fpsUpdateInterval) {
        double fps = frameCount / (currentTime - fpsLastUpdate);
        std::stringstream ss;
        ss << "Open Window - FPS: " << std::fixed << fps;
        glfwSetWindowTitle(m_window, ss.str().c_str());

        // 重置计数器和时间
        frameCount = 0;
        fpsLastUpdate = currentTime;
    }
}

void World::RenderQuad() {
    static GLuint quadVAO = 0;
    static GLuint quadVBO;
    if (quadVAO == 0) {
        GLfloat quadVertices[] = {
            // Positions        // Texture Coords
            -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
            1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
            1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        // Setup plane VAO
        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));
    }

    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}