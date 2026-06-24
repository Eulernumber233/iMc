#pragma once

#include "NetCommon.h"
#include "NetSerializer.h"
#include <vector>
#include <cstdint>
#include <cstddef>

// ============================================================================
// NetObject: 可网络同步的对象基类
// ============================================================================

class NetObject {
public:
    using SerializeFunc   = void (*)(const NetObject*, size_t offset, MemoryStream&);
    using DeserializeFunc = void (*)(NetObject*, size_t offset, MemoryStream&);
    using OnRepFunc       = void (*)(NetObject*);

    struct Property {
        uint16_t id;
        size_t offset;               // offsetof(SubClass, field)
        NetReliability reliability;
        uint8_t minResendIntervalMs;  // 0 = 每帧可发
        OnRepFunc onRep;              // 可为 nullptr
        SerializeFunc serialize;
        DeserializeFunc deserialize;
    };

    NetObject() = default;
    virtual ~NetObject() = default;

    // ---- 属性注册 ----

    template<typename T>
    uint16_t registerPropImpl(uint16_t id, size_t offset, NetReliability rel,
                              OnRepFunc onRep = nullptr, uint8_t minResendMs = 0);

    // ---- 脏标记 ----

    void markDirty(uint16_t propId);
    bool isDirty() const { return !m_dirtyProps.empty(); }
    const std::vector<uint16_t>& getDirtyProps() const { return m_dirtyProps; }
    void clearDirty() { m_dirtyProps.clear(); }

    // 将所有已注册属性标记为脏（服务端收到客户端更新后用于触发广播）
    void markAllDirty();

    // ---- 属性查询 ----

    const std::vector<Property>& getProperties() const { return m_properties; }
    Property* getProperty(uint16_t id);

    // ---- 序列化 ----

    // 将所有脏属性写入 MemoryStream（propId + value × N）
    void serializeDirty(MemoryStream& s);

    // 从 MemoryStream 读取并应用属性，自动触发 onRep 回调
    void deserialize(MemoryStream& s);

    // ---- ID ----

    void setNetId(uint16_t id) { m_netId = id; }
    uint16_t getNetId() const { return m_netId; }

protected:
    std::vector<Property> m_properties;
    std::vector<uint16_t> m_dirtyProps;
    uint16_t m_netId = 0;  // 由 NetObjectManager 分配
};

// ============================================================================
// 模板实现
// ============================================================================

template<typename T>
struct NetPropSerializer {
    static void write(const NetObject* self, size_t offset, MemoryStream& s) {
        const T* ptr = reinterpret_cast<const T*>(
            reinterpret_cast<const uint8_t*>(self) + offset);
        NetTypeSerializer<T>::write(s, *ptr);
    }

    static void read(NetObject* self, size_t offset, MemoryStream& s) {
        T* ptr = reinterpret_cast<T*>(
            reinterpret_cast<uint8_t*>(self) + offset);
        NetTypeSerializer<T>::read(s, *ptr);
    }
};

template<typename T>
uint16_t NetObject::registerPropImpl(uint16_t id, size_t offset, NetReliability rel,
                                     OnRepFunc onRep, uint8_t minResendMs) {
    if (id >= m_properties.size()) {
        m_properties.resize(id + 1);
    }
    Property& prop = m_properties[id];
    prop.id = id;
    prop.offset = offset;
    prop.reliability = rel;
    prop.minResendIntervalMs = minResendMs;
    prop.onRep = onRep;
    prop.serialize = &NetPropSerializer<T>::write;
    prop.deserialize = &NetPropSerializer<T>::read;
    return id;
}

// ============================================================================
// 宏：REPLICATED + REGISTER_PROP
// ============================================================================

// 声明一个可同步字段（仅字段声明，不含元数据）
#define REPLICATED(Type, Name) Type Name{}

// 开始属性注册块
#define REGISTER_PROP_BEGIN(ClassName) \
    uint16_t __propId = 0; \
    using __NetSelf = ClassName;

// 注册一个属性（有 OnRep 回调）
#define REGISTER_PROP(Name, Reliability, Callback) \
    registerPropImpl<decltype(Name)>(__propId++, offsetof(__NetSelf, Name), \
        NetReliability::Reliability, \
        static_cast<NetObject::OnRepFunc>([](NetObject* o) { \
            static_cast<__NetSelf*>(o)->Callback(); }))

// 注册一个属性（无 OnRep 回调）
#define REGISTER_PROP_NOREP(Name, Reliability) \
    registerPropImpl<decltype(Name)>(__propId++, offsetof(__NetSelf, Name), \
        NetReliability::Reliability, nullptr)

// 结束属性注册块（预留扩展点）
#define REGISTER_PROP_END()
