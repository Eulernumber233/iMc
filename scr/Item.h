#pragma once
#include "chunk/BlockType.h"
#include <string>

// 前向声明：物品栈（运行时每格内容，见 item/ItemStack.h）
struct ItemStack;

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

// ── 物品行为基类（无状态）─────────────────────────────────────────
// 数据（名称 / 图标 / 堆叠 / 耐久 / 关联方块）已迁到 ItemDefinition（数据资产）；
// Item 只负责"行为"。每个 ItemDefinition 由 ItemFactory 绑定一个共享的无状态
// Item 行为对象；行为方法收 ItemStack&，可修改其 count / durability（放置减 1、
// 使用减耐久等）。
class Item {
public:
    virtual ~Item() = default;

    // 右键点击行为。stack 为发起交互的物品栈（可被修改）。
    // 返回值：是否消耗了这次点击（放置方块返回 true，望远镜观察返回 false）。
    virtual bool onRightClick(ItemStack& stack, const PlacementContext& ctx,
                              class ChunkManager* chunkManager) = 0;

    // 左键点击行为（默认返回 false，表示不改变默认破坏行为）。
    virtual bool onLeftClick(ItemStack& stack, const PlacementContext& ctx,
                             class ChunkManager* chunkManager) {
        (void)stack; (void)ctx; (void)chunkManager; return false;
    }
};

// 方块物品行为：右键在相邻空气格放置对应方块，成功后消耗一个。
// 方块类型从 stack.def->blockType 读取（不再自持状态）。
class BlockItem : public Item {
public:
    bool onRightClick(ItemStack& stack, const PlacementContext& ctx,
                      class ChunkManager* chunkManager) override;
};

// 望远镜物品行为（预留）
class SpyglassItem : public Item {
public:
    bool onRightClick(ItemStack& stack, const PlacementContext& ctx,
                      class ChunkManager* chunkManager) override;
    void useSpyglass();
};

// 通用物品行为：右键 / 左键都不做事（材料、暂无功能的物品）
class GenericItem : public Item {
public:
    bool onRightClick(ItemStack& stack, const PlacementContext& ctx,
                      class ChunkManager* chunkManager) override {
        (void)stack; (void)ctx; (void)chunkManager; return false;
    }
};
