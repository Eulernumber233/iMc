#pragma once
#include "../core.h"
#include "../chunk/BlockType.h"
#include <unordered_map>
#include <memory>

// ── 方块物品立方体模型 ──────────────────────────────────────────
// 为「方块物品」（背包图标 / 掉落物 / 手持）构建一个居中于原点、边长 1 的
// 立方体网格。每个面的纹理层由 BlockFaceType::getTextureLayer({type,face})
// 解析，采样与地形共用的方块纹理数组（sampler2DArray）。
// 用 block_item.vert/block_item.frag 绘制；调用方负责绑定纹理数组到 unit 0。
class BlockItemModel {
public:
    ~BlockItemModel();

    // 为某方块类型构建立方体（需有效 GL 上下文）。失败返回 nullptr。
    static std::unique_ptr<BlockItemModel> buildForBlock(BlockType type);

    // 该方块是否有可用的纹理层（BlockFaceType 映射里至少注册了一面）。
    // 未注册的方块（如缺纹理的 sand）应退回挤出 2D 图标，避免整块显示成错误纹理。
    static bool hasValidTextures(BlockType type);

    void draw() const; // 绑定 VAO 并 glDrawArrays

private:
    BlockItemModel() = default;
    GLuint m_vao = 0, m_vbo = 0;
    GLsizei m_vertexCount = 0;
};

// 按方块类型懒构建 + 缓存立方体网格
class BlockItemModelCache {
public:
    static BlockItemModelCache& instance();
    // 取（或首次构建）某方块类型的立方体；构建失败返回 nullptr。
    const BlockItemModel* get(BlockType type);

    // 清空缓存。VAO 不跨 GL 上下文共享，故每次新建游戏窗口（新 RenderSystem）时清一次。
    void clear() { m_cache.clear(); }

private:
    std::unordered_map<int, std::unique_ptr<BlockItemModel>> m_cache;
};
