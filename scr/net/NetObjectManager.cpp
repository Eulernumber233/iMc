#include "NetObjectManager.h"

void NetObjectManager::destroyObject(uint16_t netId) {
    m_objects.erase(netId);
}

NetObject* NetObjectManager::findObject(uint16_t netId) const {
    auto it = m_objects.find(netId);
    return (it != m_objects.end()) ? it->second.get() : nullptr;
}

NetObject* NetObjectManager::addObject(uint16_t netId, std::unique_ptr<NetObject> obj) {
    obj->setNetId(netId);
    NetObject* ptr = obj.get();
    m_objects[netId] = std::move(obj);
    return ptr;
}

void NetObjectManager::collectDirty(std::vector<NetMessage>& outReliable,
                                    std::vector<NetMessage>& outUnreliable) {
    for (auto& pair : m_objects) {
        NetObject* obj = pair.second.get();
        if (!obj->isDirty()) continue;

        // 按可靠性分流：同一个对象的脏属性，可靠和不可靠各打包为一条消息
        MemoryStream relStream;
        MemoryStream unrelStream;
        // 服务端广播：跳过 NO_REBROADCAST 属性（如整背包只上报不外发）
        obj->serializeDirtySplit(relStream, unrelStream, /*skipNoRebroadcast=*/true);

        if (relStream.size() > 0) {
            outReliable.push_back(NetMessage::propertySync(obj->getNetId(), relStream));
        }
        if (unrelStream.size() > 0) {
            outUnreliable.push_back(NetMessage::propertySync(obj->getNetId(), unrelStream));
        }
    }
}
