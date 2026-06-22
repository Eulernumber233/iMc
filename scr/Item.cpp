#include "Item.h"
#include "chunk/ChunkManager.h"

// 把击中面映射到放置方块的 orient。
// 语义：原木沿击中面的法线方向延伸 —— 玩家点 UP 面就放竖直原木（轴 Y），
// 点 LEFT/RIGHT 就放沿 X 横躺，点 FRONT/BACK 就沿 Z。
// BlockFace 的数值刚好与 BlockOrient (PX..NY) 一一对应，直接强转即可。
static uint8_t orientFromHitFace(BlockFace face) {
    return static_cast<uint8_t>(face);
}

bool BlockItem::onRightClick(const PlacementContext& ctx, class ChunkManager* chunkManager) {
    if (!chunkManager) return false;

    // 检查目标位置是否是空气
    BlockState blockAtPlace = chunkManager->getBlockAt(ctx.adjacentPos);
    if (blockAtPlace.type() != BLOCK_AIR) {
        return false; // 位置非空气，不放置
    }

    uint8_t orient = GetBlockProperties(m_blockType).hasAxis
        ? orientFromHitFace(ctx.hitFace)
        : ORIENT_NONE;
    chunkManager->setBlock(ctx.adjacentPos, BlockState(m_blockType, orient));
    return true;
}

bool SpyglassItem::onRightClick(const PlacementContext& ctx, class ChunkManager* chunkManager) {
    (void)ctx; (void)chunkManager;
    // 望远镜使用 - 预留接口
    useSpyglass();
    return false;
}

void SpyglassItem::useSpyglass() {
    // 望远镜使用预留接口
    // 可以在这里触发视野变化、UI提示等
    // 目前只需预留，不做具体实现
}