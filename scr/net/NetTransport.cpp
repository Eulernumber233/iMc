#include "NetTransport.h"
#include <cstdio>
#include <cstring>
#include <chrono>

NetTransport::~NetTransport() {
    stopNetThread();
    if (m_host) {
        destroyHost();
    }
    if (m_initialized) {
        shutdown();
    }
}

bool NetTransport::init() {
    if (m_initialized) return true;
    if (enet_initialize() != 0) {
        fprintf(stderr, "[NetTransport] enet_initialize() failed\n");
        return false;
    }
    m_initialized = true;
    return true;
}

void NetTransport::shutdown() {
    // 必须先停网络线程，再销毁 host / deinitialize
    stopNetThread();
    if (m_host) {
        destroyHost();
    }
    if (m_initialized) {
        enet_deinitialize();
        m_initialized = false;
    }
}

bool NetTransport::createServer(uint16_t port, uint32_t maxClients) {
    if (!m_initialized) {
        fprintf(stderr, "[NetTransport] not initialized\n");
        return false;
    }
    if (m_host) {
        destroyHost();
    }

    ENetAddress addr;
    addr.host = ENET_HOST_ANY;
    addr.port = port;

    m_host = enet_host_create(&addr, maxClients,
        NetConstants::CHANNEL_UNRELIABLE + 1, // 通道数
        0, 0);  // 不限带宽

    if (!m_host) {
        int err = WSAGetLastError();
        fprintf(stderr, "[NetTransport] enet_host_create (server) failed\n");
        fprintf(stderr, "[NetTransport]   port=%u, maxClients=%u\n", port, maxClients);
        fprintf(stderr, "[NetTransport]   WSAGetLastError() = %d (0x%X)\n", err, err);
        // 常见原因：10049=WSAEADDRNOTAVAIL(IPv6不可用), 10048=WSAEADDRINUSE(端口占用),
        //          10013=WSAEACCES(权限不足), 10022=WSAEINVAL(参数无效)
        return false;
    }
    m_isServer = true;
    printf("[NetTransport] server created on port %u, max clients %u\n", port, maxClients);
    return true;
}

bool NetTransport::createClient() {
    if (!m_initialized) {
        fprintf(stderr, "[NetTransport] not initialized\n");
        return false;
    }
    if (m_host) {
        destroyHost();
    }

    m_host = enet_host_create(nullptr, 1,  // 1 个 outgoing
        NetConstants::CHANNEL_UNRELIABLE + 1,
        0, 0);

    if (!m_host) {
        int err = WSAGetLastError();
        fprintf(stderr, "[NetTransport] enet_host_create (client) failed\n");
        fprintf(stderr, "[NetTransport]   WSAGetLastError() = %d (0x%X)\n", err, err);
        return false;
    }
    m_isServer = false;
    return true;
}

void NetTransport::destroyHost() {
    if (m_host) {
        enet_host_destroy(m_host);
        m_host = nullptr;
    }
}

ENetPeer* NetTransport::connect(const char* ip, uint16_t port) {
    if (!m_host) return nullptr;

    ENetAddress addr;
    if (enet_address_set_host(&addr, ip) != 0) {
        fprintf(stderr, "[NetTransport] failed to resolve: %s\n", ip);
        return nullptr;
    }
    addr.port = port;

    ENetPeer* peer = enet_host_connect(m_host, &addr,
        NetConstants::CHANNEL_UNRELIABLE + 1, 0);
    if (!peer) {
        fprintf(stderr, "[NetTransport] enet_host_connect failed\n");
    }
    return peer;
}

void NetTransport::disconnectPeer(ENetPeer* peer) {
    if (peer) {
        enet_peer_disconnect(peer, 0);
    }
}

void NetTransport::disconnectAll() {
    if (m_host) {
        enet_host_flush(m_host);
        for (size_t i = 0; i < m_host->peerCount; ++i) {
            enet_peer_disconnect(&m_host->peers[i], 0);
        }
    }
}

// ============================================================================
// 网络线程
// ============================================================================

void NetTransport::startNetThread() {
    if (m_threadRunning.load()) return;
    if (!m_host) {
        fprintf(stderr, "[NetTransport] startNetThread: no host\n");
        return;
    }
    m_threadStop.store(false);
    m_threadRunning.store(true);
    m_netThread = std::thread(&NetTransport::netThreadMain, this);
}

void NetTransport::stopNetThread() {
    if (!m_threadRunning.load()) return;
    m_threadStop.store(true);
    m_outCV.notify_all();
    if (m_netThread.joinable()) m_netThread.join();
    m_threadRunning.store(false);

    // 清空残留队列（线程已 join，无并发）
    {
        std::lock_guard<std::mutex> lk(m_outMutex);
        m_outQueue.clear();
    }
    {
        std::lock_guard<std::mutex> lk(m_inMutex);
        m_inQueue.clear();
    }
}

void NetTransport::netThreadMain() {
    // 每轮：drain 出站 → enet_peer_send + flush → enet_host_service 收事件（小超时阻塞）。
    // 小超时（5ms）既给收包阻塞、又限制出站延迟上限。
    static constexpr uint32_t SERVICE_TIMEOUT_MS = 5;

    std::deque<OutboundMsg> outBatch;
    while (!m_threadStop.load()) {
        // 1) 取走本轮出站消息
        outBatch.clear();
        {
            std::unique_lock<std::mutex> lk(m_outMutex);
            // 没有出站消息时，等一小会（被新出站消息或停止信号唤醒），避免空转。
            if (m_outQueue.empty()) {
                m_outCV.wait_for(lk, std::chrono::milliseconds(SERVICE_TIMEOUT_MS),
                    [this] { return m_threadStop.load() || !m_outQueue.empty(); });
            }
            outBatch.swap(m_outQueue);
        }

        // 2) 发送（仅网络线程触碰 enet_peer_send / packet_create）
        bool sentAny = false;
        for (auto& msg : outBatch) {
            if (!msg.peer || msg.data.empty()) continue;
            ENetPacket* pkt = enet_packet_create(
                msg.data.data(), msg.data.size(),
                msg.reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
            int ch = msg.reliable ? NetConstants::CHANNEL_RELIABLE
                                  : NetConstants::CHANNEL_UNRELIABLE;
            if (enet_peer_send(msg.peer, ch, pkt) < 0) {
                // peer 已断开/无效：enet_peer_send 失败时未入队，需手动销毁 packet
                enet_packet_destroy(pkt);
            } else {
                sentAny = true;
            }
        }

        // 3) 收事件 + 驱动 ENet（service 内部含 flush 出站）
        serviceOnce(SERVICE_TIMEOUT_MS);

        // service 已驱动发送；若本轮有 send 但 service 未及时 flush，再补一次。
        if (sentAny && m_host) enet_host_flush(m_host);
    }
}

void NetTransport::serviceOnce(uint32_t timeoutMs) {
    if (!m_host) return;

    ENetEvent ev;
    // 第一次用 timeoutMs 阻塞等待，之后非阻塞收完本轮所有就绪事件。
    int ret = enet_host_service(m_host, &ev, timeoutMs);
    while (ret > 0) {
        NetEvent netEv;
        bool keep = true;
        switch (ev.type) {
        case ENET_EVENT_TYPE_CONNECT:
            netEv.type = NetEvent::Connected;
            netEv.peer = ev.peer;
            printf("[NetTransport] peer connected: %x:%u\n",
                ev.peer->address.host, ev.peer->address.port);
            break;

        case ENET_EVENT_TYPE_DISCONNECT:
            netEv.type = NetEvent::Disconnected;
            netEv.peer = ev.peer;
            printf("[NetTransport] peer disconnected: %x:%u\n",
                ev.peer->address.host, ev.peer->address.port);
            break;

        case ENET_EVENT_TYPE_RECEIVE:
            netEv.type = NetEvent::Data;
            netEv.peer = ev.peer;
            // 立即把 packet 字节拷进自持 vector，并销毁 packet（不跨线程传 ENetPacket）。
            netEv.data.assign(ev.packet->data, ev.packet->data + ev.packet->dataLength);
            enet_packet_destroy(ev.packet);
            break;

        default:
            keep = false;
            break;
        }

        if (keep) {
            std::lock_guard<std::mutex> lk(m_inMutex);
            m_inQueue.push_back(std::move(netEv));
        }

        ret = enet_host_service(m_host, &ev, 0);  // 非阻塞收完剩余
    }
}

void NetTransport::drainInbound(std::vector<NetEvent>& outEvents) {
    std::lock_guard<std::mutex> lk(m_inMutex);
    while (!m_inQueue.empty()) {
        outEvents.push_back(std::move(m_inQueue.front()));
        m_inQueue.pop_front();
    }
}

void NetTransport::sendReliable(ENetPeer* peer, const void* data, size_t len) {
    if (!peer || !data || len == 0) return;
    OutboundMsg msg;
    msg.peer = peer;
    msg.reliable = true;
    msg.data.assign(static_cast<const uint8_t*>(data),
                    static_cast<const uint8_t*>(data) + len);
    {
        std::lock_guard<std::mutex> lk(m_outMutex);
        m_outQueue.push_back(std::move(msg));
    }
    m_outCV.notify_one();
}

void NetTransport::sendUnreliable(ENetPeer* peer, const void* data, size_t len) {
    if (!peer || !data || len == 0) return;
    OutboundMsg msg;
    msg.peer = peer;
    msg.reliable = false;
    msg.data.assign(static_cast<const uint8_t*>(data),
                    static_cast<const uint8_t*>(data) + len);
    {
        std::lock_guard<std::mutex> lk(m_outMutex);
        m_outQueue.push_back(std::move(msg));
    }
    m_outCV.notify_one();
}

void NetTransport::flush() {
    // 阶段 2：网络线程每轮自动 flush，这里只唤醒它尽快处理出站队列。
    m_outCV.notify_one();
}
