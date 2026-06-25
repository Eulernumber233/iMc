#include "NetManager.h"
#include "../mode/SkinManager.h"
#include <cstdio>
#include <chrono>

NetManager::NetManager() = default;

NetManager::~NetManager() {
    leave();
}

// ============================================================================
// Host / Join
// ============================================================================

bool NetManager::host(uint16_t port, const std::string& worldName, uint32_t seed) {
    if (m_connected) {
        fprintf(stderr, "[NetManager] already connected\n");
        return false;
    }
    if (!m_transport.init()) return false;
    if (!m_transport.createServer(port)) return false;

    m_worldName = worldName;
    m_worldSeed = seed;
    m_isHost = true;
    m_localPlayerId = 1;  // 房主 ID 固定为 1

    // 创建房主本地玩家
    auto player = std::make_unique<NetPlayer>();
    player->playerId = m_localPlayerId;
    player->playerName = "Host";
    player->skinName = "steve";
    player->peer = nullptr;  // 本地玩家无 peer
    m_localNetState = player->netState.get();
    player->netState->setOwner(player.get());

    // 注册到 ObjectManager
    m_objManager.addObject(m_localPlayerId, std::move(player->netState));
    player->netState.release();  // addObject 已接管所有权

    m_players[m_localPlayerId] = std::move(player);
    m_connected = true;

    printf("[NetManager] hosting \"%s\" on port %u, seed=%u\n",
        worldName.c_str(), port, seed);
    return true;
}

bool NetManager::join(const std::string& ip, uint16_t port, const std::string& playerName) {
    if (m_connected) {
        fprintf(stderr, "[NetManager] already connected\n");
        return false;
    }
    if (!m_transport.init()) return false;
    if (!m_transport.createClient()) return false;

    ENetPeer* serverPeer = m_transport.connect(ip.c_str(), port);
    if (!serverPeer) return false;

    m_isHost = false;
    m_serverPeer = serverPeer;

    printf("[NetManager] joining %s:%u as \"%s\"\n", ip.c_str(), port, playerName.c_str());

    // 等待 ENet 握手完成，然后发送 JOIN_REQUEST，再等待 JOIN_ACCEPT
    // 注意：enet_peer_send 在 peer 处于 CONNECTING 状态时会直接返回 -1，
    // 所以 JOIN_REQUEST 必须在 CONNECT 事件之后再发送。
    {
        using namespace std::chrono;
        ENetEvent ev;
        bool gotConnect = false;
        bool gotAccept = false;
        bool joinRequestSent = false;
        auto startTime = steady_clock::now();
        auto lastLog = startTime;
        while (duration_cast<milliseconds>(steady_clock::now() - startTime).count() < 10000) {
            int ret = enet_host_service(m_transport.getHost(), &ev, 100);
            if (ret > 0) {
                if (ev.type == ENET_EVENT_TYPE_CONNECT) {
                    gotConnect = true;
                    printf("[NetManager] transport connected (elapsed=%lldms)\n",
                        duration_cast<milliseconds>(steady_clock::now() - startTime).count());

                    // peer 现在是 CONNECTED 状态，发送 JOIN_REQUEST
                    auto joinReq = NetMessage::joinRequest(playerName);
                    std::vector<uint8_t> joinBuf;
                    joinReq.encode(joinBuf);
                    m_transport.sendReliable(serverPeer, joinBuf.data(), joinBuf.size());
                    m_transport.flush();
                    joinRequestSent = true;
                    printf("[NetManager] JOIN_REQUEST sent (elapsed=%lldms)\n",
                        duration_cast<milliseconds>(steady_clock::now() - startTime).count());
                } else if (ev.type == ENET_EVENT_TYPE_RECEIVE) {
                    NetMessage netMsg;
                    if (NetMessage::decode(ev.packet->data, ev.packet->dataLength, netMsg)) {
                        printf("[NetManager] received msg type=0x%02X (elapsed=%lldms)\n",
                            (int)netMsg.type,
                            duration_cast<milliseconds>(steady_clock::now() - startTime).count());
                        if (netMsg.type == NetMsgType::JOIN_ACCEPT) {
                            MemoryStream payload;
                            if (netMsg.payload.size() > 0)
                                payload.writeBytes(netMsg.payload.data(), netMsg.payload.size());
                            m_localPlayerId = payload.readPod<uint16_t>();
                            m_worldSeed = payload.readPod<uint32_t>();
                            // 读取服务端玩家初始位置（新格式；兼容旧格式：若 payload 不够则用默认值）
                            float spawnX = 0.0f, spawnY = 500.0f, spawnZ = 0.0f, spawnYaw = 0.0f;
                            if (payload.remaining() >= sizeof(float) * 4) {
                                spawnX = payload.readPod<float>();
                                spawnY = payload.readPod<float>();
                                spawnZ = payload.readPod<float>();
                                spawnYaw = payload.readPod<float>();
                            }

                            std::string skinName = "steve";
                            if (payload.remaining() > 0) {
                                skinName = payload.readString();
                            }

                            if (payload.remaining() > 0) {
                                m_worldName = payload.readString();
                            }

                            // 创建本地玩家
                            auto player = std::make_unique<NetPlayer>();
                            player->playerId = m_localPlayerId;
                            player->playerName = playerName;
                            player->skinName = skinName;
                            player->peer = nullptr;
                            m_localNetState = player->netState.get();
                            player->netState->setOwner(player.get());
                            // 设置初始位置（来自服务端）
                            player->updateCachedPosition(glm::vec3(spawnX, spawnY, spawnZ));
                            player->updateCachedLook(spawnYaw, 0.0f);
                            m_objManager.addObject(m_localPlayerId, std::move(player->netState));
                            player->netState.release();
                            m_players[m_localPlayerId] = std::move(player);

                            printf("[NetManager] joined, local player id=%u, world seed=%u, spawn=(%.1f,%.1f,%.1f), skin=%s\n",
                                m_localPlayerId, m_worldSeed, spawnX, spawnY, spawnZ, skinName.c_str());
                            gotAccept = true;
                        } else if (netMsg.type == NetMsgType::JOIN_DENY) {
                            MemoryStream payload;
                            if (netMsg.payload.size() > 0)
                                payload.writeBytes(netMsg.payload.data(), netMsg.payload.size());
                            std::string reason = payload.readString();
                            fprintf(stderr, "[NetManager] join denied: %s\n", reason.c_str());
                            enet_packet_destroy(ev.packet);
                            m_transport.destroyHost();
                            m_transport.shutdown();
                            return false;
                        }
                    }
                } else if (ev.type == ENET_EVENT_TYPE_DISCONNECT) {
                    printf("[NetManager] disconnected before accept (elapsed=%lldms)\n",
                        duration_cast<milliseconds>(steady_clock::now() - startTime).count());
                    if (!gotConnect && !gotAccept) {
                        printf("[NetManager] connection refused or timed out\n");
                    }
                    m_transport.destroyHost();
                    m_transport.shutdown();
                    return false;
                }
                // 销毁 receive 事件的 packet
                if (ev.packet)
                    enet_packet_destroy(ev.packet);
            } else if (ret < 0) {
                fprintf(stderr, "[NetManager] enet_host_service error during connect\n");
                m_transport.destroyHost();
                m_transport.shutdown();
                return false;
            }
            if (gotAccept) break;

            // 每 2 秒输出一次等待状态
            auto now = steady_clock::now();
            if (duration_cast<milliseconds>(now - lastLog).count() > 2000) {
                printf("[NetManager] still waiting for handshake... (elapsed=%lldms, gotConnect=%d, joinReqSent=%d)\n",
                    duration_cast<milliseconds>(now - startTime).count(),
                    gotConnect ? 1 : 0, joinRequestSent ? 1 : 0);
                lastLog = now;
            }
        }
        if (!gotAccept) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - startTime).count();
            fprintf(stderr, "[NetManager] join handshake timed out (%lldms, gotConnect=%d, joinReqSent=%d)\n",
                elapsed, gotConnect ? 1 : 0, joinRequestSent ? 1 : 0);
            m_transport.destroyHost();
            m_transport.shutdown();
            return false;
        }
    }

    m_connected = true;
    return true;
}

void NetManager::leave() {
    if (!m_connected) return;

    if (m_isHost) {
        // 通知所有客户端断开
        m_transport.disconnectAll();
    }

    m_players.clear();
    m_peerToPlayer.clear();
    m_localNetState = nullptr;
    m_transport.destroyHost();
    m_transport.shutdown();
    m_connected = false;
    m_isHost = false;

    printf("[NetManager] disconnected\n");
}

// ============================================================================
// 每帧更新
// ============================================================================

void NetManager::update() {
    if (!m_connected) return;

    // 1. 轮询事件
    dispatchEvents();

    // 2. 地形同步
    if (m_isHost) {
        m_chunkSync.pushChunks();
    }

    // 3. 收集脏属性并同步
    if (m_isHost) {
        // 服务端：收集所有 NetObject 的脏属性，发送给所有客户端
        std::vector<NetMessage> relMsgs, unrelMsgs;
        m_objManager.collectDirty(relMsgs, unrelMsgs);

        for (auto& msg : relMsgs) {
            sendToAll(nullptr, msg, true);
        }
        for (auto& msg : unrelMsgs) {
            sendToAll(nullptr, msg, false);
        }

        // 清除脏标记
        for (auto& pair : m_objManager.allObjects()) {
            pair.second->clearDirty();
        }
    } else {
        // 客户端：只同步本地玩家的脏属性到服务端
        if (m_localNetState && m_localNetState->isDirty() && m_serverPeer) {
            MemoryStream propStream;
            m_localNetState->serializeDirty(propStream);

            if (propStream.size() > 0) {
                auto msg = NetMessage::propertySync(m_localNetState->getNetId(), propStream);
                std::vector<uint8_t> buf;
                msg.encode(buf);
                // 客户端位置全走不可靠通道
                m_transport.sendUnreliable(m_serverPeer, buf.data(), buf.size());
            }
            m_localNetState->clearDirty();
        }
    }
}

void NetManager::dispatchEvents() {
    std::vector<NetEvent> events;
    m_transport.poll(events, 0);

    for (auto& ev : events) {
        switch (ev.type) {
        case NetEvent::Connected:
            if (m_isHost) {
                // 客户端连接，等 JOIN_REQUEST
                printf("[NetManager] new connection, waiting for JOIN_REQUEST\n");
            }
            break;

        case NetEvent::Disconnected: {
            auto it = m_peerToPlayer.find(ev.peer);
            if (it != m_peerToPlayer.end()) {
                uint16_t pid = it->second;
                printf("[NetManager] player %u disconnected\n", pid);
                m_objManager.destroyObject(pid);
                m_players.erase(pid);
                m_peerToPlayer.erase(it);

                // 清理地形同步中的 pending 请求
                m_chunkSync.onPeerDisconnected(ev.peer);

                // 广播给其他人
                if (m_isHost) {
                    auto msg = NetMessage::playerLeft(pid);
                    sendToAll(nullptr, msg, true);
                }
            }
            break;
        }

        case NetEvent::Data: {
            NetMessage msg;
            if (!NetMessage::decode(ev.data, ev.dataLen, msg)) break;
            dispatchMessage(ev.peer, msg);
            break;
        }

        default:
            break;
        }

        // 销毁接收到的 packet
        ev.destroyPacket();
    }
}

void NetManager::dispatchMessage(ENetPeer* peer, const NetMessage& msg) {
    // 复制 payload，因为需要多次读取
    MemoryStream payload;
    if (msg.payload.size() > 0) {
        payload.writeBytes(msg.payload.data(), msg.payload.size());
    }

    switch (msg.type) {
    case NetMsgType::JOIN_REQUEST:
        if (m_isHost) handleJoinRequest(peer, payload);
        break;

    case NetMsgType::JOIN_ACCEPT:
        if (!m_isHost) handleJoinAccept(payload);
        break;

    case NetMsgType::JOIN_DENY:
        if (!m_isHost) handleJoinDeny(payload);
        break;

    case NetMsgType::PLAYER_JOINED:
        if (!m_isHost) handlePlayerJoined(payload);
        break;

    case NetMsgType::PLAYER_LEFT:
        if (!m_isHost) handlePlayerLeft(payload);
        break;

    case NetMsgType::PLAYER_LIST:
        if (!m_isHost) handlePlayerList(payload);
        break;

    case NetMsgType::PROPERTY_SYNC:
        if (m_isHost) handlePropertySyncServer(peer, payload);
        else          handlePropertySyncClient(payload);
        break;

    case NetMsgType::CHUNK_DATA:
    case NetMsgType::CHUNK_RESPONSE:
        if (!m_isHost) {
            printf("[NetManager] received CHUNK_DATA/CHUNK_RESPONSE (%zu bytes)\n", payload.size());
            handleChunkData(payload);
        }
        break;

    case NetMsgType::CHUNK_REQUEST:
        if (m_isHost) handleChunkRequest(peer, payload);
        break;

    default:
        break;
    }
}

// ============================================================================
// 服务端 Handlers
// ============================================================================

void NetManager::handleJoinRequest(ENetPeer* peer, MemoryStream& payload) {
    std::string playerName = payload.readString();

    // 分配新玩家 ID
    uint16_t newId = 2;
    while (m_players.count(newId)) ++newId;

    // 分配皮肤（随机非 steve 皮肤）
    std::string skinName = "steve";
    if (SkinManager::instance().isInitialized() && SkinManager::instance().getSkinCount() > 1) {
        skinName = SkinManager::instance().getRandomSkin("steve");
    }

    // 创建 NetPlayer
    auto player = std::make_unique<NetPlayer>();
    player->playerId = newId;
    player->peer = peer;
    player->playerName = playerName;
    player->skinName = skinName;
    player->netState->setOwner(player.get());

    m_objManager.addObject(newId, std::move(player->netState));
    player->netState.release();

    m_players[newId] = std::move(player);
    m_peerToPlayer[peer] = newId;

    printf("[NetManager] player \"%s\" joined, assigned id=%u, skin=%s\n",
        playerName.c_str(), newId, skinName.c_str());

    // 设置新玩家的初始渲染位置（使用宿主位置作为近似出生点，
    // 避免 renderRemotePlayers 的 (0,0,0) 过滤导致远程玩家不可见）
    {
        auto it = m_players.find(newId);
        if (it != m_players.end()) {
            float hx = 0.0f, hy = 500.0f, hz = 0.0f, hyaw = 0.0f;
            if (m_localNetState) {
                hx = m_localNetState->m_position.x;
                hy = m_localNetState->m_position.y;
                hz = m_localNetState->m_position.z;
                hyaw = m_localNetState->m_yaw;
            }
            it->second->updateCachedPosition(glm::vec3(hx, hy, hz));
            it->second->updateCachedLook(hyaw, 0.0f);
        }
    }

    // 发送 JOIN_ACCEPT（含宿主当前位置，用于初始化远程玩家渲染位置）
    {
        float hx = 0.0f, hy = 500.0f, hz = 0.0f, hyaw = 0.0f;
        if (m_localNetState) {
            hx = m_localNetState->m_position.x;
            hy = m_localNetState->m_position.y;
            hz = m_localNetState->m_position.z;
            hyaw = m_localNetState->m_yaw;
        }
        auto msg = NetMessage::joinAccept(newId, m_worldSeed, hx, hy, hz, hyaw, skinName, m_worldName);
        std::vector<uint8_t> buf;
        msg.encode(buf);
        m_transport.sendReliable(peer, buf.data(), buf.size());
    }

    // 发送当前在线玩家列表给新玩家（含位置信息）
    {
        std::vector<std::pair<uint16_t, std::string>> list;
        std::vector<float> posData;
        std::vector<float> yawData;
        std::vector<std::string> skinNames;
        for (auto& [id, p] : m_players) {
            list.emplace_back(id, p->playerName);
            skinNames.push_back(p->skinName);
            if (p->netState) {
                posData.push_back(p->netState->m_position.x);
                posData.push_back(p->netState->m_position.y);
                posData.push_back(p->netState->m_position.z);
                yawData.push_back(p->netState->m_yaw);
            } else {
                posData.push_back(0.0f); posData.push_back(500.0f); posData.push_back(0.0f);
                yawData.push_back(0.0f);
            }
        }
        auto msg = NetMessage::playerList(list, posData.data(), yawData.data(), &skinNames);
        std::vector<uint8_t> buf;
        msg.encode(buf);
        m_transport.sendReliable(peer, buf.data(), buf.size());
    }

    // 广播 PLAYER_JOINED 给其他人（含初始位置）
    {
        float px = 0.0f, py = 500.0f, pz = 0.0f, pyaw = 0.0f;
        auto it = m_players.find(newId);
        if (it != m_players.end() && it->second->netState) {
            px = it->second->netState->m_position.x;
            py = it->second->netState->m_position.y;
            pz = it->second->netState->m_position.z;
            pyaw = it->second->netState->m_yaw;
        }
        auto msg = NetMessage::playerJoined(newId, playerName, px, py, pz, pyaw, skinName);
        sendToAll(peer, msg, true);  // 排除新玩家自己
    }

    // 全量推送地形给新玩家
    m_chunkSync.pushAllChunks(peer);

    m_transport.flush();
}

void NetManager::handlePropertySyncServer(ENetPeer* peer, MemoryStream& payload) {
    // 读取 payload: netObjId + 属性数据
    if (payload.remaining() < sizeof(uint16_t)) return;
    uint16_t netObjId = payload.readPod<uint16_t>();

    NetObject* obj = m_objManager.findObject(netObjId);
    if (!obj) return;

    // 反序列化属性
    obj->deserialize(payload);

    // 标记所有属性为脏，下一帧 collectDirty 会广播给其他客户端
    obj->markAllDirty();
}

// ============================================================================
// 客户端 Handlers
// ============================================================================

void NetManager::handleJoinAccept(MemoryStream& payload) {
    // 如果 join() 中已经内联处理过了，跳过
    if (m_localPlayerId != 0) return;

    m_localPlayerId = payload.readPod<uint16_t>();
    m_worldSeed = payload.readPod<uint32_t>();
    float spawnX = 0.0f, spawnY = 500.0f, spawnZ = 0.0f, spawnYaw = 0.0f;
    if (payload.remaining() >= sizeof(float) * 4) {
        spawnX = payload.readPod<float>();
        spawnY = payload.readPod<float>();
        spawnZ = payload.readPod<float>();
        spawnYaw = payload.readPod<float>();
    }

    std::string skinName = "steve";
    if (payload.remaining() > 0) {
        skinName = payload.readString();
    }

    if (payload.remaining() > 0) {
        m_worldName = payload.readString();
    }

    // 创建本地玩家
    auto player = std::make_unique<NetPlayer>();
    player->playerId = m_localPlayerId;
    player->playerName = "Client";
    player->skinName = skinName;
    player->peer = nullptr;
    m_localNetState = player->netState.get();
    player->netState->setOwner(player.get());
    player->updateCachedPosition(glm::vec3(spawnX, spawnY, spawnZ));
    player->updateCachedLook(spawnYaw, 0.0f);

    m_objManager.addObject(m_localPlayerId, std::move(player->netState));
    player->netState.release();
    m_players[m_localPlayerId] = std::move(player);

    printf("[NetManager] joined, local player id=%u, world seed=%u, spawn=(%.1f,%.1f,%.1f)\n",
        m_localPlayerId, m_worldSeed, spawnX, spawnY, spawnZ);
}

void NetManager::handleJoinDeny(MemoryStream& payload) {
    std::string reason = payload.readString();
    fprintf(stderr, "[NetManager] join denied: %s\n", reason.c_str());
    leave();
}

void NetManager::handlePlayerJoined(MemoryStream& payload) {
    uint16_t playerId = payload.readPod<uint16_t>();
    std::string name = payload.readString();
    float px = 0.0f, py = 500.0f, pz = 0.0f, pyaw = 0.0f;
    if (payload.remaining() >= sizeof(float) * 4) {
        px = payload.readPod<float>();
        py = payload.readPod<float>();
        pz = payload.readPod<float>();
        pyaw = payload.readPod<float>();
    }

    std::string skinName;
    if (payload.remaining() > 0) {
        skinName = payload.readString();
    }

    auto player = std::make_unique<NetPlayer>();
    player->playerId = playerId;
    player->playerName = name;
    player->skinName = skinName;
    player->peer = nullptr;
    player->netState->setOwner(player.get());
    player->updateCachedPosition(glm::vec3(px, py, pz));
    player->updateCachedLook(pyaw, 0.0f);

    m_objManager.addObject(playerId, std::move(player->netState));
    player->netState.release();
    m_players[playerId] = std::move(player);

    printf("[NetManager] remote player %u (\"%s\", skin=%s) joined, pos=(%.1f,%.1f,%.1f)\n",
        playerId, name.c_str(), skinName.c_str(), px, py, pz);
}

void NetManager::handlePlayerLeft(MemoryStream& payload) {
    uint16_t playerId = payload.readPod<uint16_t>();
    m_objManager.destroyObject(playerId);
    m_players.erase(playerId);

    printf("[NetManager] remote player %u left\n", playerId);
}

void NetManager::handlePlayerList(MemoryStream& payload) {
    uint16_t count = payload.readPod<uint16_t>();
    for (uint16_t i = 0; i < count; ++i) {
        uint16_t playerId = payload.readPod<uint16_t>();
        std::string name = payload.readString();
        float px = 0.0f, py = 500.0f, pz = 0.0f, pyaw = 0.0f;
        if (payload.remaining() >= sizeof(float) * 4) {
            px = payload.readPod<float>();
            py = payload.readPod<float>();
            pz = payload.readPod<float>();
            pyaw = payload.readPod<float>();
        }

        std::string skinName;
        if (payload.remaining() > 0) {
            skinName = payload.readString();
        }

        if (playerId == m_localPlayerId) continue;
        if (m_players.count(playerId)) continue;

        auto player = std::make_unique<NetPlayer>();
        player->playerId = playerId;
        player->playerName = name;
        player->skinName = skinName;
        player->peer = nullptr;
        player->netState->setOwner(player.get());
        player->updateCachedPosition(glm::vec3(px, py, pz));
        player->updateCachedLook(pyaw, 0.0f);

        m_objManager.addObject(playerId, std::move(player->netState));
        player->netState.release();
        m_players[playerId] = std::move(player);

        printf("[NetManager] player in list: %u (\"%s\", skin=%s) pos=(%.1f,%.1f,%.1f)\n",
            playerId, name.c_str(), skinName.c_str(), px, py, pz);
    }
}

void NetManager::handlePropertySyncClient(MemoryStream& payload) {
    if (payload.remaining() < sizeof(uint16_t)) return;
    uint16_t netObjId = payload.readPod<uint16_t>();

    NetObject* obj = m_objManager.findObject(netObjId);
    if (!obj) {
        // 未知对象（可能还未创建），静默跳过
        return;
    }

    obj->deserialize(payload);
    obj->clearDirty();  // 远程对象不需要再往外同步
}

void NetManager::handleChunkData(MemoryStream& payload) {
    m_chunkSync.onChunkData(payload.data(), payload.size());
}

void NetManager::handleChunkRequest(ENetPeer* peer, MemoryStream& payload) {
    if (!peer || payload.remaining() < 8) return;
    int32_t chunkX = payload.readPod<int32_t>();
    int32_t chunkZ = payload.readPod<int32_t>();
    printf("[NetManager] CHUNK_REQUEST for (%d,%d)\n", chunkX, chunkZ);
    m_chunkSync.handleChunkRequest(peer, chunkX, chunkZ);
}

void NetManager::handleChunkResponse(MemoryStream& payload) {
    // Same format as CHUNK_DATA
    handleChunkData(payload);
}

// ============================================================================
// 辅助方法
// ============================================================================

NetPlayer* NetManager::getLocalPlayer() {
    auto it = m_players.find(m_localPlayerId);
    return (it != m_players.end()) ? it->second.get() : nullptr;
}

NetPlayer* NetManager::getPlayer(uint16_t playerId) {
    auto it = m_players.find(playerId);
    return (it != m_players.end()) ? it->second.get() : nullptr;
}

std::string NetManager::getLocalSkinName() const {
    auto it = m_players.find(m_localPlayerId);
    if (it != m_players.end()) return it->second->skinName;
    return "steve";
}

void NetManager::sendToAll(ENetPeer* exclude, const NetMessage& msg, bool reliable) {
    std::vector<uint8_t> buf;
    msg.encode(buf);

    for (auto& [id, player] : m_players) {
        if (!player->peer) continue;
        if (player->peer == exclude) continue;
        if (reliable) {
            m_transport.sendReliable(player->peer, buf.data(), buf.size());
        } else {
            m_transport.sendUnreliable(player->peer, buf.data(), buf.size());
        }
    }
}

void NetManager::sendToAll(const NetMessage& msg, bool reliable) {
    sendToAll(nullptr, msg, reliable);
}

void NetManager::sendChunkData(ENetPeer* peer, const std::vector<uint8_t>& compressedData) {
    auto msg = NetMessage::chunkData(compressedData);
    std::vector<uint8_t> buf;
    msg.encode(buf);
    m_transport.sendReliable(peer, buf.data(), buf.size());
}

void NetManager::broadcastChunkData(const std::vector<uint8_t>& compressedData) {
    auto msg = NetMessage::chunkData(compressedData);
    std::vector<uint8_t> buf;
    msg.encode(buf);

    for (auto& [id, player] : m_players) {
        if (!player->peer) continue;
        m_transport.sendReliable(player->peer, buf.data(), buf.size());
    }
}

void NetManager::sendChunkRequest(int32_t chunkX, int32_t chunkZ) {
    if (m_isHost || !m_serverPeer) return;
    auto msg = NetMessage::chunkRequest(chunkX, chunkZ);
    std::vector<uint8_t> buf;
    msg.encode(buf);
    m_transport.sendReliable(m_serverPeer, buf.data(), buf.size());
    m_transport.flush();
}
