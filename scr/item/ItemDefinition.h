#pragma once
#include "../core.h"
#include "../chunk/BlockType.h"
#include <string>

// ── 物品数据资产（类 UE5 DataAsset）──────────────────────────────
// 物品的"静态定义"：一份 JSON 条目对应一个 ItemDefinition，全局唯一，
// 由 ItemRegistry 拥有并按 id 索引。运行时每格背包内容是 ItemStack（见 ItemStack.h），
// 只持有指向 ItemDefinition 的指针 + 数量 + 耐久。
//
// 数据（这里）与行为（Item 子类，见 Item.h）分离：behavior 字段决定绑定哪个
// 无状态 Item 行为对象（ItemFactory 构造），行为对 ItemStack 操作（放置减 count、
// 使用减耐久）。

// 前向声明行为基类，避免头文件循环依赖
class Item;

// 物品类别（影响默认堆叠 / 行为归类）
enum class ItemCategory {
    BLOCK,      // 可放置方块
    TOOL,       // 工具（通常有耐久、不可堆叠）
    MATERIAL,   // 材料（可堆叠）
    FOOD,       // 食物
    MISC        // 其它
};

// 掉落 / 手持时的 3D 模型来源
enum class ItemModelType {
    EXTRUDED_2D,   // 逐像素挤出 2D 图标（默认，多数物品）
    BLOCK_CUBE,    // 直接渲染方块立方体（方块物品，v1 暂统一走 EXTRUDED_2D）
    CUSTOM_MODEL   // 外部模型文件（modelPath）
};

struct ItemDefinition {
    std::string id;            // 唯一标识（= 图标文件名去扩展名，如 "stone" / "oak_log"）
    std::string displayName;   // UI 显示名
    std::string iconName;      // 注册到 TextureMgr 的纹理名（= id），供 UI 按名取用
    std::string iconPath;      // 图标 PNG 相对工作目录路径（挤出网格读像素用）

    ItemCategory category   = ItemCategory::MISC;
    int  maxStack           = 64;      // 最大堆叠（1 = 不可堆叠）
    bool hasDurability      = false;   // 是否为耐久物品
    int  maxDurability      = 0;       // 最大耐久（hasDurability 时有效）

    ItemModelType modelType = ItemModelType::EXTRUDED_2D;
    std::string   modelPath;           // CUSTOM_MODEL 时的模型路径

    BlockType   blockType   = BLOCK_AIR;  // category==BLOCK 时对应的方块
    std::string behavior    = "generic";  // 行为标识（ItemFactory 用）

    bool loadInDebug        = false;   // 调试模式下是否加载该物品图标

    // ── 运行时填充（不来自 JSON）──
    GLuint iconTexture      = 0;        // 图标 GL 纹理 id（未加载 = 0）
    GLuint guiIconTexture   = 0;        // 方块物品的等距立方体 UI 图标（RenderSystem 离屏渲染生成，非方块 = 0）
    Item*  behaviorObj      = nullptr;  // 无状态行为对象（ItemRegistry 拥有）

    // 是否为「可渲染成立方体」的方块物品
    bool isBlockItem() const {
        return category == ItemCategory::BLOCK && blockType != BLOCK_AIR;
    }
};
