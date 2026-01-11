#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <array> 
#include <memory>

// 前向声明
class Ray;

// 摄像机移动方向
enum Camera_Movement {
    CAMERA_FORWARD,
    CAMERA_BACKWARD,
    CAMERA_LEFT,
    CAMERA_RIGHT
};

// 默认摄像机值
const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 7.5f;
const float SENSITIVITY = 0.1f;
const float ZOOM = 45.0f;

// 摄像机类
class Camera
{
public:
    // 摄像机属性
    glm::vec3 Position;
    glm::vec3 Front;
    glm::vec3 Up;
    glm::vec3 Right;
    glm::vec3 WorldUp;

    // 欧拉角
    float Yaw;
    float Pitch;

    // 摄像机选项
    float MovementSpeed;
    float BasicMovementSpeed;
    float MouseSensitivity;
    float Zoom;

    // 投影参数
    float AspectRatio;
    float NearPlane;
    float FarPlane;
    float FOV;

public:
    // 构造函数
    Camera(glm::vec3 position = glm::vec3(0.0f, 70.0f, 0.0f),
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
        float yaw = YAW,
        float pitch = PITCH);

    Camera(float posX, float posY, float posZ,
        float upX, float upY, float upZ,
        float yaw, float pitch);

    // 析构函数
    ~Camera() = default;

    // 视图矩阵
    glm::mat4 GetViewMatrix() const;

    // 投影矩阵
    glm::mat4 GetProjectionMatrix() const;

    // 视图投影矩阵
    glm::mat4 GetViewProjectionMatrix() const;

    // 获取视锥体平面
    std::array<glm::vec4, 6> GetFrustumPlanes() const;

    // 获取从摄像机位置发出的射线（用于准星）
    std::shared_ptr<Ray> GetViewRay() const;

    // 从屏幕坐标获取射线（用于鼠标拾取）
    std::shared_ptr<Ray> GetRayFromScreen(float screenX, float screenY,
        int screenWidth, int screenHeight) const;

    // 更新摄像机向量
    void UpdateCameraVectors();

    // 处理键盘输入
    void ProcessKeyboard(Camera_Movement direction, float deltaTime);

    // 处理鼠标输入
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);

    // 处理鼠标滚轮
    void ProcessMouseScroll(float yoffset);

private:
    // 内部更新方法
    void updateCameraVectors();
};

#endif // CAMERA_H