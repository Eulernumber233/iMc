#pragma once

// 强制 ENet 使用 IPv4（部分 Windows 系统 IPv6 不可用，bind 报 10049）
#define ENET_IPV4_ONLY

#include <cstdint>
#include <chrono>

// 单调时钟：返回自首次调用起的秒数（double）。远程玩家插值缓冲用它作为统一时间轴
// —— 发送端节奏、网络抖动都被「按到达时间入缓冲 + 延迟播放」吸收，故用本地时钟即可，
// 不需要两端时钟同步。接收端（NetManager 记录到达时间）与渲染端（RenderSystem 计算
// renderTime = now - 插值延迟）都调它，保证同一时间轴。
inline double netNowSeconds() {
    static const auto epoch = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch).count();
}

namespace NetConstants {

// 单条消息最大有效载荷（取 32KB，远小于 uint16_t 上限 64KB，为 enet 分片留余量）
constexpr uint16_t MAX_MSG_PAYLOAD = 32768;

// ENet 通道分配
constexpr int CHANNEL_RELIABLE   = 0;
constexpr int CHANNEL_UNRELIABLE = 1;

// 房间默认最大客户端数
constexpr uint32_t DEFAULT_MAX_CLIENTS = 32;

// 默认端口（与 CliManager 命令行/菜单的默认端口保持一致）
constexpr uint16_t DEFAULT_PORT = 60011;

// 服务端为单个客户端加载/推送时允许的最大渲染半径（夹住异常/恶意上报，防内存爆）
constexpr int MAX_CLIENT_RENDER_RADIUS = 32;
// 客户端未上报（=0）时服务端采用的默认半径
constexpr int DEFAULT_CLIENT_RENDER_RADIUS = 8;

// ---- 保留 netId 段（NetObjectManager 分配 id 时须避开）----
// 玩家对象 netId == playerId，从 1 起小号；世界单例与动态实体各占一段，互不重叠。
constexpr uint16_t WORLD_STATE_NETID      = 0xFFF0;  // 世界状态单例（时间/天气），服务端权威
constexpr uint16_t DROPPED_ITEM_NETID_BASE = 0x1000; // 掉落物 netId 起点（服务端自增分配）
constexpr uint16_t DROPPED_ITEM_NETID_END  = 0xEFFF; // 掉落物 netId 上限

} // namespace NetConstants

// 可靠性级别
enum class NetReliability : uint8_t {
    Reliable,    // 可靠有序，走 channel 0 (ENET_PACKET_FLAG_RELIABLE)
    Unreliable,  // 不可靠无序，走 channel 1 (ENET_PACKET_FLAG_UNSEQUENCED)
};

// 网络对象类型标签：SPAWN_OBJECT 消息里携带，客户端据此构造正确的 NetObject 子类。
// 玩家对象走独立的 JOIN/PLAYER_LIST 路径，不用这里的通用 spawn，故不列入。
enum class NetObjType : uint8_t {
    WorldState  = 1,   // 世界状态单例（固定 netId，随会话常驻，实际不走 spawn）
    DroppedItem = 2,   // 掉落物实体（服务端权威模拟，spawn/despawn 复制）
};

// 世界命令类型（WORLD_CMD 消息）：客户端→服务端的"请求"，由服务端权威应用后经复制回传。
enum class WorldCmdType : uint8_t {
    SetTime      = 1,  // 设置世界时间（param = 小时 [0,24)）
    AdjustScale  = 2,  // 调整时间流速（param = 增量，可负）
    SetMoving    = 3,  // 设置时间是否流动（param = 0 停 / 1 走）
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
    CHUNK_REQUEST = 0x21,  // 客户端→服务端: 请求 chunk 数据
    CHUNK_RESPONSE= 0x22,  // 服务端→客户端: chunk 数据响应
    BLOCK_CHANGE  = 0x23,  // 双向: 方块修改 (MVP 后实现)
    CHAT_MESSAGE  = 0x30,  // 双向: 聊天 (MVP 后实现)
    INVENTORY_RESTORE = 0x31,  // 服务端→客户端: 加入时恢复该玩家存档的背包

    // === 通用对象复制 ===
    SPAWN_OBJECT  = 0x40,  // 服务端→客户端: 生成一个实体 (typeTag + netId + 初始属性)
    DESTROY_OBJECT= 0x41,  // 服务端→客户端: 销毁一个实体 (netId)
    WORLD_CMD     = 0x42,  // 客户端→服务端: 世界命令请求 (WorldCmdType + 参数)
    DROPPED_SYNC  = 0x43,  // 服务端→客户端: 掉落物位置/数量批量同步 (高频, 不可靠)
    DROP_REQUEST  = 0x44,  // 客户端→服务端: 请求生成掉落物 (客户端丢弃/破坏方块)

    // === 内部 ===
    PING          = 0xFE,  // 心跳
    CUSTOM        = 0xFF,  // 扩展
};
