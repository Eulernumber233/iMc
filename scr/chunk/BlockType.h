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
    BLOCK_GLOWSTONE = 8,  // 萤石（自发光方块）
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

// 方块朝向：4 bit 编码，与 InstanceData::packed 的 orient 字段直接对应。
//   ORIENT_PX..ORIENT_NZ —— 6 面朝向，与 BlockFace 数值一致（RIGHT/LEFT/FRONT/BACK/UP/DOWN）。
//   ORIENT_NONE = 0xF    —— 无轴向方块（泥土/草/石头等），渲染时按面独立采样。
//   剩余编码（6..14）保留给未来扩展。
enum BlockOrient : uint8_t {
    ORIENT_PX = 0,   // +X
    ORIENT_NX = 1,   // -X
    ORIENT_PZ = 2,   // +Z
    ORIENT_NZ = 3,   // -Z
    ORIENT_PY = 4,   // +Y
    ORIENT_NY = 5,   // -Y
    ORIENT_NONE = 0xF
};


// 方块状态：CPU 端 16 bit 紧凑布局，Section 的 m_blocks 单元格用它。
//   bits  0..7  type     (BlockType)
//   bits  8..11 orient   (BlockOrient，4 bit；0xF 表示无轴向)
//   bits 12..15 reserved (水位/生长阶段/亮度等，后续扩展)
//
// 区块层（Section / Chunk / ChunkManager / Ray::HitResult 等）对外接口
// 统一传 BlockState；调用方拿到后用 state.type() / state.orient() 自取。
struct BlockState {
    uint16_t bits;

    constexpr BlockState() noexcept : bits(0) {}
    constexpr BlockState(uint16_t b) noexcept : bits(b) {}
    constexpr BlockState(BlockType t, uint8_t o = ORIENT_NONE) noexcept
        : bits(uint16_t(uint16_t(t) | (uint16_t(o & 0xFu) << 8))) {}

    constexpr BlockType type() const noexcept { return BlockType(bits & 0xFFu); }
    constexpr uint8_t   orient() const noexcept { return uint8_t((bits >> 8) & 0xFu); }

    void setType(BlockType t) noexcept {
        bits = uint16_t((bits & 0xFF00u) | uint16_t(t));
    }
    void setOrient(uint8_t o) noexcept {
        bits = uint16_t((bits & 0xF0FFu) | (uint16_t(o & 0xFu) << 8));
    }

    bool operator==(const BlockState& other) const noexcept { return bits == other.bits; }
    bool operator!=(const BlockState& other) const noexcept { return bits != other.bits; }
};
static_assert(sizeof(BlockState) == 2, "BlockState must be 2 bytes");
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

    // 带轴方块（hasAxis）的"侧面层"和"端面层"。约定：
    //   - 侧面层 side_layer_by_type[t]：t 不是 hasAxis 时为 -1；hasAxis 时是侧面图。
    //   - 端面层 end_layer_by_type[t] ：同上。
    // 这两个表的作用：CPU 端在 addFaceLocal 时如果发现某方块有 side_layer 注册，
    // 就把侧面层填到 InstanceData.textureLayer（忽略 face），让所有面默认拿侧面图；
    // shader 端再按 orient 决定哪两个面切换到 endLayer。无注册（-1）的方块走"按 face 查
    // BlockFaceType::type_to_texture"的老路径——这样草方块这种"6 面 3 张图但不带轴"的方块
    // 不受影响。
    static constexpr int kLayerLookupSize = 256;
    static int side_layer_by_type[kLayerLookupSize];
    static int end_layer_by_type[kLayerLookupSize];
    static const int* getSideLayerLookup() { return side_layer_by_type; }
    static const int* getEndLayerLookup()  { return end_layer_by_type;  }
    static int getSideLayer(BlockType t) { return side_layer_by_type[uint8_t(t)]; }
    // 旧接口名（仅 RenderSystem 用，保持兼容）
    static constexpr int kEndLayerLookupSize = kLayerLookupSize;

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

// 将 DrawFaceKey 转化为 InstanceData 进行实例化渲染。
//
// GPU 端 8 字节布局：
//   packed (32-bit):
//     bit  0   localX (0..15)
//     bit  4   localY (0..15, section 局部 y)
//     bit  8  localZ (0..15)
//     bit 12  faceIndex (0..5)        —— 世界空间的面
//     bit 15  orient (方块朝向)        —— 来自 BlockState.orient
//     bit 19  reserved (13 bit)
//   blockType    (16-bit)
//   textureLayer (16-bit)
//
// orient 当前 shader 不读：Section::addFaceLocal 已经在 CPU 端按 orient 把面映射成
// "方块本地坐标系下的逻辑面"再去查 textureLayer，因此端面/侧面的纹理选择已经正确。
// 保留 orient 字段（4 bit 几乎零成本）是为了将来在 frag 里旋转 UV，让横躺原木的
// 侧面木纹方向也跟着轴转 —— 那时无需再改 InstanceData 二进制布局。
//
// 世界坐标在着色器里用 sectionBase[gl_DrawID].xyz + (localX,localY,localZ) + 0.5 还原。
// CPU 不再保留 vec3 position，节省 16 字节并把整个结构降到 24B → 8B (3x)。
struct InstanceData {
    uint32_t packed;
    uint16_t blockType;
    uint16_t textureLayer;

    InstanceData() = default;
    InstanceData(uint32_t packed_, uint16_t blockType_, uint16_t textureLayer_)
        : packed(packed_), blockType(blockType_), textureLayer(textureLayer_)
    {
    }

    static constexpr uint32_t makePacked(uint8_t lx, uint8_t ly, uint8_t lz,
                                         BlockFace face, uint8_t orient = ORIENT_NONE) noexcept
    {
        return  (uint32_t(lx & 0xFu))
              | (uint32_t(ly & 0xFu) << 4)
              | (uint32_t(lz & 0xFu) << 8)
              | (uint32_t(face & 0x7u) << 12)
              | (uint32_t(orient & 0xFu) << 15);
    }

    static constexpr uint8_t   unpackX(uint32_t p)     noexcept { return  uint8_t(p        & 0xFu); }
    static constexpr uint8_t   unpackY(uint32_t p)     noexcept { return  uint8_t((p >> 4) & 0xFu); }
    static constexpr uint8_t   unpackZ(uint32_t p)     noexcept { return  uint8_t((p >> 8) & 0xFu); }
    static constexpr BlockFace unpackFace(uint32_t p)  noexcept { return BlockFace((p >> 12) & 0x7u); }
};
static_assert(sizeof(InstanceData) == 8, "InstanceData must be 8 bytes for tight GPU packing");

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
    case BLOCK_GLOWSTONE:return "Glowstone";
    default:          return "Unknown";
    }
}

// 方块属性
struct BlockProperties {
    bool isTransparent;   // 是否透明
    bool isSolid;         // 是否是固体
    bool hasAxis;         // 是否记录朝向（如原木需要按放置方向旋转纹理；泥土/石头等无轴向）
    glm::vec3 color;      // 基础颜色
    float emissive;       // 自发光强度
};

// 获取方块属性
inline BlockProperties GetBlockProperties(BlockType type) {
    switch (type) {
    case BLOCK_AIR:
        return { true,  false, false, glm::vec3(0.0f), 0.0f };
    case BLOCK_STONE:
        return { false, true,  false, glm::vec3(0.5f, 0.5f, 0.5f), 0.0f };
    case BLOCK_DIRT:
        return { false, true,  false, glm::vec3(0.4f, 0.3f, 0.2f), 0.0f };
    case BLOCK_GRASS:
        return { false, true,  false, glm::vec3(0.2f, 0.6f, 0.3f), 0.0f };
    case BLOCK_WATER:
        return { true,  false, false, glm::vec3(0.0f, 0.3f, 0.8f), 0.1f };
    case BLOCK_SAND:
        return { false, true,  false, glm::vec3(0.9f, 0.8f, 0.6f), 0.0f };
    case BLOCK_WOOD:
        return { false, true,  true,  glm::vec3(0.5f, 0.35f, 0.2f), 0.0f };
    case BLOCK_LEAVES:
        return { true,  true,  false, glm::vec3(0.2f, 0.5f, 0.2f), 0.0f };
    case BLOCK_GLOWSTONE:
        return { false, true,  false, glm::vec3(1.0f, 0.95f, 0.65f), 1.0f };  // 暖黄色自发光
    default:
        return { false, true,  false, glm::vec3(1.0f, 0.0f, 1.0f), 0.0f }; // 错误颜色
    }
}