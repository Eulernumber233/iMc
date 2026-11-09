#include "Item.h"
#include "item/ItemStack.h"
#include "item/ItemDefinition.h"
#include "chunk/ChunkManager.h"

// 把击中面映射到放置方块的 orient。
// 语义：原木沿击中面的法线方向延伸 —— 玩家点 UP 面就放竖直原木（轴 Y），
// 点 LEFT/RIGHT 就放沿 X 横躺，点 FRONT/BACK 就沿 Z。
// BlockFace 的数值刚好与 BlockOrient (PX..NY) 一一对应，直接强转即可。
static uint8_t orientFromHitFace(BlockFace face) {
    return static_cast<uint8_t>(face);
}

// 以方块格为单位的六个方向（+X/-X/+Z/-Z/+Y/-Y）。
static glm::ivec3 faceDir(BlockFace face) {
    switch (face) {
    case RIGHT: return { 1, 0, 0 };
    case LEFT:  return { -1, 0, 0 };
    case FRONT: return { 0, 0, 1 };
    case BACK:  return { 0, 0, -1 };
    case UP:    return { 0, 1, 0 };
    case DOWN:  return { 0, -1, 0 };
    }
    return { 0, 0, 0 };
}

bool BlockItem::onRightClick(ItemStack& stack, const PlacementContext& ctx,
                             class ChunkManager* chunkManager) {
    if (!chunkManager || stack.empty() || !stack.def) return false;

    BlockType blockType = stack.def->blockType;
    if (blockType == BLOCK_AIR) return false;

    // 检查目标位置是否是空气
    BlockState blockAtPlace = chunkManager->getBlockAt(ctx.adjacentPos);
    if (blockAtPlace.type() != BLOCK_AIR) {
        return false; // 位置非空气，不放置
    }

    BlockProperties props = GetBlockProperties(blockType);

    // 部件型方块（火把等）必须依附在一个固体方块上，且不允许挂天花板。
    // 注意 hitFace 的语义（见 Ray.cpp）：它是【放置格指向支撑方块】的方向，
    // 即 hitFace == UP 表示支撑在上方（天花板），hitFace == DOWN 表示支撑在下方（地面）。
    if (props.orientOnPlace) {
        if (ctx.hitFace == UP) return false;   // 不允许挂天花板
        glm::ivec3 supportPos = ctx.adjacentPos + faceDir(ctx.hitFace);
        BlockState support = chunkManager->getBlockAt(supportPos);
        if (!GetBlockProperties(support.type()).isSolid) return false;
    }

    uint8_t orient = (props.hasAxis || props.orientOnPlace)
        ? orientFromHitFace(ctx.hitFace)
        : ORIENT_NONE;
    chunkManager->setBlock(ctx.adjacentPos, BlockState(blockType, orient));

    // 放置成功消耗一个
    stack.count--;
    if (stack.count <= 0) stack.clear();
    return true;
}

bool SpyglassItem::onRightClick(ItemStack& stack, const PlacementContext& ctx,
                                class ChunkManager* chunkManager) {
    (void)stack; (void)ctx; (void)chunkManager;
    // 望远镜使用 - 预留接口
    useSpyglass();
    return false;
}

void SpyglassItem::useSpyglass() {
    // 望远镜使用预留接口
    // 可以在这里触发视野变化、UI提示等
    // 目前只需预留，不做具体实现
}
