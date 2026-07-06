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

    // 属性标志位（正交于可靠性）
    // NO_REBROADCAST：服务端收到 owner 客户端的该属性更新后，应用但**不**再广播给其他客户端。
    //   用于「只上报服务端」的数据（如整背包：服务端做持久化，别的客户端不需要）。
    static constexpr uint8_t PROP_NO_REBROADCAST = 0x01;

    struct Property {
        uint16_t id;
        size_t offset;               // offsetof(SubClass, field)
        NetReliability reliability;
        uint8_t minResendIntervalMs;  // 0 = 每帧可发
        uint8_t flags = 0;            // PROP_* 位组合
        OnRepFunc onRep;              // 可为 nullptr
        SerializeFunc serialize;
        DeserializeFunc deserialize;

        bool hasFlag(uint8_t f) const { return (flags & f) != 0; }
    };

    NetObject() = default;
    virtual ~NetObject() = default;

    // ---- 属性注册 ----

    template<typename T>
    uint16_t registerPropImpl(uint16_t id, size_t offset, NetReliability rel,
                              OnRepFunc onRep = nullptr, uint8_t minResendMs = 0,
                              uint8_t flags = 0);

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

    // 将脏属性按可靠性拆进两条流（Reliable → rel，Unreliable → unrel）。
    // 客户端上报自身对象、服务端 collectDirty 均复用，保证 Reliable 属性走可靠通道。
    // skipNoRebroadcast=true（服务端广播用）时跳过 PROP_NO_REBROADCAST 属性
    //（如整背包：客户端上报服务端持久化，但服务端不外发给其他客户端）。
    void serializeDirtySplit(MemoryStream& rel, MemoryStream& unrel,
                             bool skipNoRebroadcast = false);

    // 从 MemoryStream 读取并应用属性，自动触发 onRep 回调。
    // applied != nullptr 时，把实际应用到的 propId 追加进去（服务端据此决定回广播哪些属性）。
    void deserialize(MemoryStream& s, std::vector<uint16_t>* applied = nullptr);

    // ---- ID / 归属 ----

    void setNetId(uint16_t id) { m_netId = id; }
    uint16_t getNetId() const { return m_netId; }

    // owner = 拥有该对象权威的 playerId（0 = 服务端/世界所有）。
    // 客户端每帧只向服务端上报「自己 owner」的对象；服务端权威对象 owner 为 0。
    void setOwnerId(uint16_t id) { m_ownerId = id; }
    uint16_t getOwnerId() const { return m_ownerId; }

protected:
    std::vector<Property> m_properties;
    std::vector<uint16_t> m_dirtyProps;
    uint16_t m_netId = 0;    // 由 NetObjectManager 分配
    uint16_t m_ownerId = 0;  // 权威所有者 playerId
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
                                     OnRepFunc onRep, uint8_t minResendMs, uint8_t flags) {
    if (id >= m_properties.size()) {
        m_properties.resize(id + 1);
    }
    Property& prop = m_properties[id];
    prop.id = id;
    prop.offset = offset;
    prop.reliability = rel;
    prop.minResendIntervalMs = minResendMs;
    prop.flags = flags;
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

// 注册一个带标志位（PROP_* 组合）的属性（有 OnRep 回调）
#define REGISTER_PROP_FLAGS(Name, Reliability, Callback, Flags) \
    registerPropImpl<decltype(Name)>(__propId++, offsetof(__NetSelf, Name), \
        NetReliability::Reliability, \
        static_cast<NetObject::OnRepFunc>([](NetObject* o) { \
            static_cast<__NetSelf*>(o)->Callback(); }), 0, (Flags))

// 注册一个带标志位的属性（无 OnRep 回调）
#define REGISTER_PROP_NOREP_FLAGS(Name, Reliability, Flags) \
    registerPropImpl<decltype(Name)>(__propId++, offsetof(__NetSelf, Name), \
        NetReliability::Reliability, nullptr, 0, (Flags))

// 结束属性注册块（预留扩展点）
#define REGISTER_PROP_END()
