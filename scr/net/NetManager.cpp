#include "NetManager.h"
#include "../mode/SkinManager.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/BlockType.h"
#include "../RuntimeConfig.h"
#include "../Profiler.h"
#include <cstdio>
#include <chrono>
#include <thread>
#include <algorithm>

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
    // 启动专用网络线程：之后所有 enet 收发都在该线程，主线程经队列通信。
    m_transport.startNetThread();

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

    // 阶段 2：启动网络线程后，握手通过入站队列 drainInbound 轮询完成（不再主线程直接 service）。
    // enet_host_connect 已在主线程（线程启动前）调过；连接的真正建立（CONNECT 事件）由网络线程
    // 收到并入队。注意：enet_peer_send 在 peer 处于 CONNECTING 状态会被丢弃，故 JOIN_REQUEST
    // 必须在收到 Connected 入站事件之后再发。
    m_transport.startNetThread();

    {
        using namespace std::chrono;
        bool gotConnect = false;
        bool gotAccept = false;
        bool joinRequestSent = false;
        auto startTime = steady_clock::now();
        auto lastLog = startTime;
        std::vector<NetEvent> events;

        auto failExit = [&]() {
            m_transport.stopNetThread();
            m_transport.destroyHost();
            m_transport.shutdown();
        };

        while (duration_cast<milliseconds>(steady_clock::now() - startTime).count() < 10000) {
            events.clear();
            m_transport.drainInbound(events);

            for (auto& ev : events) {
                // JOIN_ACCEPT 之后、同批次里剩余的事件（PLAYER_LIST / PLAYER_JOINED 等）不能丢——
                // drainInbound 一次取走整批，旧实现在收到 ACCEPT 后 break 会丢弃它们，漏掉
                // PLAYER_LIST 就导致客户端看不到其他玩家（随机复现）。但此刻 ChunkManager 尚未
                // 设置，不能立即处理含 chunk 的消息，故统一暂存，交主循环首次 dispatchEvents 消费。
                if (gotAccept) {
                    m_deferredInbound.push_back(std::move(ev));
                    continue;
                }

                if (ev.type == NetEvent::Connected) {
                    gotConnect = true;
                    printf("[NetManager] transport connected (elapsed=%lldms)\n",
                        duration_cast<milliseconds>(steady_clock::now() - startTime).count());

                    // peer 现在 CONNECTED，发送 JOIN_REQUEST（带本机渲染半径）。走出站队列。
                    uint16_t myRadius = (uint16_t)RuntimeConfig::get().renderRadius;
                    auto joinReq = NetMessage::joinRequest(playerName, myRadius);
                    std::vector<uint8_t> joinBuf;
                    joinReq.encode(joinBuf);
                    m_transport.sendReliable(serverPeer, joinBuf.data(), joinBuf.size());
                    m_transport.flush();
                    joinRequestSent = true;
                    printf("[NetManager] JOIN_REQUEST sent (elapsed=%lldms)\n",
                        duration_cast<milliseconds>(steady_clock::now() - startTime).count());
                } else if (ev.type == NetEvent::Data) {
                    NetMessage netMsg;
                    if (NetMessage::decode(ev.data.data(), ev.data.size(), netMsg)) {
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
                            failExit();
                            return false;
                        }
                    }
                } else if (ev.type == NetEvent::Disconnected) {
                    printf("[NetManager] disconnected before accept (elapsed=%lldms)\n",
                        duration_cast<milliseconds>(steady_clock::now() - startTime).count());
                    if (!gotConnect && !gotAccept) {
                        printf("[NetManager] connection refused or timed out\n");
                    }
                    failExit();
                    return false;
                }
            }
            // 本批处理完毕：若已 ACCEPT（且已 dispatch 完同批剩余事件）则结束握手。
            if (gotAccept) break;

            // 每 2 秒输出一次等待状态
            auto now = steady_clock::now();
            if (duration_cast<milliseconds>(now - lastLog).count() > 2000) {
                printf("[NetManager] still waiting for handshake... (elapsed=%lldms, gotConnect=%d, joinReqSent=%d)\n",
                    duration_cast<milliseconds>(now - startTime).count(),
                    gotConnect ? 1 : 0, joinRequestSent ? 1 : 0);
                lastLog = now;
            }

            // 让网络线程有时间收发，避免主线程空转 drainInbound
            std::this_thread::sleep_for(milliseconds(5));
        }
        if (!gotAccept) {
            auto elapsed = duration_cast<milliseconds>(steady_clock::now() - startTime).count();
            fprintf(stderr, "[NetManager] join handshake timed out (%lldms, gotConnect=%d, joinReqSent=%d)\n",
                elapsed, gotConnect ? 1 : 0, joinRequestSent ? 1 : 0);
            failExit();
            return false;
        }
    }

    m_connected = true;
    return true;
}

void NetManager::leave() {
    if (!m_connected) return;

    // 停止序列化线程：join 之前不能销毁 host / 清空容器，否则在途 result 取回时悬空。
    // worker 本身不碰 ENet（只产 payload），故顺序上先 stop 最安全。
    m_chunkSync.shutdown();

    // 阶段 2：先停网络线程，之后 ENetHost 由主线程独占，disconnectAll/destroyHost 才安全。
    m_transport.stopNetThread();

    if (m_isHost) {
        // 通知所有客户端断开（线程已停，主线程直接发 + flush，同步推出 disconnect 包）
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

    PROFILE_SCOPE("net.update");

    // 1. 轮询事件
    { PROFILE_SCOPE("net.dispatch"); dispatchEvents(); }

    // 2. 地形同步
    if (m_isHost) {
        PROFILE_SCOPE("net.pushChunks");
        m_chunkSync.pushChunks();
    }

    // 2b. 取回序列化线程完成的 chunk payload 并发送（主线程只做 enet_peer_send）。
    //     服务端才有 chunk 推送；客户端 worker 始终空闲，drain 立即返回。
    if (m_isHost) {
        PROFILE_SCOPE("net.pollSerialize");
        m_chunkSync.pollSerializeResults();
    }

    // 3. 收集脏属性并同步
    if (m_isHost) {
        PROFILE_SCOPE("net.propSync");
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
        PROFILE_SCOPE("net.propSync");
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

void NetManager::updateSerializeThreadCount() {
    if (!m_isHost) return;
    // 远程客户端数 = 有 peer 的玩家数（排除 host 本地玩家，其 peer 为 nullptr）。
    int clientCount = 0;
    for (auto& [id, p] : m_players) {
        if (p->peer) ++clientCount;
    }
    // 线程数 = max(1, clientCount)，由 NetSerializeWorker 内部夹到 [1, maxThreads()]。
    m_chunkSync.setSerializeThreadCount((std::max)(1, clientCount));
}

void NetManager::dispatchEvents() {
    // 阶段 2：网络线程已把事件解析好放进入站队列，主线程在此取走 dispatch（不再直接碰 ENet）。
    std::vector<NetEvent> events;
    // 先消费握手期暂存的入站事件（JOIN_ACCEPT 同批次里 PLAYER_LIST 等），再取本帧新事件，保序。
    if (!m_deferredInbound.empty()) {
        events.swap(m_deferredInbound);
    }
    m_transport.drainInbound(events);

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
                    // 客户端减少 → 缩小序列化线程池
                    updateSerializeThreadCount();
                }
            }
            break;
        }

        case NetEvent::Data: {
            NetMessage msg;
            if (!NetMessage::decode(ev.data.data(), ev.data.size(), msg)) break;
            dispatchMessage(ev.peer, msg);
            break;
        }

        default:
            break;
        }
    }
}

void NetManager::dispatchMessage(ENetPeer* peer, NetMessage& msg) {
    // 直接读 msg.payload —— decode 已把包字节拷进它且读游标在 0，无需再拷一份 MemoryStream。
    // 高频不可靠位置包尤其受益（每客户端每帧一个）。
    MemoryStream& payload = msg.payload;

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
            handleChunkData(payload);
        }
        break;

    case NetMsgType::CHUNK_REQUEST:
        if (m_isHost) handleChunkRequest(peer, payload);
        break;

    case NetMsgType::BLOCK_CHANGE:
        if (m_isHost) handleBlockChangeServer(peer, payload);
        else          handleBlockChangeClient(payload);
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

    // 读取客户端上报的渲染半径（旧客户端无此字段 → 用默认），并夹上限防内存爆
    int reportedRadius = NetConstants::DEFAULT_CLIENT_RENDER_RADIUS;
    if (payload.remaining() >= sizeof(uint16_t)) {
        uint16_t r = payload.readPod<uint16_t>();
        if (r > 0) reportedRadius = r;
    }
    if (reportedRadius > NetConstants::MAX_CLIENT_RENDER_RADIUS)
        reportedRadius = NetConstants::MAX_CLIENT_RENDER_RADIUS;

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
    player->renderRadius = reportedRadius;
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

    // 客户端增加 → 先扩大序列化线程池，再投递全量推送（pushAllChunks 会一次提交几百个 job）。
    updateSerializeThreadCount();

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

void NetManager::setChunkManager(ChunkManager* cm) {
    m_chunkManager = cm;
    m_chunkSync.init(cm, this);

    // 服务端：chunk 卸载时清除 per-peer 已推送记录，使玩家再次靠近能重新收到最新数据。
    if (m_isHost && cm) {
        cm->setChunkUnloadedCallback([this](int cx, int cz) {
            m_chunkSync.onChunkUnloaded(cx, cz);
        });
    }
}

// ============================================================================
// 方块修改同步
// ============================================================================

static inline void worldToChunkXZ(int32_t wx, int32_t wz, int& cx, int& cz) {
    constexpr int W = 16;  // Chunk::WIDTH / DEPTH
    cx = (wx >= 0) ? (wx / W) : -(((-wx) + W - 1) / W);
    cz = (wz >= 0) ? (wz / W) : -(((-wz) + W - 1) / W);
}

void NetManager::requestBlockChange(int32_t worldX, int32_t worldY, int32_t worldZ,
                                    uint16_t blockStateBits) {
    if (!m_connected) return;

    if (m_isHost) {
        // 服务端权威：直接应用到本地权威数据 + 广播给相关客户端（含 Host 自己已生效）
        if (m_chunkManager) {
            m_chunkManager->applyBlockChange(glm::ivec3(worldX, worldY, worldZ),
                                             BlockState(blockStateBits));
        }
        auto msg = NetMessage::blockChange(worldX, worldY, worldZ, blockStateBits);
        std::vector<uint8_t> buf;
        msg.encode(buf);
        int cx, cz;
        worldToChunkXZ(worldX, worldZ, cx, cz);
        m_chunkSync.broadcastBlockChange(cx, cz, buf);
    } else if (m_serverPeer) {
        // 客户端：只发请求，不本地应用，等服务端广播回来
        auto msg = NetMessage::blockChange(worldX, worldY, worldZ, blockStateBits);
        std::vector<uint8_t> buf;
        msg.encode(buf);
        m_transport.sendReliable(m_serverPeer, buf.data(), buf.size());
        m_transport.flush();
    }
}

void NetManager::handleBlockChangeServer(ENetPeer* peer, MemoryStream& payload) {
    if (payload.remaining() < sizeof(int32_t) * 3 + sizeof(uint16_t)) return;
    int32_t wx = payload.readPod<int32_t>();
    int32_t wy = payload.readPod<int32_t>();
    int32_t wz = payload.readPod<int32_t>();
    uint16_t bits = payload.readPod<uint16_t>();

    // TODO(校验): 距离/权限/冷却。当前局域网受信环境，先直接应用。
    if (m_chunkManager) {
        bool applied = m_chunkManager->applyBlockChange(
            glm::ivec3(wx, wy, wz), BlockState(bits));
        if (!applied) {
            // 该 chunk 服务端未加载——理论上不该发生（客户端只改自己加载的、
            // 即服务端推送过的 chunk）。忽略。
            return;
        }
    }

    // 广播给所有相关客户端（含发起者，使其通过统一广播路径生效）
    auto msg = NetMessage::blockChange(wx, wy, wz, bits);
    std::vector<uint8_t> buf;
    msg.encode(buf);
    int cx, cz;
    worldToChunkXZ(wx, wz, cx, cz);
    m_chunkSync.broadcastBlockChange(cx, cz, buf);
}

void NetManager::handleBlockChangeClient(MemoryStream& payload) {
    if (payload.remaining() < sizeof(int32_t) * 3 + sizeof(uint16_t)) return;
    int32_t wx = payload.readPod<int32_t>();
    int32_t wy = payload.readPod<int32_t>();
    int32_t wz = payload.readPod<int32_t>();
    uint16_t bits = payload.readPod<uint16_t>();

    if (m_chunkManager) {
        m_chunkManager->applyBlockChange(glm::ivec3(wx, wy, wz), BlockState(bits));
    }
}
