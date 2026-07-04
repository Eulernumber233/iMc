#pragma once
#include "../core.h"
#include <string>
#include <unordered_map>
#include <memory>

// ── 挤出物品模型 ────────────────────────────────────────────────
// 把一张 2D 图标逐像素挤出成带厚度的 3D 网格（MC 风格）：
//   - 前 / 后各一整片带纹理的 quad（frag 内 alpha discard 出轮廓）
//   - 每个不透明像素若某方向邻居透明，沿该 alpha 边缘生成一片侧壁 quad
// 网格居中于原点，最长边归一化到 1.0，厚度 = 1 像素。用 item.vert/item.frag 绘制。
class ItemModel {
public:
    ~ItemModel();

    // 从图标 PNG 文件构建（读像素，需有效 GL 上下文创建 VBO）。失败返回 nullptr。
    static std::unique_ptr<ItemModel> buildFromIcon(const std::string& iconPath);

    void draw() const; // 绑定 VAO 并 glDrawElements

private:
    ItemModel() = default;
    GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    GLsizei m_indexCount = 0;
};

// 按物品 id 懒构建 + 缓存挤出网格
class ItemModelCache {
public:
    static ItemModelCache& instance();
    // 取（或首次构建）某物品 id 对应的挤出模型；构建失败返回 nullptr。
    const ItemModel* get(const std::string& id, const std::string& iconPath);

    // 清空缓存。VAO 不跨 GL 上下文共享，故每次新建游戏窗口（新 RenderSystem）时清一次，
    // 让模型在新上下文里按需重建，避免使用属于已销毁上下文的失效 VAO。
    void clear() { m_cache.clear(); }

private:
    std::unordered_map<std::string, std::unique_ptr<ItemModel>> m_cache;
};
