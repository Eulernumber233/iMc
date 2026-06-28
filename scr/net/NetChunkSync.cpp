#include "NetChunkSync.h"
#include "NetManager.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/Chunk.h"
#include "../chunk/Section.h"
#include "lz4.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <array>
#include <shared_mutex>

void NetChunkSync::init(ChunkManager* cm, NetManager* net) {
    m_chunkManager = cm;
    m_netManager = net;
    // 启动专用序列化线程（仅服务端真正用到，但客户端启动也无害——它不投任务）
    if (!m_serializeWorker.isRunning()) {
        m_serializeWorker.start();
    }
}

void NetChunkSync::shutdown() {
    m_serializeWorker.stop();
    m_pendingPush.clear();
    m_pendingPushSet.clear();
    m_pendingRequests.clear();
    m_sentChunks.clear();
    m_alivePeers.clear();
}

void NetChunkSync::submitSerializeJob(int chunkX, int chunkZ, const ChunkBoxes& boxes,
                                      std::vector<ENetPeer*> targets) {
    if (targets.empty()) return;
    NetSerializeWorker::Job job;
    job.chunkX = chunkX;
    job.chunkZ = chunkZ;
    job.boxes = boxes;                 // 拷 shared_ptr 数组，引用计数 +1 保活
    job.targets = std::move(targets);
    // 记录目标 peer 为存活（投递时它们都有效）
    for (ENetPeer* p : job.targets) {
        if (p) m_alivePeers.insert(p);
    }
    m_serializeWorker.submit(std::move(job));
}

void NetChunkSync::pollSerializeResults() {
    if (!m_netManager) return;

    // 限流：每帧最多取回 N 个，避免 join 突发一次回灌几百个 result 时主线程 send 抖动。
    static constexpr size_t MAX_RESULTS_PER_FRAME = 16;
    std::vector<NetSerializeWorker::Result> results;
    m_serializeWorker.drainResults(results, MAX_RESULTS_PER_FRAME);
    if (results.empty()) return;

    bool sentAny = false;
    for (auto& res : results) {
        if (res.payload.empty()) continue;
        // 包一层 CHUNK_DATA header（轻量，不压缩）
        auto msg = NetMessage::chunkData(res.payload);
        std::vector<uint8_t> buf;
        msg.encode(buf);

        for (ENetPeer* peer : res.targets) {
            if (!peer) continue;
            // peer 失效复核：result 取回时该 peer 可能已断开
            if (m_alivePeers.find(peer) == m_alivePeers.end()) continue;
            m_netManager->getTransport().sendReliable(peer, buf.data(), buf.size());
            sentAny = true;
        }
    }
    if (sentAny) m_netManager->getTransport().flush();
}

// ============================================================================
// 服务端
// ============================================================================

void NetChunkSync::pushChunks() {
    if (!m_chunkManager || !m_netManager) return;
    if (!m_netManager->isHosting()) return;

    // 先处理等待中的客户端请求：区块数据就绪后立即响应
    pollPendingRequests();

    // 取走本帧新晋升（BLOCK_READY / LOADED）的 chunk 事件。
    // 替代过去每帧全量扫描 getLoadedChunkPositions()/getBlockReadyChunkPositions()
    // （render_radius=16 时每帧上千个坐标的拷贝+遍历，是稳态掉帧主因）。
    // 这里把事件并进一个持久待发队列 m_pendingPush：因为一个晋升事件需要
    // 发给"所有当前 + 未来加入"的 peer，单帧若没有 peer 在线就先攒着。
    std::vector<glm::ivec2> promoted;
    m_chunkManager->drainPromotedChunks(promoted);
    for (auto& p : promoted) {
        ChunkKey key = makeKey(p.x, p.y);
        // 去重：同一 chunk 可能 block-ready 与 loaded 各入队一次，待发队列里只留一份。
        // 真正投递序列化时由 getChunkBoxes 统一取 section box（loaded/block-ready 均可）。
        if (m_pendingPushSet.insert(key).second) {
            m_pendingPush.push_back(p);
        }
    }

    auto& players = m_netManager->getPlayers();
    if (m_pendingPush.empty()) return;

    const int hostRadius = m_chunkManager->getRenderRadius();

    // 本帧投递预算：把多 worker 同时完成的晋升尖峰摊到多帧。序列化已卸载到专用线程，
    // 主线程这里只算相关性 + 拷 ChunkBoxes（shared_ptr）+ 投递，开销很低；预算主要
    // 用于限制 worker 队列与每帧主线程拷贝量。本帧只取队首 budget 个，剩余下帧继续。
    static constexpr size_t MAX_PUSH_PER_FRAME = 8;
    size_t take = (std::min)(m_pendingPush.size(), MAX_PUSH_PER_FRAME);

    // 预算内的候选 chunk：逐个算「哪些 peer 需要它且还没 sent」，攒成 targets 一次投递。
    // 仅当某 chunk 对所有当前 peer 都已满足（sent 或不需要）才出队；否则保留到下帧。
    // 这保留了旧版语义：远处玩家的 block-ready chunk 会一直留在队列里，等玩家移近时才推。
    std::unordered_map<ChunkKey, int> unsatisfied;  // key -> 仍未满足的 peer 数
    for (size_t i = 0; i < take; ++i) {
        unsatisfied[makeKey(m_pendingPush[i].x, m_pendingPush[i].y)] = 0;
    }

    for (size_t i = 0; i < take; ++i) {
        const glm::ivec2& pos = m_pendingPush[i];
        ChunkKey key = makeKey(pos.x, pos.y);

        // 先取一次 boxes（loaded 或 block-ready 均可，getChunkBoxes 统一返回 section box）。
        // 取不到说明数据还没就绪，整 chunk 留到下帧。
        ChunkBoxes boxes;
        bool haveBoxes = m_chunkManager->getChunkBoxes(pos, boxes);
        bool isLoaded = (m_chunkManager->getChunkAnyState(pos) != nullptr);

        std::vector<ENetPeer*> targets;
        for (auto& [id, player] : players) {
            if (!player->peer) continue;  // 本地玩家
            auto& sent = m_sentChunks[player->peer];
            if (sent.count(key)) continue;  // 该 peer 已满足

            if (isLoaded) {
                // loaded chunk：所有 peer 都该收到最新数据，无半径过滤
                if (!haveBoxes) { unsatisfied[key] += 1; continue; }
                targets.push_back(player->peer);
                sent.insert(key);  // 乐观标记：已投递，避免下帧重投
            } else {
                // block-ready chunk：带相关性过滤，仅推给落入该玩家半径的
                glm::vec3 ppos = player->getRenderPosition();
                glm::ivec2 pChunk(
                    (int)std::floor(ppos.x / (float)ChunkConstants::CHUNK_WIDTH),
                    (int)std::floor(ppos.z / (float)ChunkConstants::CHUNK_DEPTH));
                int pushR = (player->renderRadius > 0) ? player->renderRadius : hostRadius;
                int dx = std::abs(pos.x - pChunk.x);
                int dz = std::abs(pos.y - pChunk.y);
                if (dx > pushR || dz > pushR) {
                    unsatisfied[key] += 1;  // 该 peer 暂不需要，但仍未满足 → chunk 不出队
                    continue;
                }
                if (!haveBoxes) { unsatisfied[key] += 1; continue; }
                targets.push_back(player->peer);
                sent.insert(key);  // 乐观标记
            }
        }

        if (!targets.empty()) {
            submitSerializeJob(pos.x, pos.y, boxes, std::move(targets));
        }
    }

    // 重建待发队列：take 范围内 unsatisfied==0（所有 peer 都满足）的出队；
    // 其余（仍有 peer 未满足）以及 take 之外的，原样保留到下帧。
    std::vector<glm::ivec2> stillPending;
    stillPending.reserve(m_pendingPush.size());
    for (size_t i = 0; i < m_pendingPush.size(); ++i) {
        const glm::ivec2& pos = m_pendingPush[i];
        ChunkKey key = makeKey(pos.x, pos.y);
        if (i < take && unsatisfied[key] == 0) {
            m_pendingPushSet.erase(key);
        } else {
            stillPending.push_back(pos);
        }
    }
    m_pendingPush.swap(stillPending);

    // 没有任何 peer 在线时，doneThisFrame 为空，所有事件原样保留（攒着等 peer），
    // 但为避免无 peer 时队列无限增长，这里在确实没有远程 peer 时直接清空：
    // 新 peer 加入会通过 pushAllChunks 收到全量快照，无需保留这些增量事件。
    bool anyRemotePeer = false;
    for (auto& [id, player] : players) {
        if (player->peer) { anyRemotePeer = true; break; }
    }
    if (!anyRemotePeer) {
        m_pendingPush.clear();
        m_pendingPushSet.clear();
    }
}

void NetChunkSync::pushAllChunks(ENetPeer* peer) {
    if (!m_chunkManager || !m_netManager || !peer) return;

    auto& sent = m_sentChunks[peer];
    sent.clear();  // 新玩家加入，重置 sent set
    m_alivePeers.insert(peer);

    auto positions = m_chunkManager->getLoadedChunkPositions();

    // 按 Chebyshev 距离排序（从近到远），保证空间局部性：近处 chunk 先投递、先序列化、
    // 先到达客户端，玩家周围最先成形。
    std::sort(positions.begin(), positions.end(),
        [](const glm::ivec2& a, const glm::ivec2& b) {
            int da = (std::max)(std::abs(a.x), std::abs(a.y));
            int db = (std::max)(std::abs(b.x), std::abs(b.y));
            return da < db;
        });

    // 关键：不再同步序列化 + 压缩（旧版会卡主线程 100-500ms）。改为逐 chunk 投递到
    // 序列化线程，主线程立即返回。worker 慢慢做，主线程每帧 pollSerializeResults 取回发送。
    int count = 0;
    for (auto& pos : positions) {
        ChunkKey key = makeKey(pos.x, pos.y);
        ChunkBoxes boxes;
        if (!m_chunkManager->getChunkBoxes(pos, boxes)) continue;
        submitSerializeJob(pos.x, pos.y, boxes, { peer });
        sent.insert(key);  // 乐观标记
        ++count;
    }

    printf("[NetChunkSync] pushAllChunks: queued %d chunks for serialization (peer)\n", count);
}

void NetChunkSync::handleChunkRequest(ENetPeer* peer, int chunkX, int chunkZ) {
    if (!m_chunkManager || !m_netManager || !peer) return;

    glm::ivec2 pos(chunkX, chunkZ);

    // 有方块数据（loaded 或 block-ready 均可，getChunkBoxes 统一取 section box）：
    // 投递到序列化线程，主线程 pollSerializeResults 取回发送。不再同步序列化阻塞。
    ChunkBoxes boxes;
    if (m_chunkManager->getChunkBoxes(pos, boxes)) {
        submitSerializeJob(chunkX, chunkZ, boxes, { peer });
        m_sentChunks[peer].insert(makeKey(chunkX, chunkZ));  // 乐观标记
        printf("[NetChunkSync] queued CHUNK_REQUEST (%d,%d) for serialization\n",
            chunkX, chunkZ);
        return;
    }

    // 区块不存在：记录 pending 请求，然后投递 Task 1（仅生成方块数据）
    // forceChunkLoad 可能因为已 in-flight 而跳过投递，这没关系——
    // pollPendingRequests 会在区块数据就绪后轮询到并发送响应
    ChunkKey key = makeKey(chunkX, chunkZ);
    m_pendingRequests[key].push_back(peer);

    printf("[NetChunkSync] chunk (%d,%d) not found, force-generating + 4 neighbors, pending=%zu\n",
        chunkX, chunkZ, m_pendingRequests.size());
    m_chunkManager->forceChunkLoad(pos);
    static const glm::ivec2 nbOffsets[4] = {
        { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }
    };
    for (int d = 0; d < 4; ++d) {
        glm::ivec2 nbPos = pos + nbOffsets[d];
        if (!m_chunkManager->hasBlockData(nbPos))
            m_chunkManager->forceChunkLoad(nbPos);
    }
    m_netManager->getTransport().flush();
}

void NetChunkSync::pollPendingRequests() {
    if (m_pendingRequests.empty()) return;
    if (!m_chunkManager || !m_netManager) return;

    std::vector<ChunkKey> resolved;

    for (auto& [key, peers] : m_pendingRequests) {
        int32_t cx = static_cast<int32_t>(key >> 32);
        int32_t cz = static_cast<int32_t>(key & 0xFFFFFFFFLL);
        ChunkBoxes boxes;
        if (!m_chunkManager->getChunkBoxes(glm::ivec2(cx, cz), boxes)) continue;

        // 区块数据已就绪（在 m_blockReady 或 m_loadedChunks 中），投递到序列化线程。
        std::vector<ENetPeer*> targets;
        for (ENetPeer* peer : peers) {
            if (!peer) continue;
            targets.push_back(peer);
            m_sentChunks[peer].insert(key);  // 乐观标记
        }
        if (!targets.empty()) {
            submitSerializeJob(cx, cz, boxes, std::move(targets));
            printf("[NetChunkSync] pollPending: queued (%d,%d) for serialization to %zu peers\n",
                cx, cz, peers.size());
        }

        resolved.push_back(key);
    }

    for (auto& key : resolved) {
        m_pendingRequests.erase(key);
    }
}

void NetChunkSync::onPeerDisconnected(ENetPeer* peer) {
    if (!peer) return;

    // 从所有 pending 请求中移除该 peer
    for (auto it = m_pendingRequests.begin(); it != m_pendingRequests.end(); ) {
        auto& peers = it->second;
        peers.erase(std::remove(peers.begin(), peers.end(), peer), peers.end());
        if (peers.empty()) {
            it = m_pendingRequests.erase(it);
        } else {
            ++it;
        }
    }

    // 清理该 peer 的已推送记录
    m_sentChunks.erase(peer);

    // 从存活集合移除：序列化线程可能仍持有以此 peer 为 target 的在途 result，
    // pollSerializeResults 取回时会因 peer 不在 m_alivePeers 而跳过发送（§1.4 失效复核）。
    m_alivePeers.erase(peer);
}

void NetChunkSync::onChunkUnloaded(int chunkX, int chunkZ) {
    ChunkKey key = makeKey(chunkX, chunkZ);
    for (auto& [peer, sent] : m_sentChunks) {
        sent.erase(key);
    }
}

void NetChunkSync::broadcastBlockChange(int chunkX, int chunkZ,
                                        const std::vector<uint8_t>& encodedMsg) {
    if (!m_netManager) return;
    ChunkKey key = makeKey(chunkX, chunkZ);

    // 只发给"已经收到过该 chunk"的客户端（相关性过滤）。
    // 没加载该 chunk 的客户端将来 CHUNK_REQUEST 时拿到的已是改后的最新快照，无需补发。
    for (auto& [peer, sent] : m_sentChunks) {
        if (!peer) continue;
        if (sent.count(key) == 0) continue;
        m_netManager->getTransport().sendReliable(peer, encodedMsg.data(), encodedMsg.size());
    }
    m_netManager->getTransport().flush();
}

// ============================================================================
// 客户端
// ============================================================================

void NetChunkSync::onChunkData(const uint8_t* data, size_t len) {
    if (!data || len < 9) {
        fprintf(stderr, "[NetChunkSync] onChunkData: invalid data (len=%zu)\n", len);
        return;
    }
    deserializeAndImport(data, len);
}

void NetChunkSync::deserializeAndImport(const uint8_t* data, size_t len) {
    // 一个 CHUNK_DATA 消息可能包含多个拼接的 chunk（pushChunks 按 16KB 批次打包）。
    // 循环解析直到全部消费，避免批次中第 2 个及以后的 chunk 被静默丢弃。
    size_t pos = 0;
    int chunkCount = 0;

    while (pos + 9 <= len) {  // 至少需要 4+4+1 = 9 字节 header
        auto readI32 = [&]() -> int32_t {
            int32_t v = static_cast<int32_t>(data[pos])
                | (static_cast<int32_t>(data[pos + 1]) << 8)
                | (static_cast<int32_t>(data[pos + 2]) << 16)
                | (static_cast<int32_t>(data[pos + 3]) << 24);
            pos += 4;
            return v;
        };

        int chunkX = readI32();
        int chunkZ = readI32();
        if (pos >= len) break;
        uint8_t numSections = data[pos++];

        static constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
        static constexpr int BLOCK_SIZE = Section::VOLUME * sizeof(BlockState);
        auto blockBuf = std::make_unique<BlockState[]>(VOL);
        std::memset(blockBuf.get(), 0, VOL * sizeof(BlockState));  // 默认 AIR

        std::vector<uint8_t> decompressBuf(BLOCK_SIZE);
        bool parseOk = true;

        for (uint8_t i = 0; i < numSections && pos < len; ++i) {
            if (pos >= len) { parseOk = false; break; }
            uint8_t sy = data[pos++];

            if (pos >= len) { parseOk = false; break; }
            uint8_t flags = data[pos++];

            if ((flags & ChunkSyncFormat::FLAG_HAS_DATA) == 0) {
                continue;
            }

            if (pos + 2 > len) { parseOk = false; break; }
            uint16_t dataLen = static_cast<uint16_t>(data[pos])
                | (static_cast<uint16_t>(data[pos + 1]) << 8);
            pos += 2;

            if (pos + dataLen > len) { parseOk = false; break; }

            int decompressedSize = LZ4_decompress_safe(
                reinterpret_cast<const char*>(data + pos),
                reinterpret_cast<char*>(decompressBuf.data()),
                dataLen,
                BLOCK_SIZE);

            pos += dataLen;

            if (decompressedSize != BLOCK_SIZE) {
                fprintf(stderr, "[NetChunkSync] LZ4 decompress failed for (%d,%d) sy=%d: "
                    "expected %d, got %d\n", chunkX, chunkZ, sy, BLOCK_SIZE, decompressedSize);
                continue;
            }

            int sectionStart = sy * Section::VOLUME;
            std::memcpy(blockBuf.get() + sectionStart,
                decompressBuf.data(), BLOCK_SIZE);
        }

        if (parseOk && m_chunkManager) {
            m_chunkManager->importChunkData(chunkX, chunkZ, std::move(blockBuf));
            ++chunkCount;
        } else if (!m_chunkManager) {
            static int nullCmWarnCount = 0;
            if (nullCmWarnCount < 5) {
                fprintf(stderr, "[NetChunkSync] deserializeAndImport: m_chunkManager is null!\n");
                ++nullCmWarnCount;
            }
            break;
        }
        // 解析失败（数据损坏或截断）：跳过本轮，while 条件会阻止继续
    }
}
