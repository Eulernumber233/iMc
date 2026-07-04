#pragma once
#include <string>

class Item;

// ── 物品行为工厂 ────────────────────────────────────────────────
// 把 JSON 的 behavior 字符串映射到一个无状态 Item 行为对象。
// 行为对象是无状态的，故按类型共享单例（所有方块物品共用同一个 BlockItem 实例，
// 行为方法收 ItemStack& 拿到具体数据）。返回的指针由工厂持有，进程生命周期内有效。
namespace ItemFactory {
    // 返回 behavior 对应的共享行为对象；未知 behavior 回退到 generic。
    Item* getBehavior(const std::string& behavior);
}
