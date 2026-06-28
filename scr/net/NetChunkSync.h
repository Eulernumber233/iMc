#pragma once

#include "NetCommon.h"
#include "NetSerializeWorker.h"
#include "../chunk/BlockType.h"
#include "../chunk/BlockBox.h"
#include "../chunk/ChunkDimensions.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>

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

    // 关闭序列化线程（NetManager::leave 调用）
    void shutdown();

    // ---- 服务端 ----

    // 增量：推送新晋升到 loaded 的 chunk
    void pushChunks();

    // 主线程每帧调用：取回序列化线程完成的 payload，对仍存活的 peer 做 enet_peer_send。
    // 必须在 pushChunks 之后、与其他网络发送同帧调用。
    void pollSerializeResults();

    // 全量：向指定 peer 推送所有 loaded chunk（新玩家加入时调用）
    void pushAllChunks(ENetPeer* peer);

    // ---- 服务端按需响应 ----
    void handleChunkRequest(ENetPeer* peer, int chunkX, int chunkZ);

    // 客户端断连时清理 pending 请求
    void onPeerDisconnected(ENetPeer* peer);

    // 服务端 chunk 卸载时：清除所有 peer 对该 chunk 的"已推送"记录，
    // 使玩家再次靠近该区域时能重新收到（最新）数据。
    void onChunkUnloaded(int chunkX, int chunkZ);

    // ---- 服务端：方块修改广播（相关性过滤）----

    // 把一条已编码好的 BLOCK_CHANGE 消息广播给所有"已加载该 chunk"的客户端
    // （即 m_sentChunks 中含该 chunkKey 的 peer）。excludePeer 可排除发起者外的某 peer，
    // 传 nullptr 表示发给所有相关 peer（含发起者，保证发起者也通过广播路径生效）。
    void broadcastBlockChange(int chunkX, int chunkZ,
                              const std::vector<uint8_t>& encodedMsg);

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

    // 专用序列化线程：chunk 序列化 + LZ4 压缩在此线程做，主线程只投递任务 + 取回 payload 发送。
    NetSerializeWorker m_serializeWorker;

    // 存活 peer 集合：序列化异步，job 投递时 peer 有效但 result 取回时可能已断开。
    // pollSerializeResults 发送前用此集合过滤 targets（§1.4 peer 失效复核）。
    std::unordered_set<ENetPeer*> m_alivePeers;

    // 把一个 chunk 的序列化任务投递给 worker（拷 ChunkBoxes 保活 + 记录 targets）。
    // 调用方应已确认该 chunk 有方块数据（getChunkBoxes 成功）。
    void submitSerializeJob(int chunkX, int chunkZ, const ChunkBoxes& boxes,
                            std::vector<ENetPeer*> targets);

    // per-peer 已推送 chunk 集合
    std::unordered_map<ENetPeer*, std::unordered_set<ChunkKey>> m_sentChunks;

    // 增量推送待发队列：从 ChunkManager 取走的"新晋升"chunk 事件，攒着按帧预算
    // 逐批发给所有 peer（per-peer sent 去重）。替代过去每帧全量扫描全部 chunk。
    // m_pendingPushSet 与 m_pendingPush 同步，用于 O(1) 去重。
    std::vector<glm::ivec2> m_pendingPush;
    std::unordered_set<ChunkKey> m_pendingPushSet;

    // 客户端 pending 请求：chunkKey → 等待响应的 peer 列表
    std::unordered_map<ChunkKey, std::vector<ENetPeer*>> m_pendingRequests;

    // 每帧轮询：检查 pending 请求是否有就绪的区块数据，有则投递到序列化线程
    void pollPendingRequests();

    // 从 CHUNK_DATA payload 反序列化为 BlockState buffer + 导入
    void deserializeAndImport(const uint8_t* data, size_t len);
};
