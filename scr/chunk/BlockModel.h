#pragma once
#include "BlockType.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <array>
#include <unordered_map>

// ── 方块模型定义 / 全局 quad 表 ──────────────────────────────────────────
//
// 设计要点（见 CLAUDE.md「模型方块」）：
//  - 方块实例只存 BlockState（type + orient + variant），模型由状态【查表派生】，
//    不写进方块数据、存档、网络包。
//  - 启动时把所有 (type, orient, variant) 展开成一张全局只读 quad 表，上传到 GPU
//    SSBO（binding=4）。Section 的实例数据只存一个 quadId，顶点着色器按
//    gl_VertexID 取对应角点。
//  - CUBE 方块继续走原有的 6 面快速路径，InstanceData 里 MODEL_FLAG 为 0；
//    模型方块（MODEL_FLAG=1）走 quad 表。两者混在同一个 VBO / 同一个 draw call 里。
//
// 模型定义目前从 Blockbench 导出的 OBJ 读取（assert/mode/...），
// 手写模型也可以用同一套 ModelQuad 数据结构注册。

// CPU 侧的一个四边形
struct ModelQuad {
    glm::vec3 corners[4];   // block 局部坐标，0..1
    glm::vec3 normal;
    glm::vec2 uv[4];        // 已转换为与方块纹理数组一致的 UV（v=0 为图像顶部）
    int       textureLayer = -1;
    uint32_t  flags = 0;
};

// quad 标志位（GPU 端 g_buffer.frag 读取）
enum ModelQuadFlags : uint32_t {
    MODEL_QUAD_ALPHA_TEST = 1u << 0,   // 采样后 alpha < 0.5 丢弃
};

// GPU 端 quad 表布局。全部 vec4，std430 布局与 C++ 结构体严格一致。
struct GpuQuad {
    glm::vec4 p0;      // xyz = corner0
    glm::vec4 p1;
    glm::vec4 p2;
    glm::vec4 p3;
    glm::vec4 n;       // xyz = normal
    glm::vec4 uv01;    // (u0, v0, u1, v1)
    glm::vec4 uv23;    // (u2, v2, u3, v3)
    glm::uvec4 meta;   // x = textureLayer, y = flags
};
static_assert(sizeof(GpuQuad) == 128, "GpuQuad 必须与 shader 的 std430 布局一致");

// 某个状态（type+orient+variant）在全局 quad 表里占用的区间
struct ModelState {
    bool     valid = false;
    uint32_t firstQuad = 0;
    uint8_t  quadCount = 0;
};

// 全局模型注册表（单例）。只读，worker 线程可安全查询。
class BlockModelRegistry {
public:
    static BlockModelRegistry& instance();

    // 需在 TextureMgr 加载完成、BlockFaceType::init_type_map() 之后调用。
    // 幂等：重复调用直接返回。
    void initialize();

    bool ready() const { return m_ready; }

    // 该方块类型是否走模型渲染路径（快速数组查询）
    bool isModelBlock(BlockType type) const {
        return m_isModel[static_cast<uint8_t>(type)];
    }

    // 查询状态。variant 目前只用到 0；非 0 走哈希表（为门/楼梯预留）。
    const ModelState* getState(BlockType type, uint8_t orient,
                               uint8_t variant = 0) const;

    // 便捷：状态存在时返回该 localQuad 的全局 quadId，否则返回 0。
    uint32_t getQuadId(BlockType type, uint8_t orient, uint8_t variant,
                       uint8_t localQuad) const;

    const std::vector<GpuQuad>& gpuQuads() const { return m_gpuQuads; }

private:
    BlockModelRegistry() = default;

    void registerTorch();

    // 把一组 quad 追加进全局表，并登记到 (type, orient, variant) 状态。
    void addState(BlockType type, uint8_t orient, uint8_t variant,
                  const std::vector<ModelQuad>& quads);

    ModelState* stateSlot(BlockType type, uint8_t orient, uint8_t variant);

    bool m_ready = false;
    bool m_isModel[256] = {};
    // variant==0 的快速表：type(256) × orient(16)
    std::array<ModelState, 256 * 16> m_statesV0{};
    // 非 0 variant 的稀疏表：key = type<<8 | orient<<4 | variant
    std::unordered_map<uint32_t, ModelState> m_statesOther;
    std::vector<GpuQuad> m_gpuQuads;
};
