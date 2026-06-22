#include "ChunkSaveManager.h"
#include <json/json.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _MSC_VER
#include <windows.h>
#include <direct.h>
#define mkdir_impl(path) _mkdir(path)
#else
#define mkdir_impl(path) mkdir(path, 0755)
#endif

// ====================== 工具函数 ======================

bool ChunkSaveManager::makeDir(const std::string& path) {
    return mkdir_impl(path.c_str()) == 0;
}

bool ChunkSaveManager::dirExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR);
}

static bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFREG);
}

static bool makeDirRecursive(const std::string& path) {
    if (ChunkSaveManager::dirExists(path)) return true;
    // 找上一级目录
    size_t pos = path.find_last_of("/\\");
    if (pos != std::string::npos) {
        if (!makeDirRecursive(path.substr(0, pos))) return false;
    }
    return ChunkSaveManager::makeDir(path);
}

// ====================== 构造 / 析构 ======================

ChunkSaveManager::ChunkSaveManager() : m_savesRoot("saves") {}

ChunkSaveManager::~ChunkSaveManager() {
    closeWorld();
}

// ====================== 世界列表 ======================

std::vector<WorldInfo> ChunkSaveManager::listWorlds(const std::string& savesRoot) {
    std::vector<WorldInfo> result;
    if (!dirExists(savesRoot)) return result;

    // 扫描 saves/ 下的子目录
    // Windows: 使用 FindFirstFile / FindNextFile
#ifdef _MSC_VER
    std::string searchPath = savesRoot + "\\*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return result;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;

        std::string worldPath = savesRoot + "\\" + fd.cFileName;
        std::string jsonPath  = worldPath + "\\world.json";

        // 读取 world.json
        std::ifstream ifs(jsonPath);
        if (!ifs.good()) continue;

        std::stringstream ss;
        ss << ifs.rdbuf();
        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(ss.str(), root)) continue;

        WorldInfo info;
        info.name = fd.cFileName;
        info.path = worldPath;
        info.seed = root.get("seed", 0).asUInt();
        info.lastPlayed = (uint64_t)root.get("lastPlayed", 0).asUInt();
        result.push_back(info);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#endif
    return result;
}

// ====================== 世界创建 / 打开 ======================

bool ChunkSaveManager::createWorld(const std::string& worldName, uint32_t seed) {
    if (m_worldOpen) closeWorld();

    m_worldPath = m_savesRoot + "/" + worldName;
    std::string regionDir = m_worldPath + "/region";

    // 检查是否已存在
    if (dirExists(m_worldPath)) {
        std::cerr << "[Save] world \"" << worldName << "\" already exists\n";
        return false;
    }

    if (!makeDirRecursive(regionDir)) {
        std::cerr << "[Save] failed to create directory: " << regionDir << "\n";
        return false;
    }

    // 写 world.json
    PlayerSaveData defaultPlayer;
    writeWorldJson(seed, defaultPlayer);

    m_worldName = worldName;
    m_seed = seed;
    m_worldOpen = true;
    std::cout << "[Save] created world \"" << worldName << "\" seed=" << seed << "\n";
    return true;
}

bool ChunkSaveManager::openWorld(const std::string& worldName) {
    if (m_worldOpen) closeWorld();

    m_worldPath = m_savesRoot + "/" + worldName;

    if (!dirExists(m_worldPath)) {
        std::cerr << "[Save] world \"" << worldName << "\" not found\n";
        return false;
    }

    PlayerSaveData pd;
    if (!readWorldJson(m_seed, pd)) {
        std::cerr << "[Save] failed to read world.json for \"" << worldName << "\"\n";
        return false;
    }

    // 确保 region 目录存在
    std::string regionDir = m_worldPath + "/region";
    if (!dirExists(regionDir)) {
        makeDirRecursive(regionDir);
    }

    m_worldName = worldName;
    m_worldOpen = true;
    std::cout << "[Save] opened world \"" << worldName << "\" seed=" << m_seed << "\n";
    return true;
}

void ChunkSaveManager::closeWorld() {
    std::lock_guard<std::mutex> lock(m_ioMutex);
    m_regions.clear(); // 关闭所有 region 文件
    m_worldOpen = false;
    m_worldName.clear();
    m_worldPath.clear();
    m_seed = 0;
}

// ====================== 玩家状态 ======================

bool ChunkSaveManager::loadPlayerState(PlayerSaveData& outData) {
    if (!m_worldOpen) return false;
    uint32_t seed;
    return readWorldJson(seed, outData);
}

void ChunkSaveManager::savePlayerState(const PlayerSaveData& data) {
    if (!m_worldOpen) return;
    writeWorldJson(m_seed, data);
}

// ====================== 区块 I/O ======================

bool ChunkSaveManager::loadChunk(const glm::ivec2& chunkPos, BlockState* outBuf) const {
    if (!m_worldOpen) return false;

    int rx = floorDiv(chunkPos.x, RegionFile::REGION_SIZE);
    int rz = floorDiv(chunkPos.y, RegionFile::REGION_SIZE);
    int lx = chunkPos.x - rx * RegionFile::REGION_SIZE;
    int lz = chunkPos.y - rz * RegionFile::REGION_SIZE;

    std::lock_guard<std::mutex> lock(m_ioMutex);

    // 检查 region 文件是否存在（不创建）
    std::string regionPath = m_worldPath + "/region/r." +
        std::to_string(rx) + "." + std::to_string(rz) + ".mca";
    if (!fileExists(regionPath)) return false;

    RegionFile rf(regionPath);
    if (!rf.openForRead()) return false;

    std::vector<uint8_t> data;
    if (!rf.readChunk(lx, lz, data)) return false;

    rf.close();

    // 解码 TLV
    glm::ivec2 decodedPos;
    if (!decodeChunkTLV(data, decodedPos, outBuf)) return false;

    return true;
}

void ChunkSaveManager::saveChunk(const glm::ivec2& chunkPos, const BlockState* buf) {
    if (!m_worldOpen) return;

    int rx = floorDiv(chunkPos.x, RegionFile::REGION_SIZE);
    int rz = floorDiv(chunkPos.y, RegionFile::REGION_SIZE);
    int lx = chunkPos.x - rx * RegionFile::REGION_SIZE;
    int lz = chunkPos.y - rz * RegionFile::REGION_SIZE;

    // TLV 编码
    std::vector<uint8_t> tlvData;
    encodeChunkTLV(chunkPos, buf, tlvData);

    std::lock_guard<std::mutex> lock(m_ioMutex);

    RegionFile* rf = getOrOpenRegion(rx, rz);
    if (!rf) {
        std::cerr << "[Save] failed to open region for save\n";
        return;
    }
    rf->writeChunk(lx, lz, tlvData);
}

bool ChunkSaveManager::chunkExists(const glm::ivec2& chunkPos) const {
    if (!m_worldOpen) return false;

    int rx = floorDiv(chunkPos.x, RegionFile::REGION_SIZE);
    int rz = floorDiv(chunkPos.y, RegionFile::REGION_SIZE);
    int lx = chunkPos.x - rx * RegionFile::REGION_SIZE;
    int lz = chunkPos.y - rz * RegionFile::REGION_SIZE;

    std::lock_guard<std::mutex> lock(m_ioMutex);

    std::string regionPath = m_worldPath + "/region/r." +
        std::to_string(rx) + "." + std::to_string(rz) + ".mca";
    if (!fileExists(regionPath)) return false;

    RegionFile rf(regionPath);
    if (!rf.openForRead()) return false;
    bool exists = rf.hasChunk(lx, lz);
    rf.close();
    return exists;
}

// ====================== Region 缓存 ======================

RegionFile* ChunkSaveManager::getOrOpenRegion(int regionX, int regionZ) {
    int64_t key = ((int64_t)(uint32_t)regionX << 32) | (uint64_t)(uint32_t)regionZ;
    auto it = m_regions.find(key);
    if (it != m_regions.end()) {
        return it->second.get();
    }

    // 确保目录存在
    std::string regionDir = m_worldPath + "/region";
    if (!dirExists(regionDir)) {
        makeDirRecursive(regionDir);
    }

    std::string path = regionDir + "/r." +
        std::to_string(regionX) + "." + std::to_string(regionZ) + ".mca";

    auto rf = std::make_unique<RegionFile>(path);
    if (!rf->openForWrite()) return nullptr;
    RegionFile* ptr = rf.get();
    m_regions[key] = std::move(rf);
    return ptr;
}

// ====================== world.json I/O ======================

bool ChunkSaveManager::readWorldJson(uint32_t& outSeed, PlayerSaveData& outPlayer) {
    std::string path = m_worldPath + "/world.json";
    std::ifstream ifs(path);
    if (!ifs.good()) return false;

    std::stringstream ss;
    ss << ifs.rdbuf();
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(ss.str(), root)) {
        std::cerr << "[Save] parse error: " << reader.getFormatedErrorMessages() << "\n";
        return false;
    }

    outSeed = root.get("seed", 0).asUInt();
    outPlayer.posX   = (float)root.get("playerX", 0.0).asDouble();
    outPlayer.posY   = (float)root.get("playerY", 217.6).asDouble();
    outPlayer.posZ   = (float)root.get("playerZ", 0.0).asDouble();
    outPlayer.yaw    = (float)root.get("playerYaw", -90.0).asDouble();
    outPlayer.pitch  = (float)root.get("playerPitch", 0.0).asDouble();
    return true;
}

void ChunkSaveManager::writeWorldJson(uint32_t seed, const PlayerSaveData& player) {
    std::string path = m_worldPath + "/world.json";

    Json::Value root;
    root["seed"] = seed;
    root["playerX"] = player.posX;
    root["playerY"] = player.posY;
    root["playerZ"] = player.posZ;
    root["playerYaw"] = player.yaw;
    root["playerPitch"] = player.pitch;
    root["lastPlayed"] = (Json::UInt)std::time(nullptr);

    Json::FastWriter writer;
    std::string json = writer.write(root);

    std::ofstream ofs(path);
    if (ofs.good()) {
        ofs << json;
    }
}

// ====================== TLV 编解码 ======================

static void writeTLVEntry(std::vector<uint8_t>& out, uint8_t tag,
                          const uint8_t* data, uint32_t len) {
    out.push_back(tag);
    out.insert(out.end(), (const uint8_t*)&len, (const uint8_t*)&len + 4);
    if (len > 0) out.insert(out.end(), data, data + len);
}

void ChunkSaveManager::encodeChunkTLV(const glm::ivec2& pos,
    const BlockState* buf, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(128 * 1024); // 典型未压缩大小

    // 0x01 CHUNK_HEADER
    {
        uint8_t hdr[12];
        int32_t cx = pos.x, cz = pos.y;
        uint32_t flags = 0;
        memcpy(hdr + 0, &cx, 4);
        memcpy(hdr + 4, &cz, 4);
        memcpy(hdr + 8, &flags, 4);
        writeTLVEntry(out, 0x01, hdr, 12);
    }

    // 0x10 SECTION_BLOCKS — 每个非空 section
    constexpr int SY = ChunkConstants::CHUNK_HEIGHT / ChunkConstants::SECTION_HEIGHT; // 16
    constexpr int SEC_VOL = ChunkConstants::SECTION_HEIGHT *
                            ChunkConstants::CHUNK_DEPTH *
                            ChunkConstants::CHUNK_WIDTH; // 4096
    for (int sy = 0; sy < SY; ++sy) {
        const BlockState* secBuf = buf + sy * SEC_VOL;

        // 检查是否为空
        bool empty = true;
        for (int i = 0; i < SEC_VOL; ++i) {
            if (secBuf[i].type() != BLOCK_AIR) {
                empty = false;
                break;
            }
        }
        if (empty) continue;

        std::vector<uint8_t> data(4 + SEC_VOL * sizeof(BlockState));
        data[0] = (uint8_t)sy;
        data[1] = data[2] = data[3] = 0; // reserved
        memcpy(data.data() + 4, secBuf, SEC_VOL * sizeof(BlockState));
        writeTLVEntry(out, 0x10, data.data(), (uint32_t)data.size());
    }

    // 0x00 END
    writeTLVEntry(out, 0x00, nullptr, 0);
}

bool ChunkSaveManager::decodeChunkTLV(const std::vector<uint8_t>& data,
    glm::ivec2& outPos, BlockState* outBuf) {
    // 初始化为全 AIR
    constexpr int VOL = ChunkConstants::CHUNK_VOLUME;
    memset(outBuf, 0, VOL * sizeof(BlockState));

    size_t off = 0;
    bool gotHeader = false;

    while (off + 5 <= data.size()) {
        uint8_t tag = data[off];
        uint32_t len;
        memcpy(&len, data.data() + off + 1, 4);
        off += 5;

        if (off + len > data.size()) return false; // 越界

        switch (tag) {
        case 0x00: // END
            return gotHeader; // 正常结束

        case 0x01: { // CHUNK_HEADER
            if (len < 12) return false;
            memcpy(&outPos.x, data.data() + off, 4);
            memcpy(&outPos.y, data.data() + off + 4, 4);
            gotHeader = true;
            break;
        }

        case 0x10: { // SECTION_BLOCKS
            if (len < 4 + 4096 * sizeof(BlockState)) return false;
            uint8_t sectionY = data[off];
            if (sectionY >= 16) break; // 无效 sectionY，跳过
            const BlockState* src = reinterpret_cast<const BlockState*>(data.data() + off + 4);
            BlockState* dst = outBuf + sectionY * 4096;
            memcpy(dst, src, 4096 * sizeof(BlockState));
            break;
        }

        default:
            // 未知 tag，跳过（向前兼容）
            break;
        }

        off += len;
    }

    return gotHeader;
}
