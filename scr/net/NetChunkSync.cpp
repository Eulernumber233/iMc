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
        // 去重：同一 chunk 可能 block-ready 与 loaded 各入队一次，待发队列里只留一份，
        // 真正序列化时优先取 loaded 版本（serializeChunk 内部走 getChunkAnyState）。
        if (m_pendingPushSet.insert(key).second) {
            m_pendingPush.push_back(p);
        }
    }

    auto& players = m_netManager->getPlayers();
    if (m_pendingPush.empty()) return;

    const int hostRadius = m_chunkManager->getRenderRadius();
    static constexpr size_t BATCH_SIZE_LIMIT = 16384;  // 16KB per batch

    // 本帧序列化预算：把多 worker 同时完成的晋升尖峰摊到多帧，避免单帧序列化+压缩
    // 过多 chunk 造成卡顿。本帧只取队首 budget 个，剩余下帧继续。
    static constexpr size_t MAX_PUSH_PER_FRAME = 8;
    size_t take = (std::min)(m_pendingPush.size(), MAX_PUSH_PER_FRAME);

    // 统计本帧每个候选 chunk 还有几个 peer "尚未满足"（既没 sent、且需要但本帧未发到）。
    // 仅当某 chunk 对所有当前 peer 都已满足（sent 或不需要）才出队；否则保留到下帧。
    // 这保留了旧版"每帧重扫"的语义：远处玩家的 block-ready chunk 会一直留在队列里，
    // 等玩家移近（落入其 pushR 半径）时才真正推送。
    std::unordered_map<ChunkKey, int> unsatisfied;  // key -> 仍未满足的 peer 数
    for (size_t i = 0; i < take; ++i) {
        unsatisfied[makeKey(m_pendingPush[i].x, m_pendingPush[i].y)] = 0;
    }

    // 每个 peer 各攒一个 batch，复用原有 16KB 合批逻辑。
    for (auto& [id, player] : players) {
        if (!player->peer) continue;  // 本地玩家

        auto& sent = m_sentChunks[player->peer];

        glm::vec3 ppos = player->getRenderPosition();
        glm::ivec2 pChunk(
            (int)std::floor(ppos.x / (float)ChunkConstants::CHUNK_WIDTH),
            (int)std::floor(ppos.z / (float)ChunkConstants::CHUNK_DEPTH));
        int pushR = (player->renderRadius > 0) ? player->renderRadius : hostRadius;

        std::vector<uint8_t> batch;
        auto flushIfOver = [&](size_t before) {
            if (batch.size() > BATCH_SIZE_LIMIT && before > 0) {
                size_t added = batch.size() - before;
                std::vector<uint8_t> lastChunk(batch.end() - added, batch.end());
                batch.resize(before);
                if (!batch.empty()) m_netManager->sendChunkData(player->peer, batch);
                batch = std::move(lastChunk);
            }
        };

        for (size_t i = 0; i < take; ++i) {
            const glm::ivec2& pos = m_pendingPush[i];
            ChunkKey key = makeKey(pos.x, pos.y);
            if (sent.count(key)) continue;  // 该 peer 已满足（已推送过）

            // loaded 优先（数据最新、含玩家/网络改动），否则用 block-ready（带相关性过滤）
            if (m_chunkManager->getChunkAnyState(pos) != nullptr) {
                size_t before = batch.size();
                serializeChunk(pos.x, pos.y, batch);
                flushIfOver(before);
                sent.insert(key);  // 该 peer 已满足
            } else {
                int dx = std::abs(pos.x - pChunk.x);
                int dz = std::abs(pos.y - pChunk.y);
                if (dx > pushR || dz > pushR) {
                    unsatisfied[key] += 1;  // 该 peer 暂不需要，但仍未满足 → chunk 不能出队
                    continue;
                }
                ChunkBoxes boxes;
                if (!m_chunkManager->getChunkBoxes(pos, boxes)) {
                    unsatisfied[key] += 1;  // 数据还没就绪，下帧重试
                    continue;
                }
                size_t before = batch.size();
                serializeChunkFromBlocks(pos.x, pos.y, boxes, batch);
                flushIfOver(before);
                sent.insert(key);  // 该 peer 已满足
            }
        }

        if (!batch.empty()) m_netManager->sendChunkData(player->peer, batch);
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

    auto positions = m_chunkManager->getLoadedChunkPositions();

    // 按 Chebyshev 距离排序（从近到远），保证空间局部性
    std::sort(positions.begin(), positions.end(),
        [](const glm::ivec2& a, const glm::ivec2& b) {
            int da = (std::max)(std::abs(a.x), std::abs(a.y));
            int db = (std::max)(std::abs(b.x), std::abs(b.y));
            return da < db;
        });

    std::vector<uint8_t> batch;
    static constexpr size_t BATCH_SIZE_LIMIT = 16384;

    int count = 0;
    for (auto& pos : positions) {
        ChunkKey key = makeKey(pos.x, pos.y);

        size_t before = batch.size();
        serializeChunk(pos.x, pos.y, batch);

        if (batch.size() > BATCH_SIZE_LIMIT && before > 0) {
            size_t added = batch.size() - before;
            std::vector<uint8_t> lastChunk(batch.end() - added, batch.end());
            batch.resize(before);

            if (!batch.empty()) {
                m_netManager->sendChunkData(peer, batch);
            }
            batch = std::move(lastChunk);
        }

        sent.insert(key);
        ++count;
    }

    if (!batch.empty()) {
        m_netManager->sendChunkData(peer, batch);
    }

    printf("[NetChunkSync] pushAllChunks: sent %d chunks to peer\n", count);
}

void NetChunkSync::handleChunkRequest(ENetPeer* peer, int chunkX, int chunkZ) {
    if (!m_chunkManager || !m_netManager || !peer) return;

    glm::ivec2 pos(chunkX, chunkZ);

    // 优先从 loaded chunk 序列化（有完整 mesh 的区块）
    Chunk* chunk = m_chunkManager->getChunkAnyState(pos);
    if (chunk) {
        std::vector<uint8_t> data;
        serializeChunk(chunkX, chunkZ, data);
        if (!data.empty()) {
            auto msg = NetMessage::chunkResponse(data);
            std::vector<uint8_t> buf;
            msg.encode(buf);
            m_netManager->getTransport().sendReliable(peer, buf.data(), buf.size());
            ChunkKey key = makeKey(chunkX, chunkZ);
            m_sentChunks[peer].insert(key);
            printf("[NetChunkSync] sent CHUNK_RESPONSE for (%d,%d), %zu bytes\n",
                chunkX, chunkZ, data.size());
        }
        m_netManager->getTransport().flush();
        return;
    }

    // 尝试从 block-ready 数据序列化（有方块数据但还没有 mesh）
    ChunkBoxes boxes;
    if (m_chunkManager->getChunkBoxes(pos, boxes)) {
        std::vector<uint8_t> data;
        serializeChunkFromBlocks(chunkX, chunkZ, boxes, data);
        if (!data.empty()) {
            auto msg = NetMessage::chunkResponse(data);
            std::vector<uint8_t> buf;
            msg.encode(buf);
            m_netManager->getTransport().sendReliable(peer, buf.data(), buf.size());
            ChunkKey key = makeKey(chunkX, chunkZ);
            m_sentChunks[peer].insert(key);
            printf("[NetChunkSync] sent CHUNK_RESPONSE (from block-ready) for (%d,%d), %zu bytes\n",
                chunkX, chunkZ, data.size());
        }
        m_netManager->getTransport().flush();
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

        // 区块数据已就绪（在 m_blockReady 或 m_loadedChunks 中），序列化并发送
        std::vector<uint8_t> data;
        serializeChunkFromBlocks(cx, cz, boxes, data);
        if (data.empty()) continue;

        auto msg = NetMessage::chunkResponse(data);
        std::vector<uint8_t> buf;
        msg.encode(buf);

        for (ENetPeer* peer : peers) {
            if (!peer) continue;
            m_netManager->getTransport().sendReliable(peer, buf.data(), buf.size());
            m_sentChunks[peer].insert(key);
        }
        m_netManager->getTransport().flush();

        printf("[NetChunkSync] pollPending: sent CHUNK_RESPONSE for (%d,%d) to %zu peers\n",
            cx, cz, peers.size());

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
}

void NetChunkSync::onChunkUnloaded(int chunkX, int chunkZ) {
    ChunkKey key = makeKey(chunkX, chunkZ);
    for (auto& [peer, sent] : m_sentChunks) {
        sent.erase(key);
    }
}

void NetChunkSync::serializeChunk(int chunkX, int chunkZ, std::vector<uint8_t>& out) {
    // 格式: [chunkX:int32][chunkZ:int32][numSections:uint8][sections...]
    // section: [sectionY:uint8][flags:uint8][dataLen:uint16][blocks (lz4)]

    // 记录 numSections 的位置，后面回填
    size_t headerPos = out.size();
    out.push_back(static_cast<uint8_t>(chunkX & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>(chunkZ & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 24) & 0xFF));

    size_t numSectionsPos = out.size();
    out.push_back(0);  // placeholder for numSections
    uint8_t numSections = 0;

    Chunk* chunk = m_chunkManager->getChunkAnyState(glm::ivec2(chunkX, chunkZ));
    if (!chunk) {
        out[numSectionsPos] = 0;
        return;
    }

    static constexpr int SECTION_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;
    static constexpr int BLOCK_SIZE = Section::VOLUME * sizeof(BlockState);  // 8192
    static constexpr int MAX_COMPRESSED = LZ4_COMPRESSBOUND(BLOCK_SIZE);

    std::vector<BlockState> blockBuf(BLOCK_SIZE / sizeof(BlockState));
    std::vector<uint8_t> compressBuf(MAX_COMPRESSED);

    for (int sy = 0; sy < SECTION_COUNT; ++sy) {
        Section& sec = chunk->getSection(sy);
        if (sec.isEmpty()) {
            // 全空气 section: 2 字节 (sectionY + flags=0)
            out.push_back(static_cast<uint8_t>(sy));
            out.push_back(0);  // flags: no data
            ++numSections;
            continue;
        }

        // 读取方块数据
        sec.readAllBlocks(blockBuf);

        // LZ4 压缩
        int srcSize = static_cast<int>(blockBuf.size() * sizeof(BlockState));
        int compressedSize = LZ4_compress_default(
            reinterpret_cast<const char*>(blockBuf.data()),
            reinterpret_cast<char*>(compressBuf.data()),
            srcSize,
            static_cast<int>(compressBuf.size()));

        if (compressedSize <= 0 || compressedSize > 0xFFFF) {
            // 压缩失败或太大，跳过此 section
            fprintf(stderr, "[NetChunkSync] LZ4 compress failed for (%d,%d) sy=%d\n",
                chunkX, chunkZ, sy);
            continue;
        }

        // 写入 section header + compressed data
        out.push_back(static_cast<uint8_t>(sy));
        out.push_back(ChunkSyncFormat::FLAG_HAS_DATA);
        uint16_t dataLen = static_cast<uint16_t>(compressedSize);
        out.push_back(static_cast<uint8_t>(dataLen & 0xFF));
        out.push_back(static_cast<uint8_t>((dataLen >> 8) & 0xFF));
        out.insert(out.end(), compressBuf.data(),
                   compressBuf.data() + compressedSize);

        ++numSections;
    }

    out[numSectionsPos] = numSections;
}

void NetChunkSync::serializeChunkFromBlocks(int chunkX, int chunkZ,
                                             const ChunkBoxes& boxes,
                                             std::vector<uint8_t>& out) {
    // 与 serializeChunk 相同的 header 格式
    size_t headerPos = out.size();
    out.push_back(static_cast<uint8_t>(chunkX & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkX >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>(chunkZ & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((chunkZ >> 24) & 0xFF));

    size_t numSectionsPos = out.size();
    out.push_back(0);
    uint8_t numSections = 0;

    static constexpr int SECTION_COUNT = ChunkConstants::CHUNK_HEIGHT / Section::HEIGHT;
    static constexpr int SEC_VOL = Section::VOLUME;
    static constexpr int BLOCK_SIZE = SEC_VOL * sizeof(BlockState);
    static constexpr int MAX_COMPRESSED = LZ4_COMPRESSBOUND(BLOCK_SIZE);

    std::vector<uint8_t> compressBuf(MAX_COMPRESSED);

    for (int sy = 0; sy < SECTION_COUNT; ++sy) {
        if (!boxes[sy]) {
            // 该 section 无数据，按全空气处理
            out.push_back(static_cast<uint8_t>(sy));
            out.push_back(0);
            ++numSections;
            continue;
        }
        // 持读锁拷一份 section 数据（与玩家修改互斥）
        std::array<BlockState, SEC_VOL> snapshot;
        {
            std::shared_lock<std::shared_mutex> lk(boxes[sy]->mutex);
            snapshot = boxes[sy]->blocks;
        }
        const BlockState* sectionBlocks = snapshot.data();

        // 检测全空气 section
        bool allAir = true;
        for (int i = 0; i < SEC_VOL; ++i) {
            if (sectionBlocks[i].type() != BLOCK_AIR) {
                allAir = false;
                break;
            }
        }

        if (allAir) {
            out.push_back(static_cast<uint8_t>(sy));
            out.push_back(0);
            ++numSections;
            continue;
        }

        int srcSize = static_cast<int>(SEC_VOL * sizeof(BlockState));
        int compressedSize = LZ4_compress_default(
            reinterpret_cast<const char*>(sectionBlocks),
            reinterpret_cast<char*>(compressBuf.data()),
            srcSize,
            static_cast<int>(compressBuf.size()));

        if (compressedSize <= 0 || compressedSize > 0xFFFF) {
            fprintf(stderr, "[NetChunkSync] LZ4 compress failed for (%d,%d) sy=%d (block-data)\n",
                chunkX, chunkZ, sy);
            continue;
        }

        out.push_back(static_cast<uint8_t>(sy));
        out.push_back(ChunkSyncFormat::FLAG_HAS_DATA);
        uint16_t dataLen = static_cast<uint16_t>(compressedSize);
        out.push_back(static_cast<uint8_t>(dataLen & 0xFF));
        out.push_back(static_cast<uint8_t>((dataLen >> 8) & 0xFF));
        out.insert(out.end(), compressBuf.data(), compressBuf.data() + compressedSize);

        ++numSections;
    }

    out[numSectionsPos] = numSections;
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
