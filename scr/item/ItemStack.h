#pragma once
#include "ItemDefinition.h"

// ── 物品栈（背包每格的运行时内容）────────────────────────────────
// 只持有指向静态 ItemDefinition 的指针 + 本格数量 + 剩余耐久。
// def == nullptr 或 count <= 0 视为空格。
struct ItemStack {
    const ItemDefinition* def = nullptr;
    int count      = 0;
    int durability = 0;   // 剩余耐久（def->hasDurability 时有效）

    ItemStack() = default;
    explicit ItemStack(const ItemDefinition* d, int c = 1)
        : def(d), count(c),
          durability(d && d->hasDurability ? d->maxDurability : 0) {}

    bool empty() const { return def == nullptr || count <= 0; }

    void clear() { def = nullptr; count = 0; durability = 0; }

    int  maxStack() const { return def ? def->maxStack : 64; }

    // 两个栈是否可合并（同一定义且都非空）
    bool sameItem(const ItemStack& o) const {
        return def != nullptr && def == o.def;
    }
};
