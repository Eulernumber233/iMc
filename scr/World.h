#pragma once
#include "Shader.h"
#include "Camera.h"
#include "TextureMgr.h"
class World
{
public:
    World(std::shared_ptr<Camera> camera_, GLFWwindow* window_) {
        window = window_;
        camera = camera_;
    }
    virtual int run() = 0;
protected:
    GLFWwindow* window;
    std::shared_ptr<Camera> camera;

    // 读取键盘输入，移动
    void readKey_toMove(GLFWwindow* window, float deltaTime) {
        static float speed_multiple = 1.0f;
        static float change_speed_delta_time = 0.0f;
        float speed = camera->MovementSpeed * deltaTime;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            camera->Position += speed * camera->Front;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            camera->Position -= speed * camera->Front;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            camera->Position -= speed * camera->Right; // 注意：这里用到了 camera->Right
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            camera->Position += speed * camera->Right;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            camera->Position += speed * camera->Up;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
            camera->Position -= speed * camera->Up;
        if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) {// 加速
            change_speed_delta_time += deltaTime;
            if (change_speed_delta_time > 0.033) {
                speed_multiple += 0.33;
                change_speed_delta_time = 0.0f;
            }
            camera->MovementSpeed = camera->BasicMovementSpeed * speed_multiple;
        }
        if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {// 减速
            change_speed_delta_time += deltaTime;
            if (change_speed_delta_time > 0.033) {
                speed_multiple -= 0.33;
                change_speed_delta_time = 0.0f;
            }
            camera->MovementSpeed = camera->BasicMovementSpeed * speed_multiple;
        }
    }

    // 显示帧率
    void showFPS(GLFWwindow* window) {
        static int frameCount = 0;
        static double fpsUpdateInterval = 0.5; // 每0.5秒更新
        static double fpsLastUpdate = glfwGetTime();

        double currentTime = glfwGetTime();
        frameCount++;
        if (currentTime - fpsLastUpdate >= fpsUpdateInterval) {
            double fps = frameCount / (currentTime - fpsLastUpdate);
            // 创建标题字符串
            std::stringstream ss;
            //<< std::setprecision(1)
            ss << "Open Window - FPS: " << std::fixed << fps;
            glfwSetWindowTitle(window, ss.str().c_str());

            // 重置计数器和时间
            frameCount = 0;
            fpsLastUpdate = currentTime;
        }
    }
    
	// 渲染一个覆盖整个屏幕的四边形
    void RenderQuad()
    {
        static GLuint quadVAO = 0;
        static GLuint quadVBO;
        if (quadVAO == 0)
        {
            GLfloat quadVertices[] = {
                // Positions        // Texture Coords
                -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,
                -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
                1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
                1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
            };
            // Setup plane VAO
            glGenVertexArrays(1, &quadVAO);
            glGenBuffers(1, &quadVBO);
            glBindVertexArray(quadVAO);
            glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (GLvoid*)(3 * sizeof(GLfloat)));

        }

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }
};

