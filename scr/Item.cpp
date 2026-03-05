#include "Item.h"
#include "chunk/ChunkManager.h"

bool BlockItem::onRightClick(const glm::ivec3& adjacentPos, class ChunkManager* chunkManager) {
    if (!chunkManager) return false;

    // 检查目标位置是否是空气
    BlockType blockAtPlace = chunkManager->getBlockAt(adjacentPos);
    if (blockAtPlace == BLOCK_AIR) {
        // 放置方块
        chunkManager->setBlock(adjacentPos, m_blockType);
        return true; // 消耗点击
    }
    return false; // 位置非空气，不放置
}

bool SpyglassItem::onRightClick(const glm::ivec3& adjacentPos, class ChunkManager* chunkManager) {
    // 望远镜使用 - 预留接口
    // 这里可以触发望远镜观察效果，例如放大视野
    // 暂时不消耗点击，允许玩家继续其他操作
    useSpyglass();
    return false;
}

void SpyglassItem::useSpyglass() {
    // 望远镜使用预留接口
    // 可以在这里触发视野变化、UI提示等
    // 目前只需预留，不做具体实现
}