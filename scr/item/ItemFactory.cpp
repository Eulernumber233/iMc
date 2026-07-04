#include "ItemFactory.h"
#include "../Item.h"
#include <unordered_map>
#include <memory>

namespace ItemFactory {

// 行为单例池：behavior 名 → 无状态行为对象。首次请求时惰性构造。
Item* getBehavior(const std::string& behavior) {
    static std::unordered_map<std::string, std::unique_ptr<Item>> pool;

    auto it = pool.find(behavior);
    if (it != pool.end()) return it->second.get();

    std::unique_ptr<Item> obj;
    if (behavior == "block") {
        obj = std::make_unique<BlockItem>();
    } else if (behavior == "spyglass") {
        obj = std::make_unique<SpyglassItem>();
    } else {
        // generic 及未知行为都回退到无操作行为
        obj = std::make_unique<GenericItem>();
    }

    Item* raw = obj.get();
    pool.emplace(behavior, std::move(obj));
    return raw;
}

} // namespace ItemFactory
