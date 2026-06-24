#pragma once

#include <cstdint>
#include <cstring>
#include <cassert>
#include <string>
#include <vector>
#include <type_traits>
#include <glm/glm.hpp>
#include "../chunk/BlockType.h"

// ============================================================================
// MemoryStream: 网络序列化读写缓冲区
// ============================================================================

class MemoryStream {
public:
    MemoryStream() = default;

    void reset() { m_buffer.clear(); m_readPos = 0; }

    const uint8_t* data() const { return m_buffer.data(); }
    size_t size() const { return m_buffer.size(); }
    size_t remaining() const { return m_buffer.size() - m_readPos; }

    // ---- 写入 ----

    template<typename T>
    void writePod(const T& val) {
        static_assert(std::is_trivially_copyable_v<T>,
            "writePod requires trivially copyable type");
        const auto* src = reinterpret_cast<const uint8_t*>(&val);
        m_buffer.insert(m_buffer.end(), src, src + sizeof(T));
    }

    void writeBool(bool v) {
        m_buffer.push_back(v ? 1 : 0);
    }

    void writeString(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(s.size());
        writePod(len);
        writeBytes(s.data(), len);
    }

    void writeBytes(const void* data, size_t len) {
        const auto* src = static_cast<const uint8_t*>(data);
        m_buffer.insert(m_buffer.end(), src, src + len);
    }

    // ---- 读取 ----

    template<typename T>
    T readPod() {
        static_assert(std::is_trivially_copyable_v<T>,
            "readPod requires trivially copyable type");
        assert(m_readPos + sizeof(T) <= m_buffer.size() && "MemoryStream read overflow");
        T val;
        std::memcpy(&val, m_buffer.data() + m_readPos, sizeof(T));
        m_readPos += sizeof(T);
        return val;
    }

    bool readBool() {
        assert(m_readPos < m_buffer.size() && "MemoryStream read overflow");
        return m_buffer[m_readPos++] != 0;
    }

    std::string readString() {
        uint16_t len = readPod<uint16_t>();
        assert(m_readPos + len <= m_buffer.size() && "MemoryStream read string overflow");
        std::string result(reinterpret_cast<const char*>(m_buffer.data() + m_readPos), len);
        m_readPos += len;
        return result;
    }

    void readBytes(void* out, size_t len) {
        assert(m_readPos + len <= m_buffer.size() && "MemoryStream read bytes overflow");
        std::memcpy(out, m_buffer.data() + m_readPos, len);
        m_readPos += len;
    }

    // ---- 版本兼容 ----

    void writeVersion(uint8_t ver) { writePod(ver); }
    uint8_t readVersion() { return readPod<uint8_t>(); }

private:
    std::vector<uint8_t> m_buffer;
    size_t m_readPos = 0;
};

// ============================================================================
// NetTypeSerializer<T>: 类型序列化器（主模板 + 基础类型 + 自定义类型特化）
// ============================================================================

template<typename T>
struct NetTypeSerializer {
    static void write(MemoryStream& s, const T& val) {
        if constexpr (std::is_same_v<T, bool>) {
            s.writeBool(val);
        } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
            s.writePod(val);
        } else {
            static_assert(sizeof(T) == 0,
                "No NetTypeSerializer specialization for this type. "
                "Add a template specialization of NetTypeSerializer<T>.");
        }
    }

    static void read(MemoryStream& s, T& val) {
        if constexpr (std::is_same_v<T, bool>) {
            val = s.readBool();
        } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T>) {
            val = s.readPod<T>();
        } else {
            static_assert(sizeof(T) == 0,
                "No NetTypeSerializer specialization for this type. "
                "Add a template specialization of NetTypeSerializer<T>.");
        }
    }
};

// ---- glm::vec3 ----
template<>
struct NetTypeSerializer<glm::vec3> {
    static void write(MemoryStream& s, const glm::vec3& v) {
        s.writePod(v.x);
        s.writePod(v.y);
        s.writePod(v.z);
    }
    static void read(MemoryStream& s, glm::vec3& v) {
        v.x = s.readPod<float>();
        v.y = s.readPod<float>();
        v.z = s.readPod<float>();
    }
};

// ---- glm::ivec3 ----
template<>
struct NetTypeSerializer<glm::ivec3> {
    static void write(MemoryStream& s, const glm::ivec3& v) {
        s.writePod(v.x);
        s.writePod(v.y);
        s.writePod(v.z);
    }
    static void read(MemoryStream& s, glm::ivec3& v) {
        v.x = s.readPod<int32_t>();
        v.y = s.readPod<int32_t>();
        v.z = s.readPod<int32_t>();
    }
};

// ---- BlockState (2 字节) ----
template<>
struct NetTypeSerializer<BlockState> {
    static void write(MemoryStream& s, const BlockState& v) {
        s.writePod(v.bits);
    }
    static void read(MemoryStream& s, BlockState& v) {
        v.bits = s.readPod<uint16_t>();
    }
};

// ---- std::string ----
template<>
struct NetTypeSerializer<std::string> {
    static void write(MemoryStream& s, const std::string& v) {
        s.writeString(v);
    }
    static void read(MemoryStream& s, std::string& v) {
        v = s.readString();
    }
};
