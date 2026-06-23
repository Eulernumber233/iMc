#pragma once
#include "../core.h"
#include "../chunk/BlockType.h"
#include "../chunk/ChunkDimensions.h"
#include "RegionFile.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <glm/glm.hpp>

class Player;
class ChunkManager;
struct PlayerSaveData;

struct WorldInfo {
    std::string name;
    std::string path;
    uint64_t    seed = 0;
    uint64_t    lastPlayed = 0; // epoch 秒
};

// 玩家存档纯数据结构（不依赖 Player 头）
struct PlayerSaveData {
    float posX = 0, posY = 217.6f, posZ = 0;
    float yaw = -90.0f, pitch = 0;
};

class ChunkSaveManager {
public:
    ChunkSaveManager();
    ~ChunkSaveManager();

    // ---- 世界列表 ----
    static std::vector<WorldInfo> listWorlds(const std::string& savesRoot = "saves");

    // ---- 世界管理 ----
    bool createWorld(const std::string& worldName, uint64_t seed);
    bool openWorld(const std::string& worldName);
    void closeWorld();

    bool isWorldOpen() const { return m_worldOpen; }
    const std::string& getWorldName() const { return m_worldName; }
    uint64_t getSeed() const { return m_seed; }
    const std::string& getSavesRoot() const { return m_savesRoot; }

    // ---- 区块 I/O (线程安全, m_ioMutex 保护) ----
    // outBuf 大小必须 = ChunkConstants::CHUNK_VOLUME (65536)
    bool loadChunk(const glm::ivec2& chunkPos, BlockState* outBuf) const;

    // buf 大小必须 = ChunkConstants::CHUNK_VOLUME
    void saveChunk(const glm::ivec2& chunkPos, const BlockState* buf);

    bool chunkExists(const glm::ivec2& chunkPos) const;

    // ---- 玩家状态 ----
    bool loadPlayerState(PlayerSaveData& outData);
    void savePlayerState(const PlayerSaveData& data);

    // ---- 工具 (public — makeDirRecursive 自由函数需要) ----
    static bool makeDir(const std::string& path);
    static bool dirExists(const std::string& path);

private:
    std::string m_savesRoot;
    std::string m_worldPath;
    std::string m_worldName;
    uint64_t    m_seed = 0;
    bool        m_worldOpen = false;

    // region 缓存: key = ((int64_t)rx << 32) | (uint32_t)rz
    // mutable — loadChunk/chunkExists 是 const 但仍需持锁
    mutable std::unordered_map<int64_t, std::unique_ptr<RegionFile>> m_regions;
    mutable std::mutex m_ioMutex;

    RegionFile* getOrOpenRegion(int regionX, int regionZ);

    // 世界元数据 I/O
    bool readWorldJson(uint64_t& outSeed, PlayerSaveData& outPlayer);
    void writeWorldJson(uint64_t seed, const PlayerSaveData& player);

    // TLV 编解码
    static void encodeChunkTLV(const glm::ivec2& pos, const BlockState* buf,
                               std::vector<uint8_t>& out);
    static bool decodeChunkTLV(const std::vector<uint8_t>& data,
                               glm::ivec2& outPos, BlockState* outBuf);

    static inline int floorDiv(int a, int b) {
        return (a >= 0) ? a / b : (a - b + 1) / b;
    }
};
