#include "World.h"
#include "UI/UIManager.h"
#include "Data.h"
#include "chunk/Chunk.h"
#include "chunk/ChunkDimensions.h"
#include "collision/PhysicsConstants.h"
#include "save/ChunkSaveManager.h"
#include "RuntimeConfig.h"
#include "Profiler.h"
#include <iostream>
#include <thread>
#include <chrono>

World::World(GLFWwindow* window_, const std::string& worldName,
             uint32_t seed, bool isNewWorld)
    : m_window(window_), m_seed(seed), m_worldName(worldName), m_isNewWorld(isNewWorld)
{
    // 初始化存档管理器
    m_saveManager = std::make_unique<ChunkSaveManager>();
    if (isNewWorld) {
        m_saveManager->createWorld(worldName, seed);
    } else {
        if (!m_saveManager->openWorld(worldName)) {
            std::cerr << "[World] 加载存档失败，将创建新世界\n";
            m_saveManager->createWorld(worldName, seed);
        } else {
            m_seed = m_saveManager->getSeed();
        }
    }

    // 创建摄像机 (临时高空位置，新世界出生点会在区块(0,0)生成后重新计算)
    auto camera = std::make_shared<Camera>();
    camera->Position = glm::vec3(0.0f, 500.0f, 0.0f);

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

    // 初始化区块管理器（半径从 runtime config 读，方便不重编调参）
    m_chunkManager = std::make_shared<ChunkManager>(m_seed);
    m_chunkManager->setSaveManager(m_saveManager.get());
    m_chunkManager->initialize(RuntimeConfig::get().renderRadius, m_player->getCamera()->Position);
    m_chunkManager->printStats();

    // 读档：加载玩家位置（存档已有出生点，无需重新计算）
    if (!m_isNewWorld) {
        PlayerSaveData pd;
        if (m_saveManager->loadPlayerState(pd)) {
            m_player->loadSaveData(pd);
        }
        m_spawnFound = true;
    }

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

        // 新世界：等待区块(0,0)生成后，计算出生点（柱顶最高非空气方块上方）
        if (!m_spawnFound && m_isNewWorld) {
            Chunk* spawnChunk = m_chunkManager->getChunkAnyState(glm::ivec2(0, 0));
            if (spawnChunk && spawnChunk->isMeshReady()) {
                bool placed = false;
                for (int y = ChunkConstants::CHUNK_HEIGHT - 1; y >= 0; --y) {
                    if (spawnChunk->getBlock(0, y, 0).type() != BLOCK_AIR) {
                        float groundTop = (float)(y + 1);
                        float playerY = groundTop + PhysicsConstants::PLAYER_HEIGHT_STANDING * 0.5f;
                        m_player->setPosition(glm::vec3(0.5f, playerY, 0.5f));
                        m_spawnFound = true;
                        placed = true;
                        std::cout << "[World] 出生点: y=" << playerY
                                  << " (方块顶=" << groundTop << ")" << std::endl;
                        break;
                    }
                }
                if (!placed) {
                    // 全空气柱（不太可能），用默认高度
                    m_player->setPosition(glm::vec3(0.5f,
                        (float)ChunkConstants::CHUNK_HEIGHT * 0.5f, 0.5f));
                    m_spawnFound = true;
                }
            }
        }

        // 获取视图和投影矩阵
        glm::mat4 view = camera->GetViewMatrix();
        glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 1000.0f);

        // 渲染帧
        renderSystem.render(*m_chunkManager, view, projection, camera, deltaTime, m_player.get());

        {
            PROFILE_SCOPE("swapBuffers");
            glfwSwapBuffers(m_window);
        }
        glfwPollEvents();

        // 每秒打印一次 profiler（如 RuntimeConfig 启用）
        Profiler::frame();
    }

    // 退出保存
    if (m_saveManager && m_saveManager->isWorldOpen()) {
        PlayerSaveData pd = m_player->getSaveData();
        m_saveManager->savePlayerState(pd);
        m_chunkManager->saveAllDirtyChunks();
        m_saveManager->closeWorld();
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