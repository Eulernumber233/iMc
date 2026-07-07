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

    ImGui::Begin("Debug Panel (F1)");
    ImGui::TextUnformatted("held_display live tuning: drag values, applies instantly");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("First-person arm", ImGuiTreeNodeFlags_DefaultOpen)) {
        FirstPersonHandConfig& a = reg.armMutable();
        ImGui::DragFloat3("offset", &a.offset.x, 0.005f);
        ImGui::DragFloat("pitch", &a.pitch, 0.5f);
        ImGui::DragFloat("yaw",   &a.yaw,   0.5f);
        ImGui::DragFloat("roll",  &a.roll,  0.5f);
        ImGui::DragFloat("scale", &a.scale, 0.005f, 0.01f, 5.0f);
        ImGui::SeparatorText("Swing");
        ImGui::DragFloat("swing_duration",  &a.swingDuration, 0.005f, 0.01f, 2.0f);
        ImGui::DragFloat("swing_pitch_amp", &a.swingPitchAmp, 0.5f);
        ImGui::DragFloat("swing_roll_amp",  &a.swingRollAmp,  0.5f);
        ImGui::DragFloat("swing_lift",      &a.swingLift,     0.005f);
    }

    if (ImGui::CollapsingHeader("Held item TRS", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (heldDef) {
            ImGui::Text("item id: %s", heldDef->id.c_str());
            HeldItemDisplay* d = reg.resolveMutable(*heldDef);

            // 第一人称（屏幕上手里那份）。"##fp" 后缀让 ImGui 的控件 ID 唯一（显示不出）。
            ImGui::SeparatorText("First person (in hand, on screen)");
            HeldTransform& fp = d->firstPerson;
            ImGui::DragFloat3("t (translate)##fp",  &fp.translation.x, 0.005f);
            ImGui::DragFloat3("r (rotate deg)##fp", &fp.rotationDeg.x, 0.5f);
            ImGui::DragFloat("s (scale)##fp",       &fp.scale, 0.005f, 0.01f, 5.0f);

            // 第三人称（挂右手骨骼，按 F3 切到第三人称才看得到效果）。
            ImGui::SeparatorText("Third person (attached to right hand)");
            HeldTransform& tp = d->thirdPerson;
            ImGui::DragFloat3("t (translate)##tp",  &tp.translation.x, 0.005f);
            ImGui::DragFloat3("r (rotate deg)##tp", &tp.rotationDeg.x, 0.5f);
            ImGui::DragFloat("s (scale)##tp",       &tp.scale, 0.005f, 0.01f, 5.0f);

            ImGui::TextDisabled("Tip: press F3 to toggle first/third person while tuning");
            ImGui::TextDisabled("Note: edits the PROFILE this item uses; items sharing it change too");
        } else {
            ImGui::TextDisabled("(Empty hand: select an item slot to tune placement)");
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Print values to console (JSON)")) {
        printValuesToConsole(heldDef);
    }
    ImGui::SameLine();
    ImGui::Checkbox("ImGui Demo (learn)", &m_showDemo);
    ImGui::TextDisabled("After tuning: paste console output back into assert/held_display.json");

    ImGui::End();
}

void DebugUI::printValuesToConsole(const ItemDefinition* heldDef) {
    HeldDisplayRegistry& reg = HeldDisplayRegistry::instance();
    const FirstPersonHandConfig& a = reg.armMutable();

    std::ostringstream os;
    os << "\n===== held_display current values (paste into assert/held_display.json) =====\n";
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
        const HeldTransform& tp = d->thirdPerson;
        os << "// item \"" << heldDef->id << "\" profile (first + third):\n";
        os << "\"first\": {\n";
        os << "  \"t\": [" << fp.translation.x << ", " << fp.translation.y << ", " << fp.translation.z << "],\n";
        os << "  \"r\": [" << fp.rotationDeg.x << ", " << fp.rotationDeg.y << ", " << fp.rotationDeg.z << "],\n";
        os << "  \"s\": " << fp.scale << "\n";
        os << "},\n";
        os << "\"third\": {\n";
        os << "  \"t\": [" << tp.translation.x << ", " << tp.translation.y << ", " << tp.translation.z << "],\n";
        os << "  \"r\": [" << tp.rotationDeg.x << ", " << tp.rotationDeg.y << ", " << tp.rotationDeg.z << "],\n";
        os << "  \"s\": " << tp.scale << "\n";
        os << "}\n";
    }
    std::cout << os.str() << std::endl;
}
