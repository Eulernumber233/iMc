#include "Shader.h"
#include "Camera.h"
#include "TextureMgr.h"
#include "World.h"
#include "save/ChunkSaveManager.h"
#include <thread>
#include <chrono>
#include <ctime>
#include <unordered_set>
// 窗口大小回调（调整视口）- 现在由World类处理
void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// 设置窗口icon
bool setWindowIcon(GLFWwindow* window, const char* iconPath) {
    int width, height, channels;
    unsigned char* pixels = stbi_load(iconPath, &width, &height, &channels, 4); // 强制加载为RGBA

    if (!pixels) {
        std::cerr << "Failed to load icon: " << iconPath << std::endl;
        return false;
    }

    GLFWimage icon;
    icon.width = width;
    icon.height = height;
    icon.pixels = pixels;

    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(pixels);
    return true;
}

GLFWwindow* initAll() {
    // 1. 初始化GLFW
    glfwInit();
    // 4.6 core：方块渲染走 glMultiDrawElementsIndirect + baseInstance（4.2 起进核心），
    // 顶点着色器需要 gl_DrawID（4.6 内置）以从 SSBO 读 sectionBase 还原世界坐标。
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);  // 确保默认帧缓冲有深度缓冲区
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Open Window", NULL, NULL);
    if (!window) {
        std::cerr << "窗口创建失败" << std::endl;
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(window);
    // 注意：回调函数现在由World类设置
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

    // 2. 初始化GLEW
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW初始化失败" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return nullptr;
    }
    // 设置窗口icon
    setWindowIcon(window, "assert/textures/item/Axolotl_Bucket.png");
    return window;
}

void clearAll(GLFWwindow* window) {
    glfwDestroyWindow(window);
    glfwTerminate();
}

int main() {
    srand(13);

    // ==============================
    // Phase 1: CLI 世界选择 / 创建
    // ==============================
    std::string worldName;
    uint32_t seed = 0;
    bool isNewWorld = false;

    std::cout << "=== iMc 体素引擎 ===" << std::endl;
    std::cout << "1. 新建世界" << std::endl;
    std::cout << "2. 读取存档" << std::endl;
    std::cout << "3. 退出" << std::endl;
    std::cout << "请选择 (1/2/3): ";

    int choice = 0;
    std::cin >> choice;
    std::cin.ignore(); // 吃掉换行

    if (choice == 3) {
        std::cout << "再见!" << std::endl;
        return 0;
    }

    if (choice == 2) {
        auto worlds = ChunkSaveManager::listWorlds();
        if (worlds.empty()) {
            std::cout << "没有找到存档，将创建新世界。" << std::endl;
            choice = 1;
        } else {
            std::cout << std::endl << "已有世界:" << std::endl;
            for (size_t i = 0; i < worlds.size(); ++i) {
                std::cout << "  " << (i + 1) << ". " << worlds[i].name
                          << "  (种子: " << worlds[i].seed << ")" << std::endl;
            }
            std::cout << "  0. 返回" << std::endl;
            std::cout << "请输入编号进入: ";
            int sel = 0;
            std::cin >> sel;
            std::cin.ignore();
            if (sel > 0 && sel <= (int)worlds.size()) {
                worldName = worlds[sel - 1].name;
                seed = worlds[sel - 1].seed;
                isNewWorld = false;
            } else {
                choice = 1; // 返回或无效 → 跳到新建
            }
        }
    }

    if (choice == 1) {
        // 检查已有世界列表，避免重名
        auto existing = ChunkSaveManager::listWorlds();
        std::unordered_set<std::string> usedNames;
        for (auto& w : existing) usedNames.insert(w.name);

        while (true) {
            std::cout << "请输入世界名字 (直接回车 = \"New World\"): ";
            std::getline(std::cin, worldName);
            if (worldName.empty()) {
                worldName = "New World";
                int suffix = 1;
                while (usedNames.count(worldName)) {
                    worldName = "New World_" + std::to_string(suffix++);
                }
                std::cout << "使用名字: \"" << worldName << "\"" << std::endl;
                break;
            }

            if (usedNames.count(worldName)) {
                std::cout << "名字 \"" << worldName << "\" 已存在，请换一个。" << std::endl;
            } else {
                break;
            }
        }

        std::cout << "请输入种子 (0 = 随机, 直接回车 = 随机): ";
        {
            std::string seedLine;
            std::getline(std::cin, seedLine);
            if (seedLine.empty()) {
                seed = 0;
            } else {
                try { seed = (uint32_t)std::stoul(seedLine); }
                catch (...) { seed = 0; }
            }
        }
        if (seed == 0) {
            seed = (uint32_t)std::chrono::system_clock::now()
                       .time_since_epoch().count();
            std::cout << "随机种子: " << seed << std::endl;
        }
        isNewWorld = true;
    }

    if (worldName.empty()) {
        worldName = "New World";
        seed = WorldConstants::WORLD_SEED;
        isNewWorld = true;
    }

    // ==============================
    // Phase 2: 启动 OpenGL 窗口
    // ==============================

    GLFWwindow* window = initAll();
    if (!window) {
        return -1;
    }

    World world(window, worldName, seed, isNewWorld);

    world.run();

    clearAll(window);
    return 0;
}