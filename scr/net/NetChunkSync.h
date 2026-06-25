#pragma once

#include "NetCommon.h"
#include "../chunk/BlockType.h"
#include "../chunk/ChunkDimensions.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <memory>

#include "../enet/enet.h"
class ChunkManager;
class NetManager;
struct BlockState;

// ChunkSync 消息格式常量
namespace ChunkSyncFormat {
    // section flags
    constexpr uint8_t FLAG_HAS_DATA = 0x01;  // 有方块数据，否则全空气
}

// 网络地形同步：服务端推送 + 客户端接收
class NetChunkSync {
public:
    NetChunkSync() = default;

    void init(ChunkManager* cm, NetManager* net);

    // ---- 服务端 ----

    // 增量：推送新晋升到 loaded 的 chunk
    void pushChunks();

    // 全量：向指定 peer 推送所有 loaded chunk（新玩家加入时调用）
    void pushAllChunks(ENetPeer* peer);

    // ---- 服务端按需响应 ----
    void handleChunkRequest(ENetPeer* peer, int chunkX, int chunkZ);

    // 客户端断连时清理 pending 请求
    void onPeerDisconnected(ENetPeer* peer);

    // ---- 客户端 ----

    // 处理收到的 CHUNK_DATA / CHUNK_RESPONSE payload
    void onChunkData(const uint8_t* data, size_t len);

private:
    using ChunkKey = int64_t;

    static ChunkKey makeKey(int x, int z) {
        return (static_cast<int64_t>(x) << 32) | (static_cast<int64_t>(z) & 0xFFFFFFFFLL);
    }

    ChunkManager* m_chunkManager = nullptr;
    NetManager* m_netManager = nullptr;

    // per-peer 已推送 chunk 集合
    std::unordered_map<ENetPeer*, std::unordered_set<ChunkKey>> m_sentChunks;

    // 客户端 pending 请求：chunkKey → 等待响应的 peer 列表
    std::unordered_map<ChunkKey, std::vector<ENetPeer*>> m_pendingRequests;

    // 每帧轮询：检查 pending 请求是否有就绪的区块数据，有则发送 CHUNK_RESPONSE
    void pollPendingRequests();

    // 将单个 chunk 序列化 + 压缩为 CHUNK_DATA payload
    void serializeChunk(int chunkX, int chunkZ, std::vector<uint8_t>& out);

    // 从原始 BlockState 数组序列化（用于 block-ready 状态的 chunk 按需响应）
    void serializeChunkFromBlocks(int chunkX, int chunkZ, const BlockState* blocks,
                                  std::vector<uint8_t>& out);

    // 从 CHUNK_DATA payload 反序列化为 BlockState buffer + 导入
    void deserializeAndImport(const uint8_t* data, size_t len);
};
