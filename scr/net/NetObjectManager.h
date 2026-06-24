#pragma once

#include "NetObject.h"
#include "NetMessage.h"
#include <unordered_map>
#include <memory>
#include <vector>
#include <cstdint>

// ============================================================================
// NetObjectManager: 管理所有 NetObject 的生命周期和脏属性收集
// ============================================================================

class NetObjectManager {
public:
    // 创建对象（返回 netId）
    template<typename T, typename... Args>
    T* createObject(Args&&... args);

    void destroyObject(uint16_t netId);
    NetObject* findObject(uint16_t netId) const;

    // 收集所有脏对象的属性，按可靠/不可靠分流打包
    void collectDirty(std::vector<NetMessage>& outReliable,
                      std::vector<NetMessage>& outUnreliable);

    // 创建对象后需手动分配 ID（或使用内部自增）
    NetObject* addObject(uint16_t netId, std::unique_ptr<NetObject> obj);

    const auto& allObjects() const { return m_objects; }

private:
    std::unordered_map<uint16_t, std::unique_ptr<NetObject>> m_objects;
    uint16_t m_nextNetId = 1;
};

// ---- 模板实现 ----

template<typename T, typename... Args>
T* NetObjectManager::createObject(Args&&... args) {
    uint16_t id = m_nextNetId++;
    auto obj = std::make_unique<T>(std::forward<Args>(args)...);
    obj->setNetId(id);
    T* ptr = obj.get();
    m_objects[id] = std::move(obj);
    return ptr;
}
