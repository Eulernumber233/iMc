#pragma once

#include "NetTransport.h"
#include "NetObjectManager.h"
#include "NetPlayer.h"
#include "NetMessage.h"
#include "NetChunkSync.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>

class ChunkManager;

// ============================================================================
// WorldState: 世界状态单例（服务端权威），固定 netId = WORLD_STATE_NETID。
// 复制世界时间 / 流速 / 是否流动给所有客户端。Host 每帧从 RenderSystem 镜像进来，
// 客户端每帧读出灌回自己的 RenderSystem（externallyDriven 模式，不本地推进时间）。
// ============================================================================
class WorldState : public NetObject {
public:
    WorldState();

    REPLICATED(float,   m_timeHours);  // 0  世界时间 [0,24)
    REPLICATED(float,   m_timeScale);  // 1  时间流速
    REPLICATED(uint8_t, m_sunMoving);  // 2  是否流动

    // 只在值变化时标脏，避免每帧全量重发（时间几乎每帧变，流速/开关很少变）。
    void mirror(float h, float s, bool moving);

    float timeHours() const { return m_timeHours; }
    float timeScale() const { return m_timeScale; }
    bool  sunMoving() const { return m_sunMoving != 0; }
};

// ============================================================================
// NetManager: 顶层网络管理器 (Host / Join / Update / Dispatch)
// ============================================================================

class NetManager {
public:
    NetManager();
    ~NetManager();

    // 禁止拷贝
    NetManager(const NetManager&) = delete;
    NetManager& operator=(const NetManager&) = delete;

    // ---- 房主模式 ----
    bool host(uint16_t port, const std::string& worldName, uint32_t seed);

    // ---- 客户端模式 ----
    bool join(const std::string& ip, uint16_t port, const std::string& playerName);

    // ---- 每帧 ----
    void update();

    // ---- 断开 ----
    void leave();

    // ---- 玩家管理 ----
    NetPlayer* getLocalPlayer();
    NetPlayer* getPlayer(uint16_t playerId);
    const std::unordered_map<uint16_t, std::unique_ptr<NetPlayer>>& getPlayers() const {
        return m_players;
    }

    // ---- 状态 ----
    bool isHosting() const { return m_isHost; }
    bool isConnected() const { return m_connected; }
    uint16_t getLocalPlayerId() const { return m_localPlayerId; }
    uint32_t getWorldSeed() const { return m_worldSeed; }
    const std::string& getWorldName() const { return m_worldName; }
    PlayerNetState* getLocalNetState() const { return m_localNetState; }
    std::string getLocalSkinName() const;

    // ---- 访问下层 ----
    NetTransport& getTransport() { return m_transport; }
    NetObjectManager& getObjManager() { return m_objManager; }

    // ---- 地形同步 ----

    // 设置 ChunkManager 引用（需要在 host/join 后调用）
    void setChunkManager(ChunkManager* cm);

    // 服务端推送 chunk 给指定 peer
    void sendChunkData(ENetPeer* peer, const std::vector<uint8_t>& compressedData);
    // 广播给所有客户端
    void broadcastChunkData(const std::vector<uint8_t>& compressedData);
    // 客户端向服务端请求 chunk
    void sendChunkRequest(int32_t chunkX, int32_t chunkZ);

    // ---- 方块同步 ----

    // 客户端：向服务端发送方块修改"请求"（不本地应用，等服务端广播回来）。
    // 服务端（Host 玩家本地交互）：应用到权威数据并广播给相关客户端。
    // 该方法内部按 netMode 分流，World/Player 只需无脑调它。
    void requestBlockChange(int32_t worldX, int32_t worldY, int32_t worldZ,
                            uint16_t blockStateBits);

    NetChunkSync& getChunkSync() { return m_chunkSync; }

    // ---- 背包持久化（需求 2，仅 Host）----
    // persist：玩家断开/退出时把其背包写盘（World 实现，按玩家名存独立文件）。
    // load：玩家加入时读盘取回其背包（返回 false = 无存档）。
    void setPlayerPersistCallbacks(
        std::function<void(const std::string&, const InventoryData&)> persist,
        std::function<bool(const std::string&, InventoryData&)> load) {
        m_playerPersistCb = std::move(persist);
        m_playerLoadCb = std::move(load);
    }
    // 客户端：取走服务端 INVENTORY_RESTORE 恢复的背包（消费一次，返回 false = 无）。
    bool takeRestoredInventory(InventoryData& out);

    // ---- 通用游戏消息（掉落物 spawn/despawn/同步/生成请求，需求 3+4）----
    // Host：广播给所有客户端。Client：发给服务端。payload 读游标会被消费，传引用即可。
    void broadcast(NetMsgType type, MemoryStream& payload, bool reliable);
    void sendToServer(NetMsgType type, MemoryStream& payload, bool reliable);
    // 注册游戏消息处理器：收到 SPAWN/DESTROY/DROPPED_SYNC/DROP_REQUEST 时回调（World 按类型分派）。
    void setGameMessageHandler(std::function<void(NetMsgType, MemoryStream&)> fn) {
        m_gameMsgHandler = std::move(fn);
    }

    // ---- 世界状态（时间等）----
    WorldState* getWorldState() { return m_worldState; }
    // 服务端注册命令处理器：客户端发来的 WORLD_CMD 由它应用到权威时间源（RenderSystem）。
    void setWorldCmdHandler(std::function<void(WorldCmdType, float)> h) {
        m_worldCmdHandler = std::move(h);
    }
    // 客户端：向服务端发世界命令请求（改时间/流速/开关）。reliable=false 用于高频 AdjustScale。
    void sendWorldCmd(WorldCmdType type, float param, bool reliable = true);

private:
    NetTransport m_transport;
    NetObjectManager m_objManager;
    std::unordered_map<uint16_t, std::unique_ptr<NetPlayer>> m_players;

    // peer → playerId 映射（服务端用）
    std::unordered_map<ENetPeer*, uint16_t> m_peerToPlayer;

    uint16_t m_localPlayerId = 0;
    bool m_isHost = false;
    bool m_connected = false;

    // 服务端信息（客户端用）
    uint32_t m_worldSeed = 0;
    std::string m_worldName;
    ENetPeer* m_serverPeer = nullptr;  // 客户端连接的服务端 peer

    // 本地玩家 PlayerNetState 指针（便利访问）
    PlayerNetState* m_localNetState = nullptr;

    // 地形同步
    ChunkManager* m_chunkManager = nullptr;
    NetChunkSync m_chunkSync;

    // 世界状态单例（host/join 都会创建并注册到 objManager，netId 固定）
    WorldState* m_worldState = nullptr;
    std::function<void(WorldCmdType, float)> m_worldCmdHandler;
    // 创建并注册世界状态对象（幂等，避免重复）
    void ensureWorldState();

    // 背包持久化回调（Host）+ 客户端恢复暂存
    std::function<void(const std::string&, const InventoryData&)> m_playerPersistCb;
    std::function<bool(const std::string&, InventoryData&)> m_playerLoadCb;
    InventoryData m_restoredInventory;
    bool m_hasRestoredInventory = false;

    // 通用游戏消息处理器（掉落物等由 World 分派到 DroppedItemManager）
    std::function<void(NetMsgType, MemoryStream&)> m_gameMsgHandler;
    // 服务端断开某玩家前持久化其背包（从 objManager 取其 PlayerNetState）
    void persistPlayerInventory(uint16_t playerId, const std::string& name);

    // 握手期（join）收到 JOIN_ACCEPT 后、同一 drainInbound 批次里剩余的入站事件。
    // 此时 ChunkManager 尚未 setChunkManager，不能立刻 dispatch 含 chunk 的消息；
    // 暂存于此，由主循环首次 dispatchEvents 先行消费（保留原「设置好再处理」语义）。
    std::vector<NetEvent> m_deferredInbound;

    // 按当前远程客户端数更新序列化线程池大小（仅 host）。玩家加入/离开后调用。
    void updateSerializeThreadCount();

    // ---- 消息分发 ----
    void dispatchEvents();
    // 注意：非 const —— 直接读 msg.payload（推进其读游标），省掉一次 payload 拷贝。
    void dispatchMessage(ENetPeer* peer, NetMessage& msg);

    // Handlers (服务端)
    void handleJoinRequest(ENetPeer* peer, MemoryStream& payload);
    void handlePropertySyncServer(ENetPeer* peer, MemoryStream& payload);

    // Handlers (客户端)
    void handleJoinAccept(MemoryStream& payload);
    void handleJoinDeny(MemoryStream& payload);
    void handlePlayerJoined(MemoryStream& payload);
    void handlePlayerLeft(MemoryStream& payload);
    void handlePlayerList(MemoryStream& payload);
    void handlePropertySyncClient(MemoryStream& payload);
    void handleChunkData(MemoryStream& payload);
    void handleChunkRequest(ENetPeer* peer, MemoryStream& payload);
    void handleChunkResponse(MemoryStream& payload);

    // 世界命令：服务端收到客户端请求，经 m_worldCmdHandler 应用到权威时间源
    void handleWorldCmd(MemoryStream& payload);

    // 客户端：处理服务端 INVENTORY_RESTORE（存下待 World 取用）
    void handleInventoryRestore(MemoryStream& payload);

    // 方块修改：服务端权威处理（校验→应用→相关性广播）/ 客户端应用广播
    void handleBlockChangeServer(ENetPeer* peer, MemoryStream& payload);
    void handleBlockChangeClient(MemoryStream& payload);

    // P2P 辅助
    void sendToAll(ENetPeer* exclude, const NetMessage& msg, bool reliable);
    void sendToAll(const NetMessage& msg, bool reliable);
};
