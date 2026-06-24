#pragma once

// 强制 ENet 使用 IPv4（部分 Windows 系统 IPv6 不可用，bind 报 10049）
#define ENET_IPV4_ONLY

#include <cstdint>

namespace NetConstants {

// 单条消息最大有效载荷（取 32KB，远小于 uint16_t 上限 64KB，为 enet 分片留余量）
constexpr uint16_t MAX_MSG_PAYLOAD = 32768;

// ENet 通道分配
constexpr int CHANNEL_RELIABLE   = 0;
constexpr int CHANNEL_UNRELIABLE = 1;

// 房间默认最大客户端数
constexpr uint32_t DEFAULT_MAX_CLIENTS = 32;

// 默认端口
constexpr uint16_t DEFAULT_PORT = 27015;

} // namespace NetConstants

// 可靠性级别
enum class NetReliability : uint8_t {
    Reliable,    // 可靠有序，走 channel 0 (ENET_PACKET_FLAG_RELIABLE)
    Unreliable,  // 不可靠无序，走 channel 1 (ENET_PACKET_FLAG_UNSEQUENCED)
};

// 消息类型
enum class NetMsgType : uint8_t {
    // === 连接控制 ===
    JOIN_REQUEST  = 0x01,  // 客户端→服务端: 请求加入
    JOIN_ACCEPT   = 0x02,  // 服务端→客户端: 接受，含玩家ID + 种子
    JOIN_DENY     = 0x03,  // 服务端→客户端: 拒绝
    PLAYER_JOINED = 0x04,  // 服务端→所有客户端: 新玩家加入
    PLAYER_LEFT   = 0x05,  // 服务端→所有客户端: 玩家离开
    PLAYER_LIST   = 0x06,  // 服务端→新客户端: 在线玩家列表

    // === 状态同步 (双向) ===
    PROPERTY_SYNC = 0x10,  // 属性同步 (MVP 双向: 客户端→服务端→广播)

    // === 游戏数据 ===
    CHUNK_DATA    = 0x20,  // 服务端→客户端: chunk 方块数据 (LZ4 压缩)
    BLOCK_CHANGE  = 0x23,  // 双向: 方块修改 (MVP 后实现)
    CHAT_MESSAGE  = 0x30,  // 双向: 聊天 (MVP 后实现)

    // === 内部 ===
    PING          = 0xFE,  // 心跳
    CUSTOM        = 0xFF,  // 扩展
};
