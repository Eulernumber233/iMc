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

class ChunkSaveManager;
class NetManager;

enum class NetMode : uint8_t {
    None,   // 单机
    Host,   // 开房间
    Join,   // 加入
};

class World
{
public:
    World(GLFWwindow* window_, const std::string& worldName,
          uint64_t seed, bool isNewWorld, NetMode netMode = NetMode::None);
    ~World();

    int run();

    // 设置联网模式（在构造后、run() 前调用），成功返回 true
    bool setupNetworking(NetMode mode, uint16_t port = 0,
                         const std::string& joinAddress = "");

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

    // 获取网络管理器
    NetManager* getNetManager() const { return m_netManager.get(); }
    bool isNetHost() const { return m_netMode == NetMode::Host; }
    bool isNetClient() const { return m_netMode == NetMode::Join; }

private:
    GLFWwindow* m_window;
    uint64_t m_seed;
    std::string m_worldName;
    bool m_isNewWorld;

    // 网络
    NetMode m_netMode = NetMode::None;
    std::unique_ptr<NetManager> m_netManager;

    // 存档管理器
    std::unique_ptr<ChunkSaveManager> m_saveManager;
    bool m_spawnFound = false;

    // 玩家对象
    std::shared_ptr<Player> m_player;

    // 本地皮肤名（用于窗口标题）
    std::string m_localSkinName;

    // 渲染系统
    RenderSystem* m_renderSystem = nullptr;

    // ---- 时间控制（o/p 调时间比例，固定灵敏度）----
    void updateSunSpeedKeys(float deltaTime);    // 主循环每帧轮询 o/p 持续按住状态
    void printTimeFlowSpeed();                   // 输出当前时间流速（现实世界单位）
    static std::string formatWorldTime(float hour);  // 0-24 浮点 → "HH:MM"

    // 区块管理器
    std::shared_ptr<ChunkManager> m_chunkManager;

    // 输入处理函数
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
