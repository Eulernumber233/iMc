#pragma once
#include "Shader.h"
#include "Camera.h"
#include "TextureMgr.h"
#include "Player.h"
#include "render/RenderSystem.h"
#include "chunk/ChunkManager.h"
#include "generate/TerrainGenerator.h"
#include <memory>
#include <sstream>

class World
{
public:
    World(GLFWwindow* window_, unsigned int seed = 42);
    ~World();

    int run();

    // 获取玩家对象
    std::shared_ptr<Player> getPlayer() const { return m_player; }

    // 获取摄像机（通过玩家）
    std::shared_ptr<Camera> getCamera() const {
        return m_player ? m_player->getCamera() : nullptr;
    }

    // 获取区块管理器
    std::shared_ptr<ChunkManager> getChunkManager() const { return m_chunkManager; }

    // 获取渲染系统
    RenderSystem* getRenderSystem() const { return m_renderSystem; }

private:
    GLFWwindow* m_window;
    unsigned int m_seed;

    // 玩家对象
    std::shared_ptr<Player> m_player;

    // 渲染系统
    RenderSystem* m_renderSystem = nullptr;

    // 区块管理器
    std::shared_ptr<ChunkManager> m_chunkManager;

    // 输入处理函数（转发给Player）
    void processMouse(double xpos, double ypos);
    void processMouseButton(int button, int action);
    void processKey(int key, int action);
    void processMouseScroll(double xoffset, double yoffset);

    // 静态回调函数
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    // 工具函数
    void showFPS();
    void RenderQuad();
};

