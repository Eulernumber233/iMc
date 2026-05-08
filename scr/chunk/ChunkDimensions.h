#pragma once
// 区块/section 几何常量。把它单独放一个头是为了：
//   - 改 CHUNK_HEIGHT / CHUNK_WIDTH 时只重编直接 include 这个头的少数文件
//     （Chunk.h、Section.h、TerrainGenerator.cpp、ChunkWorkerPool 等几个）
//   - 不影响完全不关心 chunk 几何的文件（UI、Player、Render 子系统）

namespace ChunkConstants {
    constexpr int CHUNK_WIDTH  = 16;
    constexpr int CHUNK_HEIGHT = 256;
    constexpr int CHUNK_DEPTH  = 16;
    constexpr int CHUNK_VOLUME = CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_DEPTH;

    // section 高度。CHUNK_HEIGHT 必须是它的倍数。
    constexpr int SECTION_HEIGHT = 16;
    constexpr int SECTION_COUNT  = CHUNK_HEIGHT / SECTION_HEIGHT;
    static_assert(CHUNK_HEIGHT % SECTION_HEIGHT == 0,
        "CHUNK_HEIGHT must be a multiple of SECTION_HEIGHT");
}
