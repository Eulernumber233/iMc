#include "NetMessage.h"
#include <cstring>

void NetMessage::encode(std::vector<uint8_t>& out) const {
    uint16_t payloadLen = static_cast<uint16_t>(payload.size());

    out.push_back(static_cast<uint8_t>(type));
    out.push_back(0); // reserved
    out.push_back(static_cast<uint8_t>(payloadLen & 0xFF));
    out.push_back(static_cast<uint8_t>((payloadLen >> 8) & 0xFF));

    if (payloadLen > 0 && payload.data()) {
        out.insert(out.end(), payload.data(), payload.data() + payloadLen);
    }
}

bool NetMessage::decode(const uint8_t* data, size_t len, NetMessage& out) {
    if (!data || len < NetMessageHeader::SIZE) return false;

    out.type = static_cast<NetMsgType>(data[0]);
    // data[1] = reserved
    uint16_t payloadLen = static_cast<uint16_t>(data[2])
                        | (static_cast<uint16_t>(data[3]) << 8);

    if (len < NetMessageHeader::SIZE + payloadLen) return false;

    out.payload.reset();
    if (payloadLen > 0) {
        out.payload.writeBytes(data + NetMessageHeader::SIZE, payloadLen);
    }
    return true;
}

// ---- 便捷工厂 ----

NetMessage NetMessage::joinRequest(const std::string& playerName) {
    NetMessage msg(NetMsgType::JOIN_REQUEST);
    msg.payload.writeString(playerName);
    return msg;
}

NetMessage NetMessage::joinAccept(uint16_t playerId, uint32_t seed) {
    NetMessage msg(NetMsgType::JOIN_ACCEPT);
    msg.payload.writePod(playerId);
    msg.payload.writePod(seed);
    return msg;
}

NetMessage NetMessage::joinDeny(const std::string& reason) {
    NetMessage msg(NetMsgType::JOIN_DENY);
    msg.payload.writeString(reason);
    return msg;
}

NetMessage NetMessage::playerJoined(uint16_t playerId, const std::string& name) {
    NetMessage msg(NetMsgType::PLAYER_JOINED);
    msg.payload.writePod(playerId);
    msg.payload.writeString(name);
    return msg;
}

NetMessage NetMessage::playerLeft(uint16_t playerId) {
    NetMessage msg(NetMsgType::PLAYER_LEFT);
    msg.payload.writePod(playerId);
    return msg;
}

NetMessage NetMessage::playerList(const std::vector<std::pair<uint16_t, std::string>>& players) {
    NetMessage msg(NetMsgType::PLAYER_LIST);
    msg.payload.writePod(static_cast<uint16_t>(players.size()));
    for (auto& [id, name] : players) {
        msg.payload.writePod(id);
        msg.payload.writeString(name);
    }
    return msg;
}

NetMessage NetMessage::propertySync(uint16_t netObjId, MemoryStream& propData) {
    NetMessage msg(NetMsgType::PROPERTY_SYNC);
    msg.payload.writePod(netObjId);
    if (propData.size() > 0) {
        msg.payload.writeBytes(propData.data(), propData.size());
    }
    return msg;
}

NetMessage NetMessage::chunkData(const std::vector<uint8_t>& compressedChunks) {
    NetMessage msg(NetMsgType::CHUNK_DATA);
    msg.payload.writeBytes(compressedChunks.data(), compressedChunks.size());
    return msg;
}
