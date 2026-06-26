#pragma once
#include "BlockType.h"
#include "ChunkDimensions.h"
#include <array>
#include <shared_mutex>

// BlockBox：一个 section（16×16×16）的方块数据 + 一把读写锁，打包在一起。
//
// 设计要点（见 CLAUDE.md「区块数据唯一来源」）：
//  - 它是 section 方块数据的【唯一】所有者：Task 1 产出它，Task 2 把它直接共享给
//    生成出来的 Section，玩家修改、邻居 mesh 读取边界、网络序列化都走它。
//    不再有 Chunk::m_fullBlockData 那种「为邻居准备的第二份快照」。
//  - 因为含 std::shared_mutex（不可移动 / 不可拷贝），BlockBox 本身也不可移动 / 拷贝。
//    全程只通过 std::shared_ptr<BlockBox> 传递（move / 拷贝 shared_ptr 只搬指针，
//    锁对象本身原地不动，从头到尾只有一个）。
//  - 这样无论邻居处于 block-ready 还是 loaded 状态，worker 做 Task 2 时都对【同一个】
//    BlockBox 上读锁，玩家修改时对它上写锁，从根本上消除「邻居读到过期数据」的隐患。
//
// 生命周期：shared_ptr 的引用计数保证 —— 即使邻居 chunk 在 worker 读边界期间被卸载，
// worker 持有的那份 shared_ptr 也会让 BlockBox 存活到读取结束，不会悬挂。
struct BlockBox {
    // section 体积：16×16×16 = 4096
    static constexpr int VOLUME = ChunkConstants::CHUNK_WIDTH *
                                  ChunkConstants::SECTION_HEIGHT *
                                  ChunkConstants::CHUNK_DEPTH;

    std::array<BlockState, VOLUME> blocks;
    mutable std::shared_mutex mutex;

    BlockBox() { blocks.fill(BlockState{}); }

    // 禁止拷贝 / 移动（shared_mutex 已删除这些操作，这里显式声明以表意）
    BlockBox(const BlockBox&) = delete;
    BlockBox& operator=(const BlockBox&) = delete;
    BlockBox(BlockBox&&) = delete;
    BlockBox& operator=(BlockBox&&) = delete;
};

// 一个 chunk 的全部 section BlockBox（每 section 一个，含数据 + 锁）。
using ChunkBoxes = std::array<std::shared_ptr<BlockBox>,
                              ChunkConstants::CHUNK_HEIGHT / ChunkConstants::SECTION_HEIGHT>;

// 把一段「整 chunk 连续 buffer」（地形生成器 / 存档 / 网络反序列化的布局
// (worldY*DEPTH+z)*WIDTH+x）切分为 CHUNK_SECTION_COUNT 个 section BlockBox。
// 不加锁：调用时 box 都是新建、尚未对外共享。
inline void splitChunkBufferToBoxes(
    const BlockState* src,
    std::array<std::shared_ptr<BlockBox>,
               ChunkConstants::CHUNK_HEIGHT / ChunkConstants::SECTION_HEIGHT>& out)
{
    constexpr int W = ChunkConstants::CHUNK_WIDTH;
    constexpr int D = ChunkConstants::CHUNK_DEPTH;
    constexpr int SEC_H = ChunkConstants::SECTION_HEIGHT;
    constexpr int SEC_COUNT = ChunkConstants::CHUNK_HEIGHT / SEC_H;
    for (int sy = 0; sy < SEC_COUNT; ++sy) {
        auto box = std::make_shared<BlockBox>();
        BlockState* dst = box->blocks.data();
        for (int y = 0; y < SEC_H; ++y) {
            int worldY = sy * SEC_H + y;
            for (int z = 0; z < D; ++z) {
                for (int x = 0; x < W; ++x) {
                    dst[(y * D + z) * W + x] = src[(worldY * D + z) * W + x];
                }
            }
        }
        out[sy] = std::move(box);
    }
}
