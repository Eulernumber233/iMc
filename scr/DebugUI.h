#pragma once

// 前向声明，避免在头文件里拖入 ImGui / GL / 物品定义
struct GLFWwindow;
struct ItemDefinition;

// ── 调试面板（Dear ImGui 封装）─────────────────────────────────
// F1 开合。可见时释放鼠标（GLFW_CURSOR_NORMAL）以操作面板；隐藏时恢复第一人称
// 锁定光标。目前面板内容：实时调 held_display（第一人称手臂 + 当前手持物的
// first-person TRS），一个「输出当前值到控制台(JSON)」按钮，一个 ImGui 官方 Demo
// 开关（学习用）。改的值直接写进 HeldDisplayRegistry 的可变存储，RenderSystem 下一帧
// 就读到，即时生效。
//
// 生命周期：init() 须在 GL 上下文 current、且 World 的 GLFW 回调已注册之后调用
// （ImGui 会安装链式回调接住并转发给已有回调）；shutdown() 须在销毁窗口 / GL 上下文
// 之前调用（删除 ImGui 的 GL 对象）。析构会幂等兜底 shutdown。
class DebugUI {
public:
    ~DebugUI();

    void init(GLFWwindow* window);
    void shutdown();

    bool isVisible() const { return m_visible; }

    // 切换显示 + 光标模式。cursorLockedWhenHidden：隐藏后是否重新锁定光标
    // （背包同时开着时应保持 NORMAL，故由调用方决定）。
    void toggle(GLFWwindow* window, bool cursorLockedWhenHidden);

    // 每帧在「场景渲染之后、swapBuffers 之前」调用一次。仅在可见时才真正
    // 走 ImGui 的 NewFrame → 构建 → 渲染；隐藏时几乎零开销。
    // heldDef 可为空（空手）。fbWidth/fbHeight 为帧缓冲像素尺寸。
    void draw(const ItemDefinition* heldDef, int fbWidth, int fbHeight);

private:
    void buildPanels(const ItemDefinition* heldDef);
    void printValuesToConsole(const ItemDefinition* heldDef);

    bool m_visible  = false;
    bool m_inited   = false;
    bool m_showDemo = false;
};
