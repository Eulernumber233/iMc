#include "NetChunkSync.h"
#include "NetManager.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/Chunk.h"
#include "../chunk/Section.h"
#include "lz4.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
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

    auto& players = m_netManager->getPlayers();

    // 对每个客户端：发送尚未推送过的 loaded chunk
    for (auto& [id, player] : players) {
        if (!player->peer) continue;  // 本地玩家

        auto& sent = m_sentChunks[player->peer];
        auto positions = m_chunkManager->getLoadedChunkPositions();

        std::vector<uint8_t> batch;
        static constexpr size_t BATCH_SIZE_LIMIT = 16384;  // 16KB per batch

        for (auto& pos : positions) {
            ChunkKey key = makeKey(pos.x, pos.y);
            if (sent.count(key)) continue;  // 已推送

            // 序列化此 chunk
            size_t before = batch.size();
            serializeChunk(pos.x, pos.y, batch);

            // 如果加入此 chunk 后超过批次大小限制，先发送当前批次
            if (batch.size() > BATCH_SIZE_LIMIT && before > 0) {
                // 回退未发送的 chunk 数据
                size_t added = batch.size() - before;
                std::vector<uint8_t> lastChunk(batch.end() - added, batch.end());
                batch.resize(before);

                if (!batch.empty()) {
                    m_netManager->sendChunkData(player->peer, batch);
                }
                batch = std::move(lastChunk);
            }

            sent.insert(key);
        }

        if (!batch.empty()) {
            m_netManager->sendChunkData(player->peer, batch);
        }
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
