#include "Shader.h"
#include "Camera.h"
#include "TextureMgr.h"
#include "World_4.h"
#include <thread>
std::shared_ptr<Camera> camera = std::make_shared<Camera>(glm::vec3(3.0f, 3.0f, 3.0f));
// 键盘输入回调（GLFW内置）
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, GLFW_TRUE); // ESC关闭窗口
}

// 鼠标移动回调（处理镜头朝向）
void mouseCallback(GLFWwindow* window, double xposIn, double yposIn) {
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    // 首次鼠标输入时初始化位置
    static bool firstMouse = true;
    static float lastX = (float)SCR_WIDTH / 2.0;
    static float lastY = (float)SCR_HEIGHT / 2.0;
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    // 计算鼠标偏移量
    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos; // Y轴反转（鼠标上移=镜头上仰）
    lastX = xpos;
    lastY = ypos;

    // 应用灵敏度
    xoffset *= camera->MouseSensitivity;
    yoffset *= camera->MouseSensitivity;

    // 更新偏航角和俯仰角
    camera->Yaw += xoffset;
    camera->Pitch += yoffset;

    // 限制俯仰角（避免镜头翻转，-89°~89°）
    if (camera->Pitch > 89.0f) camera->Pitch = 89.0f;
    if (camera->Pitch < -89.0f) camera->Pitch = -89.0f;

    // 根据俯仰角和偏航角计算相机朝向
    glm::vec3 front;
    front.x = cos(glm::radians(camera->Yaw)) * cos(glm::radians(camera->Pitch));
    front.y = sin(glm::radians(camera->Pitch));
    front.z = sin(glm::radians(camera->Yaw)) * cos(glm::radians(camera->Pitch));
    camera->Front = glm::normalize(front); // 归一化（保证移动速度一致）
    camera->Right = glm::normalize(glm::cross(camera->Front, glm::vec3(0.0f, 1.0f, 0.0f))); // 更新右方向
}

// 窗口大小回调（调整视口）
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
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Open Window", NULL, NULL);
    if (!window) {
        std::cerr << "窗口创建失败" << std::endl;
        glfwTerminate();
        return nullptr;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetCursorPosCallback(window, mouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED); // 隐藏鼠标，捕获输入

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

    //World* world_1 = new World_1(camera, window);
    //world_1->run();

    //World* world_2 = new World_2(camera, window);
    //world_2->run();

    //World* world_3 = new World_3(camera, window);
    //world_3->run();

    World* world_4 = new World_4(camera, window, 114514);
    world_4->run();


    clearAll(window);
}