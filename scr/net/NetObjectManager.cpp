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

        for (uint16_t propId : obj->getDirtyProps()) {
            auto* prop = obj->getProperty(propId);
            if (!prop) continue;

            if (prop->reliability == NetReliability::Reliable) {
                relStream.writePod(propId);
                prop->serialize(obj, prop->offset, relStream);
            } else {
                unrelStream.writePod(propId);
                prop->serialize(obj, prop->offset, unrelStream);
            }
        }

        if (relStream.size() > 0) {
            outReliable.push_back(NetMessage::propertySync(obj->getNetId(), relStream));
        }
        if (unrelStream.size() > 0) {
            outUnreliable.push_back(NetMessage::propertySync(obj->getNetId(), unrelStream));
        }
    }
}
