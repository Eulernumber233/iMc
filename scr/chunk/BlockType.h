#pragma once
#include <cstdint>
#include "../core.h"
#include "../TextureMgr.h"
#include <functional>
#include <unordered_map> 
// 方块类型枚举
enum BlockType : uint8_t {
	BLOCK_ERRER = 255, // 错误类型
    BLOCK_AIR = 0,
    BLOCK_STONE = 1,
    BLOCK_DIRT = 2,
    BLOCK_GRASS = 3,
    BLOCK_WATER = 4,
    BLOCK_SAND = 5,
    BLOCK_WOOD = 6,
    BLOCK_LEAVES = 7,
    BLOCK_COUNT  // 方块类型总数 
};
enum BlockFace :uint8_t {
    RIGHT = 0,
    LEFT = 1,
    FRONT = 2,
    BACK = 3,
    UP = 4,
    DOWN = 5,
};
struct BlockFaceLocKey
{
    uint8_t x;
    uint8_t y;
    uint8_t z;
    BlockFace face_id;

    bool operator==(const BlockFaceLocKey& other) const noexcept {
        // 只有两个成员都相等，才算同一个key
        return (x == other.x) && (y == other.y) && (z == other.z) && (face_id == other.face_id);
    }

    BlockFaceLocKey(const uint8_t& x, const uint8_t& y, const uint8_t& z, const BlockFace& face_id)
        : x(x), y(y), z(z), face_id(face_id)
    {
    }
};
namespace std {
    template<>
    struct hash<BlockFaceLocKey> {
        inline size_t operator()(const BlockFaceLocKey& bf) const noexcept {
            size_t hash_x = hash<uint8_t>{}(static_cast<uint8_t>(bf.x));
            size_t hash_y = hash<uint8_t>{}(static_cast<uint8_t>(bf.y));
            size_t hash_z = hash<uint8_t>{}(static_cast<uint8_t>(bf.z));
            size_t hash_face = hash<uint8_t>{}(static_cast<uint8_t>(bf.face_id));
            return hash_face | (hash_x << 8) | (hash_y << 16) | (hash_z << 24);
        }
    };
}
struct BlockFaceType
{
    BlockType type;
    BlockFace face_id;
    // 1. 重载==运算符：实现key的相等比较（unordered_map必须）
    bool operator==(const BlockFaceType& other) const noexcept {
        // 只有两个成员都相等，才算同一个key
        return (type == other.type) && (face_id == other.face_id);
    }
    // 改为存储层索引 (int)
    static std::unordered_map<BlockFaceType, int> type_to_texture;
    static void init_type_map();
    static int getTextureLayer(BlockFaceType key);

    BlockFaceType(const BlockType& type, const BlockFace& face_id)
        : type(type), face_id(face_id)
    {
    }
};
namespace std {
    template<>
    struct hash<BlockFaceType> {
        size_t operator()(const BlockFaceType& bf) const noexcept {
            // 组合type和face_id的哈希（显式转uint8_t避免类型问题）
            size_t hash_type = hash<uint8_t>{}(static_cast<uint8_t>(bf.type));
            size_t hash_face = hash<uint8_t>{}(static_cast<uint8_t>(bf.face_id));
            return hash_type ^ (hash_face << 1);
        }
    };
}

// 绘制是需将 DrawFaceKey 转化为 InstanceData 进行实例化渲染
struct InstanceData {
    glm::vec3 position;   // 方块中心世界坐标 (x+0.5, y+0.5, z+0.5)
    int faceIndex;        // 面索引 (0-5)
    int blockType;        // 方块类型枚举值
    int textureLayer;     // 纹理数组层索引（由CPU预先计算）

    InstanceData(const glm::vec3& position, BlockFace faceIndex, BlockType blockType, int textureLayer)
        : position(position), faceIndex(faceIndex), blockType(blockType), textureLayer(textureLayer)
    {
    }
    static BlockFaceLocKey ChangeToBlockFaceLocKey(const InstanceData& data) {
        return BlockFaceLocKey(
            static_cast<uint8_t>(data.position.x - 0.5f)% ChunkConstants::CHUNK_WIDTH, // 转回方块坐标
            static_cast<uint8_t>(data.position.y - 0.5f),
            static_cast<uint8_t>(data.position.z - 0.5f)%ChunkConstants::CHUNK_DEPTH,
            static_cast<BlockFace>(data.faceIndex)
        );
	}
};

// 获取方块名称
inline const char* GetBlockName(BlockType type) {
    switch (type) {
    case BLOCK_AIR:   return "Air";
    case BLOCK_STONE: return "Stone";
    case BLOCK_DIRT:  return "Dirt";
    case BLOCK_GRASS: return "Grass";
    case BLOCK_WATER: return "Water";
    case BLOCK_SAND:  return "Sand";
    case BLOCK_WOOD:  return "Wood";
    case BLOCK_LEAVES:return "Leaves";
    default:          return "Unknown";
    }
}

// 方块属性
struct BlockProperties {
    bool isTransparent;   // 是否透明
    bool isSolid;         // 是否是固体
    glm::vec3 color;      // 基础颜色
    float emissive;       // 自发光强度


    //BlockProperties(bool isTransparent, bool isSolid, const glm::vec3& color, float emissive)
    //    : isTransparent(isTransparent), isSolid(isSolid), color(color), emissive(emissive)
    //{
    //}
};

// 获取方块属性
inline BlockProperties GetBlockProperties(BlockType type) {
    switch (type) {
    case BLOCK_AIR:
        return { true, false, glm::vec3(0.0f), 0.0f };
    case BLOCK_STONE:
        return { false, true, glm::vec3(0.5f, 0.5f, 0.5f), 0.0f };
    case BLOCK_DIRT:
        return { false, true, glm::vec3(0.4f, 0.3f, 0.2f), 0.0f };
    case BLOCK_GRASS:
        return { false, true, glm::vec3(0.2f, 0.6f, 0.3f), 0.0f };
    case BLOCK_WATER:
        return { true, false, glm::vec3(0.0f, 0.3f, 0.8f), 0.1f };
    case BLOCK_SAND:
        return { false, true, glm::vec3(0.9f, 0.8f, 0.6f), 0.0f };
    case BLOCK_WOOD:
        return { false, true, glm::vec3(0.5f, 0.35f, 0.2f), 0.0f };
    case BLOCK_LEAVES:
        return { true, true, glm::vec3(0.2f, 0.5f, 0.2f), 0.0f };
    default:
        return { false, true, glm::vec3(1.0f, 0.0f, 1.0f), 0.0f }; // 错误颜色
    }
}