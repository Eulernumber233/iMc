#pragma once
#include "Shader.h"
#include "Camera.h"
#include "TextureMgr.h"
#include "Player.h"
#include "render/RenderSystem.h"
#include "chunk/ChunkManager.h"
#include "generate/TerrainGenerator.h"
#include "entity/DroppedItemManager.h"
#include "net/NetCommon.h"
#include "net/NetSerializer.h"   // InventoryData
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

    // 世界时间网络同步辅助（需求 3）
    bool isTimeNetClient() const;                    // 是否为「时间由网络驱动」的客户端
    void applyTimeCommand(WorldCmdType type, float param);  // Host 应用客户端时间命令

    // 背包同步辅助（需求 2）
    std::string localHeldItemId() const;             // 本地玩家当前手持物 id（空手 = ""）
    void buildLocalInventoryData(InventoryData& out) const;  // Player 背包 → InventoryData
    void applyNetInventory(const InventoryData& inv);        // InventoryData → Player 背包（解析 id）
    void syncPlayerNetInventory();                   // 每帧：把本地背包/手持物写入 netState
    // Host：保存/加载某玩家背包到独立存档文件（saves/<world>/players/<name>.json）
    void savePlayerInventoryFile(const std::string& name, const InventoryData& inv);
    bool loadPlayerInventoryFile(const std::string& name, InventoryData& out);

    // 掉落物网络同步（需求 3）
    void setupDroppedItemNetworking();               // run() 里按 netMode 接线
    void handleGameMessage(NetMsgType type, MemoryStream& payload);  // 分派 SPAWN/DESTROY/DROPPED_SYNC/DROP_REQUEST
    void broadcastDroppedSync();                     // Host 定时批量同步位置/数量
    float m_droppedSyncTimer = 0.0f;                 // DROPPED_SYNC 节流计时

    // 区块管理器
    std::shared_ptr<ChunkManager> m_chunkManager;

    // 掉落物管理器（单机本地实体）
    std::unique_ptr<DroppedItemManager> m_droppedItems;

    // 背包界面开合状态（E 键）。开时解锁光标、鼠标事件路由到背包拖拽、冻结玩家操作。
    bool m_inventoryOpen = false;
    void toggleInventory();

    // ── 背包「光标携带」交互（长按拖拽 / 丢弃）────────────────────
    // 鼠标本身作为一个物品格：长按某格达阈值后，该格物品脱离背包附到光标上
    // （源格清空），光标图标随鼠标移动并显示数量；释放时落到目标格 / 拖出面板丢弃。
    // 每帧调用，累计长按计时并在越过阈值时触发脱离。
    void updateInventory(float deltaTime);
    int   m_invPressSlot = -1;      // 当前按住起始格（<0 无）
    float m_invPressTime = 0.0f;    // 已按住时长（秒）
    bool  m_invDetached  = false;   // 本次按压是否已把物品脱离到光标
    bool  m_invLmbHeld   = false;   // 左键是否处于按下状态
    glm::vec2 m_lastMouseUI = glm::vec2(0.0f); // 最近一次鼠标 UI 坐标（光标定位用）

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
