#pragma once
#include "ItemDefinition.h"
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>

// ── 物品注册表（单例）────────────────────────────────────────────
// 加载 assert/item_registry.json 里的全部物品数据资产。调试模式下
// （RuntimeConfig::debugMode）只加载标记 load_in_debug 的物品图标 GL 纹理，
// 避免启动时加载全量 600+ 张图。
//
// 用法：ItemRegistry::instance().load() 需在有效 GL 上下文下调用一次
// （图标要创建 GL 纹理）；之后 get(id) 取定义。返回的指针在注册表生命周期内稳定。
class ItemRegistry {
public:
    static ItemRegistry& instance();

    // 解析 JSON 并加载（按 debug 过滤）图标。需在 GL 上下文就绪后调用。
    void load(const std::string& jsonPath = "assert/item_registry.json");

    // 按 id 取定义；不存在返回 nullptr。
    const ItemDefinition* get(const std::string& id) const;

    // 取第一个 category==BLOCK 且 blockType 匹配的定义（破坏方块掉落用）；无则 nullptr。
    const ItemDefinition* getByBlockType(BlockType type) const;

    // 遍历所有已注册定义
    void forEach(const std::function<void(const ItemDefinition&)>& fn) const;

    // 可变遍历（供 RenderSystem 生成方块立方体 UI 图标后回填 guiIconTexture）
    void forEachMutable(const std::function<void(ItemDefinition&)>& fn);

    size_t count() const { return m_defs.size(); }
    size_t loadedIconCount() const { return m_loadedIcons; }

private:
    ItemRegistry() = default;
    ItemRegistry(const ItemRegistry&) = delete;
    ItemRegistry& operator=(const ItemRegistry&) = delete;

    std::unordered_map<std::string, ItemDefinition> m_defs;
    bool   m_loaded = false;
    size_t m_loadedIcons = 0;
};
