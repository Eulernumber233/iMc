#pragma once
#include <cstdint>
#include "../core.h"
#include "../TextureMgr.h"
#include <unordered_map> 
// 方块类型枚举
enum BlockType : uint8_t {
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

struct BlockFaceType
{
    BlockType type;
    BlockFace face_id;
    // 1. 重载==运算符：实现key的相等比较（unordered_map必须）
    bool operator==(const BlockFaceType& other) const noexcept {
        // 只有两个成员都相等，才算同一个key
        return (type == other.type) && (face_id == other.face_id);
    }
    static std::unordered_map<BlockFaceType, GLuint> type_to_texture;
    static void init_type_map();

    static GLuint getTexture(BlockFaceType key);
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
struct BlockFaceKey
{
    uint8_t x;
    uint8_t y;
    uint8_t z;
    BlockFace face_id;
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