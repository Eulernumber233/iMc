#pragma once
#include <cstdint>
#include <cstdio>
#include <array>
#include <string>
#include <vector>
#include <ctime>

// region 文件底层 I/O。格式借鉴 Minecraft Anvil：
//   - 32×32 chunk 一片 region，文件命名 r.<regionX>.<regionZ>.mca
//   - Header 8KB：1024 个 offset (3B sector offset + 1B sector count) + 1024 个 timestamp
//   - Chunk 数据：4B uncompressed_size + 1B compression + N B data，按 4KB 对齐
//
// 线程安全：本类不锁。调用方 (ChunkSaveManager) 用 m_ioMutex 串行化所有 I/O。
class RegionFile {
public:
    static constexpr int REGION_SIZE   = 32;
    static constexpr int SECTOR_SIZE   = 4096;
    static constexpr int HEADER_BYTES  = 8192;
    static constexpr int MAX_CHUNKS    = REGION_SIZE * REGION_SIZE; // 1024

    // 压缩类型
    enum CompressType : uint8_t {
        COMPRESS_NONE = 0,
        COMPRESS_ZLIB = 1,
    };

    explicit RegionFile(const std::string& path);
    ~RegionFile();

    bool openForRead();
    bool openForWrite();  // 不存在则创建
    void close();
    bool isOpen() const { return m_file != nullptr; }

    // 读取 chunk（解压后的 TLV 流）。chunk 不存在返回 false。
    bool readChunk(int localX, int localZ, std::vector<uint8_t>& outData);

    // 写入 chunk（未压缩的 TLV 流，内部自动压缩）。
    void writeChunk(int localX, int localZ, const std::vector<uint8_t>& data);

    // chunk 在 header 中是否有非零 offset
    bool hasChunk(int localX, int localZ) const;

private:
    static int index(int lx, int lz) { return ((lz & 31) << 5) | (lx & 31); }

    void readHeader();
    void writeHeader();

    // 在 header 中扫描 best-fit 空闲区间，或分配在文件末尾。返回 sector 偏移。
    uint32_t allocateSectors(uint8_t sectorCount, uint32_t oldOffset);

    std::string   m_path;
    FILE*         m_file = nullptr;

    // offset: 低 24bit = sector 起始偏移，高 8bit = sector 个数。0 = 未使用。
    std::array<uint32_t, MAX_CHUNKS> m_offsets{};
    std::array<uint32_t, MAX_CHUNKS> m_timestamps{};
};
