#pragma once

#include "NetCommon.h"
#include "../enet/enet.h"
#include <vector>
#include <cstdint>

// 网络事件
struct NetEvent {
    enum Type : uint8_t { None, Connected, Disconnected, Data };
    Type type = None;
    ENetPeer* peer = nullptr;
    const uint8_t* data = nullptr;
    size_t dataLen = 0;
    ENetPacket* packet = nullptr;  // 仅 Data 事件有效，用完后需 enet_packet_destroy

    // 辅助：销毁关联的 enet packet
    void destroyPacket() {
        if (packet) {
            enet_packet_destroy(packet);
            packet = nullptr;
            data = nullptr;
            dataLen = 0;
        }
    }
};

// ENet 薄封装：管理 Host / Peer，提供按通道的收发接口
class NetTransport {
public:
    NetTransport() = default;
    ~NetTransport();

    // 禁止拷贝
    NetTransport(const NetTransport&) = delete;
    NetTransport& operator=(const NetTransport&) = delete;

    // ---- 生命周期 ----
    bool init();
    void shutdown();

    // ---- Host 创建 ----
    bool createServer(uint16_t port, uint32_t maxClients = NetConstants::DEFAULT_MAX_CLIENTS);
    bool createClient();
    void destroyHost();

    // ---- 连接 (客户端) ----
    ENetPeer* connect(const char* ip, uint16_t port);

    // ---- 断开 ----
    void disconnectPeer(ENetPeer* peer);
    void disconnectAll();

    // ---- 每帧轮询 ----
    // 将所有就绪事件收集到 outEvents，timeoutMs=0 表示非阻塞
    void poll(std::vector<NetEvent>& outEvents, uint32_t timeoutMs = 0);

    // ---- 发送 ----
    void sendReliable(ENetPeer* peer, const void* data, size_t len);
    void sendUnreliable(ENetPeer* peer, const void* data, size_t len);
    void flush();

    // ---- 状态 ----
    bool isServer() const { return m_isServer; }
    ENetHost* getHost() { return m_host; }
    bool isInitialized() const { return m_initialized; }

private:
    ENetHost* m_host = nullptr;
    bool m_isServer = false;
    bool m_initialized = false;
};
