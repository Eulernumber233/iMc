#pragma once
#include "World.h"
#include "generate/TerrainGenerator.h"
#include "render/RenderSystem.h"
#include "chunk/ChunkManager.h"

class World_4 : public World
{
public:
    World_4(std::shared_ptr<Camera> camera_, GLFWwindow* window_, unsigned int seed = 42);
    int run() override;

private:
    unsigned int _seed;

    // 交互相关
    Ray::HitResult m_lastHitResult;
    bool m_hasSelectedBlock = false;
    glm::ivec3 m_selectedBlockPos;
    float m_interactionDistance = 8.0f;

    // 鼠标按键状态
    bool leftMousePressed = false;
    bool rightMousePressed = false;

    // 操作冷却
    float m_lastBreakTime = 0.0f;
    float m_lastPlaceTime = 0.0f;
    const float ACTION_COOLDOWN = 0.25f;

    // 鼠标初始标志
    bool firstMouse = true;
    float lastX, lastY;

    // 输入处理函数
    void processMouse(double xpos, double ypos);
    void processMouseButton(int button, int action);
    void processKey(int key, int action);
    void updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem);

    // 静态回调函数
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};