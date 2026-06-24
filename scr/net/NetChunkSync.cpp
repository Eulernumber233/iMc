#include "NetChunkSync.h"
#include "NetManager.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/Chunk.h"
#include "../chunk/Section.h"
#include "lz4.h"
#include <cstdio>
#include <cstring>

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

    Chunk* chunk = m_chunkManager->getChunk(glm::ivec2(chunkX, chunkZ));
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
    size_t pos = 0;

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

    if (pos >= len) return;
    uint8_t numSections = data[pos++];

    static constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    static constexpr int BLOCK_SIZE = Section::VOLUME * sizeof(BlockState);
    auto blockBuf = std::make_unique<BlockState[]>(VOL);
    std::memset(blockBuf.get(), 0, VOL * sizeof(BlockState));  // 默认 AIR

    std::vector<uint8_t> decompressBuf(BLOCK_SIZE);

    for (uint8_t i = 0; i < numSections && pos < len; ++i) {
        if (pos >= len) break;
        uint8_t sy = data[pos++];

        if (pos >= len) break;
        uint8_t flags = data[pos++];

        if ((flags & ChunkSyncFormat::FLAG_HAS_DATA) == 0) {
            // 全空气 section：已经清零，无需操作
            continue;
        }

        // 读取压缩数据
        if (pos + 2 > len) break;
        uint16_t dataLen = static_cast<uint16_t>(data[pos])
            | (static_cast<uint16_t>(data[pos + 1]) << 8);
        pos += 2;

        if (pos + dataLen > len) break;

        // LZ4 解压
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

        // 将 section 数据写入 chunk buffer 对应位置
        int sectionStart = sy * Section::VOLUME;
        std::memcpy(blockBuf.get() + sectionStart,
            decompressBuf.data(), BLOCK_SIZE);
    }

    // 提交导入
    if (m_chunkManager) {
        m_chunkManager->importChunkData(chunkX, chunkZ, std::move(blockBuf));
    } else {
        static int nullCmWarnCount = 0;
        if (nullCmWarnCount < 5) {
            fprintf(stderr, "[NetChunkSync] deserializeAndImport: m_chunkManager is null!\n");
            ++nullCmWarnCount;
        }
    }
}
