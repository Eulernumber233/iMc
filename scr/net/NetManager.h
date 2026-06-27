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

    // ---- 消息分发 ----
    void dispatchEvents();
    void dispatchMessage(ENetPeer* peer, const NetMessage& msg);

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

    // 方块修改：服务端权威处理（校验→应用→相关性广播）/ 客户端应用广播
    void handleBlockChangeServer(ENetPeer* peer, MemoryStream& payload);
    void handleBlockChangeClient(MemoryStream& payload);

    // P2P 辅助
    void sendToAll(ENetPeer* exclude, const NetMessage& msg, bool reliable);
    void sendToAll(const NetMessage& msg, bool reliable);
};
