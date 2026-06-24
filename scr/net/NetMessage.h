#pragma once

#include "NetCommon.h"
#include "NetSerializer.h"
#include <vector>
#include <cstdint>
#include <string>

// ============================================================================
// NetMessage: 网络消息编解码
// ============================================================================

struct NetMessageHeader {
    static constexpr size_t SIZE = 4;

    uint8_t  msgType = 0;
    uint8_t  reserved = 0;
    uint16_t payloadLen = 0;
};

class NetMessage {
public:
    NetMsgType type = NetMsgType::PING;
    MemoryStream payload;

    NetMessage() = default;
    explicit NetMessage(NetMsgType t) : type(t) {}

    // 编码为二进制 (header + payload → 连续 buffer)
    void encode(std::vector<uint8_t>& out) const;

    // 从二进制解码 (连续 buffer → header + payload)
    // 返回 true 表示成功
    static bool decode(const uint8_t* data, size_t len, NetMessage& out);

    // ---- 便捷工厂 ----
    static NetMessage joinRequest(const std::string& playerName);
    static NetMessage joinAccept(uint16_t playerId, uint32_t seed);
    static NetMessage joinDeny(const std::string& reason);
    static NetMessage playerJoined(uint16_t playerId, const std::string& name);
    static NetMessage playerLeft(uint16_t playerId);
    static NetMessage playerList(const std::vector<std::pair<uint16_t, std::string>>& players);
    static NetMessage propertySync(uint16_t netObjId, MemoryStream& propData);
    static NetMessage chunkData(const std::vector<uint8_t>& compressedChunks);
};
