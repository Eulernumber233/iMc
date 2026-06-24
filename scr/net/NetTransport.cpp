#include "NetTransport.h"
#include <cstdio>

NetTransport::~NetTransport() {
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

void NetTransport::poll(std::vector<NetEvent>& outEvents, uint32_t timeoutMs) {
    if (!m_host) return;

    ENetEvent ev;
    while (enet_host_service(m_host, &ev, timeoutMs) > 0) {
        NetEvent netEv;
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
            netEv.data = ev.packet->data;
            netEv.dataLen = ev.packet->dataLength;
            netEv.packet = ev.packet;  // 由 dispatch 层用完后 destroy
            break;

        default:
            continue;
        }
        outEvents.push_back(netEv);

        // Data 事件的 packet 需要调用方处理完后再 destroy
        // 这里先不 destroy，由 dispatch 层负责
        if (ev.type != ENET_EVENT_TYPE_RECEIVE) {
            // 非 Data 事件不需要 packet
        }
    }
}

void NetTransport::sendReliable(ENetPeer* peer, const void* data, size_t len) {
    if (!peer || !data || len == 0) return;
    ENetPacket* pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    enet_peer_send(peer, NetConstants::CHANNEL_RELIABLE, pkt);
}

void NetTransport::sendUnreliable(ENetPeer* peer, const void* data, size_t len) {
    if (!peer || !data || len == 0) return;
    ENetPacket* pkt = enet_packet_create(data, len, ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(peer, NetConstants::CHANNEL_UNRELIABLE, pkt);
}

void NetTransport::flush() {
    if (m_host) {
        enet_host_flush(m_host);
    }
}
