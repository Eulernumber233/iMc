#pragma once
#include "World.h"
#include "generate/TerrainGenerator.h"
#include "render/RenderSystem.h"
#include "chunk/ChunkManager.h"
#include "Item.h"
#include <memory>

class World_4 : public World
{
public:
    World_4(std::shared_ptr<Camera> camera_, GLFWwindow* window_, unsigned int seed = 42);
    int run() override;

private:
    unsigned int _seed;

    // �������
    Ray::HitResult m_lastHitResult;
    bool m_hasSelectedBlock = false;
    glm::ivec3 m_selectedBlockPos;
    float m_interactionDistance = 8.0f;

    // ��갴��״̬
    bool leftMousePressed = false;
    bool rightMousePressed = false;

    // ������ȴ
    float m_lastBreakTime = 0.0f;
    float m_lastPlaceTime = 0.0f;
    const float ACTION_COOLDOWN = 0.25f;

    // ����ʼ��־
    bool firstMouse = true;
    float lastX, lastY;

    // 物品栏相关
    std::vector<std::shared_ptr<Item>> m_hotbarItems;
    void initHotbarItems();
    std::shared_ptr<Item> getSelectedItem() const;

    // 渲染系统指针（用于粒子效果控制）
    RenderSystem* m_renderSystem = nullptr;

    // ���봦������
    void processMouse(double xpos, double ypos);
    void processMouseButton(int button, int action);
    void processKey(int key, int action);
    void processMouseScroll(double xoffset, double yoffset);
    void updateBlockSelection(ChunkManager& chunkManager, RenderSystem& renderSystem);

    // ��̬�ص�����
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void mouseCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
};