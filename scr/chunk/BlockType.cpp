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
std::unordered_map<BlockFaceType, int> BlockFaceType::type_to_texture;

int BlockFaceType::getTextureLayer(BlockFaceType key) {
    auto it = type_to_texture.find(key);
    return (it != type_to_texture.end()) ? it->second : -1;
}

void BlockFaceType::init_type_map()
{
    auto texMgr = TextureMgr::GetInstance();
    // stone
    type_to_texture[{ BLOCK_STONE, RIGHT }] = texMgr->GetTextureLayerIndex("stone");
    type_to_texture[{ BLOCK_STONE, LEFT }] = texMgr->GetTextureLayerIndex("stone");
    type_to_texture[{ BLOCK_STONE, UP }] = texMgr->GetTextureLayerIndex("stone");
    type_to_texture[{ BLOCK_STONE, DOWN }] = texMgr->GetTextureLayerIndex("stone");
    type_to_texture[{ BLOCK_STONE, FRONT }] = texMgr->GetTextureLayerIndex("stone");
    type_to_texture[{ BLOCK_STONE, BACK }] = texMgr->GetTextureLayerIndex("stone");

    // grass
    type_to_texture[{ BLOCK_GRASS, RIGHT }] = texMgr->GetTextureLayerIndex("grass_block_side");
    type_to_texture[{ BLOCK_GRASS, LEFT }] = texMgr->GetTextureLayerIndex("grass_block_side");
    type_to_texture[{ BLOCK_GRASS, UP }] = texMgr->GetTextureLayerIndex("grass_block_top");
    type_to_texture[{ BLOCK_GRASS, DOWN }] = texMgr->GetTextureLayerIndex("dirt");
    type_to_texture[{ BLOCK_GRASS, FRONT }] = texMgr->GetTextureLayerIndex("grass_block_side");
    type_to_texture[{ BLOCK_GRASS, BACK }] = texMgr->GetTextureLayerIndex("grass_block_side");

    // DIRT
    type_to_texture[{ BLOCK_DIRT, RIGHT }] = texMgr->GetTextureLayerIndex("dirt");
    type_to_texture[{ BLOCK_DIRT, LEFT }] = texMgr->GetTextureLayerIndex("dirt");
    type_to_texture[{ BLOCK_DIRT, UP }] = texMgr->GetTextureLayerIndex("dirt");
    type_to_texture[{ BLOCK_DIRT, DOWN }] = texMgr->GetTextureLayerIndex("dirt");
    type_to_texture[{ BLOCK_DIRT, FRONT }] = texMgr->GetTextureLayerIndex("dirt");
    type_to_texture[{ BLOCK_DIRT, BACK }] = texMgr->GetTextureLayerIndex("dirt");

    // BLOCK_WOOD
    type_to_texture[{ BLOCK_WOOD, RIGHT }] = texMgr->GetTextureLayerIndex("oak_log");
    type_to_texture[{ BLOCK_WOOD, LEFT }] = texMgr->GetTextureLayerIndex("oak_log");
    type_to_texture[{ BLOCK_WOOD, UP }] = texMgr->GetTextureLayerIndex("oak_log_top");
    type_to_texture[{ BLOCK_WOOD, DOWN }] = texMgr->GetTextureLayerIndex("oak_log_top");
    type_to_texture[{ BLOCK_WOOD, FRONT }] = texMgr->GetTextureLayerIndex("oak_log");
    type_to_texture[{ BLOCK_WOOD, BACK }] = texMgr->GetTextureLayerIndex("oak_log");
}

