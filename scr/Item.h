#pragma once
#include "chunk/BlockType.h"
#include <string>
#include <memory>

// 放置 / 交互上下文。
//   adjacentPos  ：要交互的目标格子（右键放置时是相邻空气格的世界坐标；
//                  左键破坏时由调用方传选中方块的位置）。
//   hitFace      ：玩家视线击中被选中方块的面（RIGHT/LEFT/FRONT/BACK/UP/DOWN）。
//                  对带轴向方块（如原木）放置时用来决定 orient。
//   playerForward：玩家朝向单位向量；某些方块（楼梯 / 半砖）需要它来选朝向。
//                  当前实现只用 hitFace，但接口先留好。
struct PlacementContext {
    glm::ivec3 adjacentPos;
    BlockFace  hitFace;
    glm::vec3  playerForward;
};

// 物品基类
class Item {
public:
    virtual ~Item() = default;

    // 获取物品显示名称
    virtual std::string getName() const = 0;

    // 获取物品图标纹理名称（用于UI显示）
    virtual std::string getIconTextureName() const = 0;

    // 右键点击行为
    // 返回值：是否消耗了这次点击（例如放置方块返回true，望远镜观察返回false）
    virtual bool onRightClick(const PlacementContext& ctx, class ChunkManager* chunkManager) = 0;

    // 左键点击行为（破坏方块） - 默认返回false，表示不改变默认破坏行为
    // 可以用于工具类物品（如斧头加速破坏），但普通物品不需要
    virtual bool onLeftClick(const PlacementContext& ctx, class ChunkManager* chunkManager) { (void)ctx; (void)chunkManager; return false; }

    // 是否为方块物品
    virtual bool isBlockItem() const { return false; }

    // 如果是方块物品，获取对应的方块类型
    virtual BlockType getBlockType() const { return BLOCK_AIR; }
};

// 方块物品类
class BlockItem : public Item {
private:
    BlockType m_blockType;
    std::string m_name;
    std::string m_iconTextureName;

public:
    BlockItem(BlockType blockType, const std::string& name, const std::string& iconTextureName)
        : m_blockType(blockType), m_name(name), m_iconTextureName(iconTextureName) {}

    std::string getName() const override { return m_name; }
    std::string getIconTextureName() const override { return m_iconTextureName; }

    bool onRightClick(const PlacementContext& ctx, class ChunkManager* chunkManager) override;

    bool isBlockItem() const override { return true; }
    BlockType getBlockType() const override { return m_blockType; }
};

// 望远镜物品类
class SpyglassItem : public Item {
private:
    std::string m_name;
    std::string m_iconTextureName;

public:
    SpyglassItem(const std::string& name, const std::string& iconTextureName)
        : m_name(name), m_iconTextureName(iconTextureName) {}

    std::string getName() const override { return m_name; }
    std::string getIconTextureName() const override { return m_iconTextureName; }

    bool onRightClick(const PlacementContext& ctx, class ChunkManager* chunkManager) override;

    // 望远镜使用接口（预留）
    void useSpyglass();
};