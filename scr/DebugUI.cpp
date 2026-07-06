#include "DebugUI.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"

#include "item/HeldDisplayRegistry.h"
#include "item/ItemDefinition.h"

#include <sstream>
#include <iostream>

DebugUI::~DebugUI() {
    shutdown();
}

void DebugUI::init(GLFWwindow* window) {
    if (m_inited) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;             // 不生成 imgui.ini（不持久化窗口布局）
    ImGui::StyleColorsDark();
    // install_callbacks = true：ImGui 安装自己的 GLFW 回调，并链式转发给
    // World 之前注册的回调（故游戏输入回调仍会触发；由 World 按 isVisible 决定是否处理）
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 460");
    m_inited = true;
}

void DebugUI::shutdown() {
    if (!m_inited) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_inited  = false;
    m_visible = false;
}

void DebugUI::toggle(GLFWwindow* window, bool cursorLockedWhenHidden) {
    m_visible = !m_visible;
    if (m_visible) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);   // 释放光标去点面板
    } else {
        glfwSetInputMode(window, GLFW_CURSOR,
                         cursorLockedWhenHidden ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    }
}

void DebugUI::draw(const ItemDefinition* heldDef, int fbWidth, int fbHeight) {
    if (!m_inited || !m_visible) return;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    buildPanels(heldDef);
    if (m_showDemo) ImGui::ShowDemoWindow(&m_showDemo);

    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbWidth, fbHeight);
    // OpenGL3 后端会自行备份/恢复 GL 状态，不会把 blend/depth 等泄漏给下一帧
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void DebugUI::buildPanels(const ItemDefinition* heldDef) {
    HeldDisplayRegistry& reg = HeldDisplayRegistry::instance();

    ImGui::Begin("调试面板 (F1 开合)");
    ImGui::TextUnformatted("held_display 实时调参：拖动数值即时生效");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("第一人称手臂 arm", ImGuiTreeNodeFlags_DefaultOpen)) {
        FirstPersonHandConfig& a = reg.armMutable();
        ImGui::DragFloat3("offset", &a.offset.x, 0.005f);
        ImGui::DragFloat("pitch", &a.pitch, 0.5f);
        ImGui::DragFloat("yaw",   &a.yaw,   0.5f);
        ImGui::DragFloat("roll",  &a.roll,  0.5f);
        ImGui::DragFloat("scale", &a.scale, 0.005f, 0.01f, 5.0f);
        ImGui::SeparatorText("挥手");
        ImGui::DragFloat("swing_duration",  &a.swingDuration, 0.005f, 0.01f, 2.0f);
        ImGui::DragFloat("swing_pitch_amp", &a.swingPitchAmp, 0.5f);
        ImGui::DragFloat("swing_roll_amp",  &a.swingRollAmp,  0.5f);
        ImGui::DragFloat("swing_lift",      &a.swingLift,     0.005f);
    }

    if (ImGui::CollapsingHeader("当前手持物 first-person TRS", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (heldDef) {
            ImGui::Text("物品 id: %s", heldDef->id.c_str());
            HeldItemDisplay* d = reg.resolveMutable(*heldDef);
            HeldTransform& fp = d->firstPerson;
            ImGui::DragFloat3("t 平移",  &fp.translation.x, 0.005f);
            ImGui::DragFloat3("r 旋转(度)", &fp.rotationDeg.x, 0.5f);
            ImGui::DragFloat("s 缩放",   &fp.scale, 0.005f, 0.01f, 5.0f);
            ImGui::TextDisabled("注意：改的是该物品所用【档案】，共享此档案的物品会一起变");
        } else {
            ImGui::TextDisabled("(当前空手：切到某个物品格再调 first-person 摆放)");
        }
    }

    ImGui::Separator();
    if (ImGui::Button("输出当前值到控制台(JSON)")) {
        printValuesToConsole(heldDef);
    }
    ImGui::SameLine();
    ImGui::Checkbox("ImGui 官方 Demo (学习用)", &m_showDemo);
    ImGui::TextDisabled("改好后：把控制台输出的数值粘回 assert/held_display.json 即可持久化");

    ImGui::End();
}

void DebugUI::printValuesToConsole(const ItemDefinition* heldDef) {
    HeldDisplayRegistry& reg = HeldDisplayRegistry::instance();
    const FirstPersonHandConfig& a = reg.armMutable();

    std::ostringstream os;
    os << "\n===== held_display 当前值（可粘回 assert/held_display.json）=====\n";
    os << "\"arm\": {\n";
    os << "  \"offset\": [" << a.offset.x << ", " << a.offset.y << ", " << a.offset.z << "],\n";
    os << "  \"pitch\": " << a.pitch << ", \"yaw\": " << a.yaw
       << ", \"roll\": " << a.roll << ", \"scale\": " << a.scale << ",\n";
    os << "  \"swing_duration\": " << a.swingDuration
       << ", \"swing_pitch_amp\": " << a.swingPitchAmp
       << ", \"swing_roll_amp\": "  << a.swingRollAmp
       << ", \"swing_lift\": "      << a.swingLift << "\n";
    os << "}\n";

    if (heldDef) {
        HeldItemDisplay* d = reg.resolveMutable(*heldDef);
        const HeldTransform& fp = d->firstPerson;
        os << "// 物品 \"" << heldDef->id << "\" 的 first-person 档案：\n";
        os << "\"first\": {\n";
        os << "  \"t\": [" << fp.translation.x << ", " << fp.translation.y << ", " << fp.translation.z << "],\n";
        os << "  \"r\": [" << fp.rotationDeg.x << ", " << fp.rotationDeg.y << ", " << fp.rotationDeg.z << "],\n";
        os << "  \"s\": " << fp.scale << "\n";
        os << "}\n";
    }
    std::cout << os.str() << std::endl;
}
