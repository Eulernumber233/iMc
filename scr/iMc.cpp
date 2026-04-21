#include "Shader.h"
#include "Camera.h"
#include "TextureMgr.h"
#include "World.h"
#include <thread>
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
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
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
    // 窗口初始化
    GLFWwindow* window = initAll();
    if (!window) {
        return -1;
    }

    // 创建并运行世界（种子来自 core.h 中的全局常量）
    World world(window, WorldConstants::WORLD_SEED);
    world.run();

    clearAll(window);
    return 0;
}