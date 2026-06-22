#include "RegionFile.h"
#include <algorithm>
#include <cstring>

#ifdef _MSC_VER
#include <sys/utime.h>
#else
#include <utime.h>
#endif

RegionFile::RegionFile(const std::string& path) : m_path(path) {}

RegionFile::~RegionFile() {
    close();
}

bool RegionFile::openForRead() {
    if (m_file) close();
#ifdef _MSC_VER
    fopen_s(&m_file, m_path.c_str(), "rb");
#else
    m_file = fopen(m_path.c_str(), "rb");
#endif
    if (!m_file) return false;
    readHeader();
    return true;
}

bool RegionFile::openForWrite() {
    if (m_file) close();
    // 先尝试打开已有文件，不存在则创建
#ifdef _MSC_VER
    fopen_s(&m_file, m_path.c_str(), "r+b");
    if (!m_file) {
        fopen_s(&m_file, m_path.c_str(), "w+b");
    }
#else
    m_file = fopen(m_path.c_str(), "r+b");
    if (!m_file) {
        m_file = fopen(m_path.c_str(), "w+b");
    }
#endif
    if (!m_file) return false;

    // 如果是空文件（刚创建），写入空 header
    fseek(m_file, 0, SEEK_END);
    long fileSize = ftell(m_file);
    if (fileSize < HEADER_BYTES) {
        m_offsets.fill(0);
        m_timestamps.fill(0);
        writeHeader();
    } else {
        readHeader();
    }
    return true;
}

void RegionFile::close() {
    if (m_file) {
        fclose(m_file);
        m_file = nullptr;
    }
}

void RegionFile::readHeader() {
    if (!m_file) return;
    fseek(m_file, 0, SEEK_SET);
    fread(m_offsets.data(), sizeof(uint32_t), MAX_CHUNKS, m_file);
    fread(m_timestamps.data(), sizeof(uint32_t), MAX_CHUNKS, m_file);
}

void RegionFile::writeHeader() {
    if (!m_file) return;
    fseek(m_file, 0, SEEK_SET);
    fwrite(m_offsets.data(), sizeof(uint32_t), MAX_CHUNKS, m_file);
    fwrite(m_timestamps.data(), sizeof(uint32_t), MAX_CHUNKS, m_file);
    fflush(m_file);
}

bool RegionFile::hasChunk(int localX, int localZ) const {
    return m_offsets[index(localX, localZ)] != 0;
}

bool RegionFile::readChunk(int localX, int localZ, std::vector<uint8_t>& outData) {
    if (!m_file) return false;
    int idx = index(localX, localZ);
    uint32_t entry = m_offsets[idx];
    if (entry == 0) return false;

    uint32_t sectorStart = entry & 0x00FFFFFFu;
    uint32_t sectorCount = (entry >> 24) & 0xFFu;
    if (sectorCount == 0) return false;

    // 读前缀
    fseek(m_file, sectorStart * (long long)SECTOR_SIZE, SEEK_SET);
    uint32_t uncompSize = 0;
    uint8_t  compType = 0;
    if (fread(&uncompSize, 4, 1, m_file) != 1) return false;
    if (fread(&compType, 1, 1, m_file) != 1) return false;

    if (uncompSize == 0 || uncompSize > 128 * 1024 * 1024) return false; // 防损坏

    std::vector<uint8_t> compressed;
    // sectorCount 个 sector 的总字节数，减去 5 字节前缀
    size_t storedLen = (size_t)sectorCount * SECTOR_SIZE - 5;
    if (storedLen > 128 * 1024 * 1024) return false;

    if (compType == COMPRESS_NONE) {
        outData.resize(uncompSize);
        if (fread(outData.data(), 1, uncompSize, m_file) != uncompSize) return false;
    } else if (compType == COMPRESS_ZLIB) {
        compressed.resize(storedLen);
        size_t rd = fread(compressed.data(), 1, storedLen, m_file);
        // zlib 解压暂不支持，预留
        return false;
    } else {
        return false; // 未知压缩类型
    }
    return true;
}

void RegionFile::writeChunk(int localX, int localZ, const std::vector<uint8_t>& data) {
    if (!m_file) return;
    int idx = index(localX, localZ);
    uint32_t oldEntry = m_offsets[idx];

    // 计算所需 sector 数（5 字节前缀 + 数据，按 4KB 取整）
    uint32_t totalBytes = 5 + (uint32_t)data.size();
    uint8_t sectorCount = (uint8_t)((totalBytes + SECTOR_SIZE - 1) / SECTOR_SIZE);
    if (sectorCount == 0) sectorCount = 1;

    // 分配空间
    uint32_t sectorStart = allocateSectors(sectorCount, oldEntry);
    uint32_t newEntry = sectorStart | ((uint32_t)sectorCount << 24);

    // 写入数据
    fseek(m_file, sectorStart * (long long)SECTOR_SIZE, SEEK_SET);

    uint32_t uncompSize = (uint32_t)data.size();
    uint8_t  compType = COMPRESS_NONE; // 暂不压缩
    fwrite(&uncompSize, 4, 1, m_file);
    fwrite(&compType, 1, 1, m_file);
    fwrite(data.data(), 1, data.size(), m_file);

    // 填充剩余 sector 空间为零
    uint32_t written = 5 + (uint32_t)data.size();
    uint32_t padded  = sectorCount * SECTOR_SIZE;
    if (padded > written) {
        std::vector<uint8_t> zeros(padded - written, 0);
        fwrite(zeros.data(), 1, zeros.size(), m_file);
    }

    // 更新时间戳
    m_timestamps[idx] = (uint32_t)std::time(nullptr);

    // 更新 header（仅当 offset 变更时才写 header）
    if (newEntry != oldEntry) {
        m_offsets[idx] = newEntry;
        writeHeader();
    } else {
        // 只写 timestamp 行
        fseek(m_file, HEADER_BYTES / 2 + idx * sizeof(uint32_t), SEEK_SET);
        fwrite(&m_timestamps[idx], sizeof(uint32_t), 1, m_file);
    }
    fflush(m_file);
}

uint32_t RegionFile::allocateSectors(uint8_t needed, uint32_t oldOffset) {
    // 收集已用区间（起始 sector, 结束 sector）
    struct Range {
        uint32_t start;
        uint32_t end;   // 不含
    };
    std::vector<Range> used;
    uint32_t oldSectorStart = 0;
    if (oldOffset != 0) {
        oldSectorStart = oldOffset & 0x00FFFFFFu;
    }

    for (int i = 0; i < MAX_CHUNKS; ++i) {
        uint32_t e = m_offsets[i];
        if (e == 0 || e == oldOffset) continue; // 跳过空位和自身旧位
        uint32_t s = e & 0x00FFFFFFu;
        uint8_t  c = (uint8_t)((e >> 24) & 0xFFu);
        if (c == 0) continue;
        used.push_back({s, s + c});
    }

    // 按起始 sector 排序
    std::sort(used.begin(), used.end(),
        [](const Range& a, const Range& b) { return a.start < b.start; });

    // 获取文件末尾（按 sector 对齐）
    fseek(m_file, 0, SEEK_END);
    long fileEnd = ftell(m_file);
    uint32_t fileEndSectors = (uint32_t)((fileEnd + SECTOR_SIZE - 1) / SECTOR_SIZE);

    // 找 best-fit 间隙
    uint32_t bestGapStart = 0;
    uint32_t bestGapSize  = 0xFFFFFFFFu;

    uint32_t cursor = 2; // 数据从 sector 2 开始（0-1 是 header）

    for (const auto& r : used) {
        if (r.start > cursor) {
            uint32_t gapSize = r.start - cursor;
            if (gapSize >= needed && gapSize < bestGapSize) {
                bestGapStart = cursor;
                bestGapSize  = gapSize;
            }
        }
        cursor = (std::max)(cursor, r.end);
    }

    // 文件末尾之后的空间
    if (fileEndSectors >= cursor) {
        uint32_t gapSize = 0xFFFFFFFFu - cursor; // 理论上无限
        if (gapSize >= needed && gapSize < bestGapSize) {
            bestGapStart = cursor;
            bestGapSize  = gapSize;
        }
    }

    // 如果找到了合适间隙，使用它
    if (bestGapSize != 0xFFFFFFFFu) {
        return bestGapStart;
    }

    // 退回到文件末尾
    return (std::max)(cursor, fileEndSectors);
}
