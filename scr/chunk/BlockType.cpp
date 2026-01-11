#include "BlockType.h"
#include <functional>
#include <unordered_map> 


//// 特化std::hash<BlockFaceType>
//namespace std {
//    template<>
//    struct hash<BlockFaceType> {
//        size_t operator()(const BlockFaceType& bf) const noexcept {
//            // 显式转换枚举为uint8_t，确保哈希函数能处理
//            size_t hash_type = hash<uint8_t>{}(static_cast<uint8_t>(bf.type));
//            size_t hash_face = hash<uint8_t>{}(static_cast<uint8_t>(bf.face_id));
//
//            // 更健壮的哈希组合方式（减少碰撞）
//            return hash_type ^ (hash_face << 1);
//        }
//    };
//}
// 定义BlockFaceType的静态成员变量
std::unordered_map<BlockFaceType, GLuint> BlockFaceType::type_to_texture;

void BlockFaceType::init_type_map()
{
    auto& texture_all = TextureMgr::GetInstance()->GetAllTextures2D();
    // stone
    type_to_texture[{ BLOCK_STONE, RIGHT }] = texture_all["stone"];
    type_to_texture[{ BLOCK_STONE, LEFT }] = texture_all["stone"];
    type_to_texture[{ BLOCK_STONE, UP }] = texture_all["stone"];
    type_to_texture[{ BLOCK_STONE, DOWN }] = texture_all["stone"];
    type_to_texture[{ BLOCK_STONE, FRONT }] = texture_all["stone"];
    type_to_texture[{ BLOCK_STONE, BACK }] = texture_all["stone"];

    // grass
    type_to_texture[{ BLOCK_GRASS, RIGHT }] = texture_all["grass_block_side"];
    type_to_texture[{ BLOCK_GRASS, LEFT }] = texture_all["grass_block_side"];
    type_to_texture[{ BLOCK_GRASS, UP }] = texture_all["grass_block_top"];
    type_to_texture[{ BLOCK_GRASS, DOWN }] = texture_all["dirt"];
    type_to_texture[{ BLOCK_GRASS, FRONT }] = texture_all["grass_block_side"];
    type_to_texture[{ BLOCK_GRASS, BACK }] = texture_all["grass_block_side"];

    // DIRT
    type_to_texture[{ BLOCK_DIRT, RIGHT }] = texture_all["dirt"];
    type_to_texture[{ BLOCK_DIRT, LEFT }] = texture_all["dirt"];
    type_to_texture[{ BLOCK_DIRT, UP }] = texture_all["dirt"];
    type_to_texture[{ BLOCK_DIRT, DOWN }] = texture_all["dirt"];
    type_to_texture[{ BLOCK_DIRT, FRONT }] = texture_all["dirt"];
    type_to_texture[{ BLOCK_DIRT, BACK }] = texture_all["dirt"];

    // BLOCK_WOOD
    type_to_texture[{ BLOCK_WOOD, RIGHT }] = texture_all["oak_log"];
    type_to_texture[{ BLOCK_WOOD, LEFT }] = texture_all["oak_log"];
    type_to_texture[{ BLOCK_WOOD, UP }] = texture_all["oak_log_top"];
    type_to_texture[{ BLOCK_WOOD, DOWN }] = texture_all["oak_log_top"];
    type_to_texture[{ BLOCK_WOOD, FRONT }] = texture_all["oak_log"];
    type_to_texture[{ BLOCK_WOOD, BACK }] = texture_all["oak_log"];
}

GLuint BlockFaceType::getTexture(BlockFaceType key)
{
    auto it = type_to_texture.find(key);
    if (it != type_to_texture.end()) {
        return it->second;
    }
    return 0;
}