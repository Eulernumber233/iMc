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

NetMessage NetMessage::joinAccept(uint16_t playerId, uint32_t seed,
                                   float posX, float posY, float posZ, float yaw,
                                   const std::string& skinName, const std::string& worldName) {
    NetMessage msg(NetMsgType::JOIN_ACCEPT);
    msg.payload.writePod(playerId);
    msg.payload.writePod(seed);
    msg.payload.writePod(posX);
    msg.payload.writePod(posY);
    msg.payload.writePod(posZ);
    msg.payload.writePod(yaw);
    msg.payload.writeString(skinName);
    msg.payload.writeString(worldName);
    return msg;
}

NetMessage NetMessage::joinDeny(const std::string& reason) {
    NetMessage msg(NetMsgType::JOIN_DENY);
    msg.payload.writeString(reason);
    return msg;
}

NetMessage NetMessage::playerJoined(uint16_t playerId, const std::string& name,
                                      float posX, float posY, float posZ, float yaw,
                                      const std::string& skinName) {
    NetMessage msg(NetMsgType::PLAYER_JOINED);
    msg.payload.writePod(playerId);
    msg.payload.writeString(name);
    msg.payload.writePod(posX);
    msg.payload.writePod(posY);
    msg.payload.writePod(posZ);
    msg.payload.writePod(yaw);
    msg.payload.writeString(skinName);
    return msg;
}

NetMessage NetMessage::playerLeft(uint16_t playerId) {
    NetMessage msg(NetMsgType::PLAYER_LEFT);
    msg.payload.writePod(playerId);
    return msg;
}

NetMessage NetMessage::playerList(const std::vector<std::pair<uint16_t, std::string>>& players,
                                   const float* positions, const float* yaws,
                                   const std::vector<std::string>* skinNames) {
    NetMessage msg(NetMsgType::PLAYER_LIST);
    msg.payload.writePod(static_cast<uint16_t>(players.size()));
    for (size_t i = 0; i < players.size(); ++i) {
        auto& [id, name] = players[i];
        msg.payload.writePod(id);
        msg.payload.writeString(name);
        if (positions) {
            msg.payload.writePod(positions[i * 3 + 0]);
            msg.payload.writePod(positions[i * 3 + 1]);
            msg.payload.writePod(positions[i * 3 + 2]);
        } else {
            msg.payload.writePod(0.0f);
            msg.payload.writePod(500.0f);
            msg.payload.writePod(0.0f);
        }
        if (yaws) {
            msg.payload.writePod(yaws[i]);
        } else {
            msg.payload.writePod(0.0f);
        }
        if (skinNames && i < skinNames->size()) {
            msg.payload.writeString((*skinNames)[i]);
        } else {
            msg.payload.writeString("");
        }
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

NetMessage NetMessage::chunkRequest(int32_t chunkX, int32_t chunkZ) {
    NetMessage msg(NetMsgType::CHUNK_REQUEST);
    msg.payload.writePod(chunkX);
    msg.payload.writePod(chunkZ);
    return msg;
}

NetMessage NetMessage::chunkResponse(const std::vector<uint8_t>& compressedData) {
    NetMessage msg(NetMsgType::CHUNK_RESPONSE);
    msg.payload.writeBytes(compressedData.data(), compressedData.size());
    return msg;
}

NetMessage NetMessage::blockChange(int32_t worldX, int32_t worldY, int32_t worldZ,
                                   uint16_t blockStateBits) {
    NetMessage msg(NetMsgType::BLOCK_CHANGE);
    msg.payload.writePod(worldX);
    msg.payload.writePod(worldY);
    msg.payload.writePod(worldZ);
    msg.payload.writePod(blockStateBits);
    return msg;
}
