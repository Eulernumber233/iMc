#include "Camera.h"
#include "collision/Ray.h"
#include <iostream>
#include <limits>

// 构造函数 - 使用向量
Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch)
    : Front(glm::vec3(0.0f, 0.0f, -1.0f))
    , MovementSpeed(SPEED)
    , MouseSensitivity(SENSITIVITY)
    , Zoom(ZOOM)
{
    Position = position;
    WorldUp = up;
    Yaw = yaw;
    Pitch = pitch;
    BasicMovementSpeed = SPEED;

    // 投影参数
    AspectRatio = 16.0f / 9.0f;
    NearPlane = 0.1f;
    FarPlane = 1000.0f;
    FOV = 45.0f;

    // 更新摄像机向量
    updateCameraVectors();
}

// 构造函数 - 使用标量
Camera::Camera(float posX, float posY, float posZ,
    float upX, float upY, float upZ,
    float yaw, float pitch)
    : Front(glm::vec3(0.0f, 0.0f, -1.0f))
    , MovementSpeed(SPEED)
    , MouseSensitivity(SENSITIVITY)
    , Zoom(ZOOM)
{
    Position = glm::vec3(posX, posY, posZ);
    WorldUp = glm::vec3(upX, upY, upZ);
    Yaw = yaw;
    Pitch = pitch;
    BasicMovementSpeed = SPEED;

    // 投影参数
    AspectRatio = 16.0f / 9.0f;
    NearPlane = 0.1f;
    FarPlane = 1000.0f;
    FOV = 45.0f;

    // 更新摄像机向量
    updateCameraVectors();
}

// 获取视图矩阵
glm::mat4 Camera::GetViewMatrix() const
{
    return glm::lookAt(Position, Position + Front, Up);
}

// 获取投影矩阵
glm::mat4 Camera::GetProjectionMatrix() const
{
    return glm::perspective(glm::radians(FOV), AspectRatio, NearPlane, FarPlane);
}

// 获取视图投影矩阵
glm::mat4 Camera::GetViewProjectionMatrix() const
{
    return GetProjectionMatrix() * GetViewMatrix();
}

// 获取视锥体平面
std::array<glm::vec4, 6> Camera::GetFrustumPlanes() const
{
    glm::mat4 viewProj = GetViewProjectionMatrix();
    std::array<glm::vec4, 6> planes;

    // 提取视锥体平面
    // 左平面
    planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]
    );

    // 右平面
    planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]
    );

    // 下平面
    planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]
    );

    // 上平面
    planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]
    );

    // 近平面
    planes[4] = glm::vec4(
        viewProj[0][3] + viewProj[0][2],
        viewProj[1][3] + viewProj[1][2],
        viewProj[2][3] + viewProj[2][2],
        viewProj[3][3] + viewProj[3][2]
    );

    // 远平面
    planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]
    );

    // 归一化平面
    for (int i = 0; i < 6; i++) {
        float length = glm::length(glm::vec3(planes[i]));
        if (length > 0.0001f) {
            planes[i] /= length;
        }
    }

    return planes;
}

// 获取从摄像机位置发出的射线（简化版，直接使用Front方向）
std::shared_ptr<Ray> Camera::GetViewRay() const
{
    // 直接从摄像机位置和前向方向创建射线
    return std::make_shared<Ray>(Position, Front);
}

// 从屏幕坐标获取射线（用于鼠标拾取）
std::shared_ptr<Ray> Camera::GetRayFromScreen(float screenX, float screenY,
    int screenWidth, int screenHeight) const
{
    // 确保使用浮点数
    float x = static_cast<float>(screenX);
    float y = static_cast<float>(screenY);
    float width = static_cast<float>(screenWidth);
    float height = static_cast<float>(screenHeight);

    // 将屏幕坐标转换为标准化设备坐标
    float ndcX = (2.0f * x) / width - 1.0f;
    float ndcY = 1.0f - (2.0f * y) / height;

    // 获取投影和视图矩阵的逆矩阵
    glm::mat4 projection = GetProjectionMatrix();
    glm::mat4 view = GetViewMatrix();
    glm::mat4 inverseProjView = glm::inverse(projection * view);

    // 创建在裁剪空间的射线端点
    glm::vec4 rayStartNDC(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 rayEndNDC(ndcX, ndcY, 0.0f, 1.0f);

    // 转换到世界空间
    glm::vec4 rayStartWorld = inverseProjView * rayStartNDC;
    rayStartWorld /= rayStartWorld.w;

    glm::vec4 rayEndWorld = inverseProjView * rayEndNDC;
    rayEndWorld /= rayEndWorld.w;

    // 计算射线方向
    glm::vec3 rayDir = glm::normalize(glm::vec3(rayEndWorld) - glm::vec3(rayStartWorld));

    return std::make_shared<Ray>(Position, rayDir);
}

// 更新摄像机向量（根据欧拉角计算）
void Camera::updateCameraVectors()
{
    // 计算前向向量
    glm::vec3 front;
    front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    front.y = sin(glm::radians(Pitch));
    front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
    Front = glm::normalize(front);

    // 重新计算右向量和上向量
    Right = glm::normalize(glm::cross(Front, WorldUp));
    Up = glm::normalize(glm::cross(Right, Front));
}

// 处理键盘输入
void Camera::ProcessKeyboard(Camera_Movement direction, float deltaTime)
{
    float velocity = MovementSpeed * deltaTime;

    switch (direction) {
    case CAMERA_FORWARD:
        Position += Front * velocity;
        break;
    case CAMERA_BACKWARD:
        Position -= Front * velocity;
        break;
    case CAMERA_LEFT:
        Position -= Right * velocity;
        break;
    case CAMERA_RIGHT:
        Position += Right * velocity;
        break;
    }
}

// 处理鼠标移动
void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch)
{
    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    Yaw += xoffset;
    Pitch += yoffset;

    // 确保俯仰角不会超过89度
    if (constrainPitch) {
        if (Pitch > 89.0f)
            Pitch = 89.0f;
        if (Pitch < -89.0f)
            Pitch = -89.0f;
    }

    // 更新前向、右和上向量
    updateCameraVectors();
}

// 处理鼠标滚轮
void Camera::ProcessMouseScroll(float yoffset)
{
    FOV -= yoffset;
    if (FOV < 1.0f)
        FOV = 1.0f;
    if (FOV > 45.0f)
        FOV = 45.0f;
}

// 公开的更新摄像机向量方法
void Camera::UpdateCameraVectors()
{
    updateCameraVectors();
}