#include "NetObject.h"

void NetObject::markDirty(uint16_t propId) {
    if (propId >= m_properties.size()) return;
    // 避免重复
    for (uint16_t id : m_dirtyProps) {
        if (id == propId) return;
    }
    m_dirtyProps.push_back(propId);
}

void NetObject::markAllDirty() {
    m_dirtyProps.clear();
    for (uint16_t i = 0; i < m_properties.size(); ++i) {
        m_dirtyProps.push_back(i);
    }
}

NetObject::Property* NetObject::getProperty(uint16_t id) {
    if (id < m_properties.size()) {
        return &m_properties[id];
    }
    return nullptr;
}

void NetObject::serializeDirty(MemoryStream& s) {
    for (uint16_t propId : m_dirtyProps) {
        if (propId >= m_properties.size()) continue;
        auto& prop = m_properties[propId];
        s.writePod(propId);
        prop.serialize(this, prop.offset, s);
    }
}

void NetObject::deserialize(MemoryStream& s) {
    while (s.remaining() >= sizeof(uint16_t)) {
        uint16_t propId = s.readPod<uint16_t>();
        if (propId >= m_properties.size()) break;
        auto& prop = m_properties[propId];
        prop.deserialize(this, prop.offset, s);
        if (prop.onRep) {
            prop.onRep(this);
        }
    }
}
