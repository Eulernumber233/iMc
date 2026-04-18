#pragma once
#include "UIManager.h"

// 物品栏：参照原版 Minecraft 的像素度量
//   槽位格距 SLOT_PITCH = 20（原版 GUI 单位）
//   图标内框 ICON_SIZE  = 16
//   选中高亮 SEL_SIZE   = 24（四周各 1px 溢出）
// 由于槽位背景贴图本身不是中心对称的（有连接到相邻槽位的 cap 像素），
// 图标与选中框都相对"逻辑槽位中心"定位，不跟随背景贴图的像素偏移。
class UIHotbar : public UIContainer {
public:
    UIHotbar(const std::string& id, int slotCount = 10);
    ~UIHotbar();

    void setSelectedSlot(int slot);
    int getSelectedSlot() const { return m_selectedSlot; }

    void setSlotItem(int slot, const std::string& textureName);
    void clearSlotItem(int slot);

    void scroll(float yoffset);

    void update(float deltaTime) override;
    void render(const glm::mat4& projection) override;

    // 按屏幕高度自动选择整数 GUI 缩放（像素完美），
    // 并更新自身尺寸、子组件位置
    void setGuiScaleForScreen(int screenWidth, int screenHeight);
    // 强制指定 GUI 缩放（>=1）
    void setGuiScale(int scale);
    int  getGuiScale() const { return m_guiScale; }

private:
    // 原版 MC 像素度量（1× 缩放下）
    static constexpr int SLOT_PITCH = 20; // 逻辑槽位间距
    static constexpr int ICON_SIZE  = 16; // 物品图标
    static constexpr int SEL_SIZE   = 24; // 选中高亮外框（含 1px 溢出）
    static constexpr int BG_W       = 29; // hotbar_offhand_right 贴图像素宽
    static constexpr int BG_H       = 24; // hotbar_offhand_right 贴图像素高
    // 背景贴图内"可见槽位中心"相对贴图几何中心的偏移（1× 缩放下的像素）。
    // 通过分析 hotbar_offhand_right.png 的 alpha：可见区 x∈[7,28], y∈[1,22]，
    // 中心 (17.5, 11.5)，贴图几何中心 (14.5, 12)，故偏移 (+3, -0.5)。
    static constexpr float BG_SLOT_OFFSET_X = 3.0f;
    static constexpr float BG_SLOT_OFFSET_Y = -0.5f;
    // hotbar_selection.png 的 16×16 透明窗中心相对贴图几何中心的偏移。
    // 窗口 x∈[4,19], y∈[4,19]，中心 (11.5, 11.5)，贴图中心 (11.5, 11)，故 (0, +0.5)。
    static constexpr float SEL_WINDOW_OFFSET_X = 0.0f;
    static constexpr float SEL_WINDOW_OFFSET_Y = 0.5f;

    int m_slotCount;
    int m_selectedSlot = 0;
    int m_guiScale = 2;

    std::string m_borderTexture   = "hotbar_offhand_right";
    std::string m_selectionTexture = "hotbar_selection";

    std::vector<std::shared_ptr<UIImage>> m_slotBackgrounds;
    std::vector<std::shared_ptr<UIImage>> m_slotIcons;
    std::shared_ptr<UIImage> m_selection; // 单独的高亮叠层

    void initChildren();
    void layout();
};
