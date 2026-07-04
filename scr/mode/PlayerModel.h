#pragma once
#include "../core.h"
#include "../Shader.h"
#include "PlayerAnimator.h"
#include <vector>
#include <string>

// 人物模型由6个长方体部件组成：头、身体、左臂、右臂、左腿、右腿
// 每个部件有独立的 VAO/VBO/EBO，支持独立变换
// 使用 mode.vert / mode.frag 着色器

// 第一人称手部摆放/挥手参数（相机空间：+X 右、+Y 上、-Z 屏幕里/前方；单位=方块）
struct FirstPersonHandConfig {
    // 手相对镜头的偏移：X 越大越靠右，Y 越负越靠下，Z 越负越靠前（离镜头越远）
    glm::vec3 offset = glm::vec3(0.24f, -0.54f, -0.1f);
    // 手臂三轴旋转（度）
    float pitch = 118.0f;   // 绕 X：把下垂的手臂掰向屏幕里（最主要，决定前伸角度）
    float yaw   = 18.0f;    // 绕 Y：手臂朝准星方向内偏
    float roll  = -25.0f;   // 绕 Z：手臂外翻
    float scale = 0.70f;    // 整体缩放（右臂原始约 0.25×0.75×0.25 方块，1.0=原大）
    // ---- 挥手动画 ----
    float swingDuration = 0.28f;  // 一次挥手时长（秒），越小越快
    float swingPitchAmp = 55.0f;  // 挥手前后摆幅（度）
    float swingRollAmp  = 20.0f;  // 挥手外翻摆幅（度）
    float swingLift     = 0.10f;  // 挥手时手部上抬幅度（方块）
};

class PlayerModel {
public:
    // 部件枚举
    enum Part {
        HEAD = 0,
        BODY,
        LEFT_ARM,
        RIGHT_ARM,
        LEFT_LEG,
        RIGHT_LEG,
        PART_COUNT
    };

    PlayerModel();
    ~PlayerModel();

    // 加载皮肤纹理并创建几何体
    bool initialize(const std::string& skinPath);

    // 绘制整个模型（静态站立姿势，保留用于兼容）
    // worldPos: 模型脚底中心的世界坐标
    // yaw: 模型朝向（绕Y轴旋转，弧度）
    void draw(Shader& shader, const glm::vec3& worldPos, float yaw = 0.0f);

    // 按照 PlayerPose 绘制（动画姿态）
    // worldPos: 模型脚底中心的世界坐标
    // pose: 来自 PlayerAnimator 的姿态
    void drawPosed(Shader& shader, const glm::vec3& worldPos, const PlayerPose& pose);

    // 使用外部纹理绘制（overrideTexture 替换 m_skinTexture，几何体复用）
    void drawPosed(Shader& shader, const glm::vec3& worldPos, const PlayerPose& pose, GLuint overrideTexture);

    // 第一人称手部 按 handConfig 构建相机空间 model 矩阵，
    void drawFirstPersonHand(Shader& shader, bool leftMousePressed, float deltaTime);

    // 本帧挥手量（弧度 / 方块）。手持物品时复用它，让物品与手臂共享同一挥手动画。
    struct HandSwing { float pitch = 0.0f; float roll = 0.0f; float lift = 0.0f; };
    // 推进挥手状态机并返回本帧挥手量。每帧只应调用一次（手臂或手持物品二选一驱动）。
    HandSwing advanceHandSwing(bool leftMousePressed, float deltaTime);

    // 第一人称手部参数（可直接读写调整）
    FirstPersonHandConfig handConfig;

    // 获取皮肤纹理 ID
    GLuint getTextureID() const { return m_skinTexture; }

private:
    // 每个部件的渲染数据
    struct PartMesh {
        GLuint VAO = 0;
        GLuint VBO = 0;
        GLuint EBO = 0;
        unsigned int indexCount = 0;

        // 部件在模型空间的偏移（相对于模型原点=脚底中心）
        glm::vec3 origin = glm::vec3(0.0f);
        // 旋转轴心点（相对于部件原点，用于动画）
        glm::vec3 pivot = glm::vec3(0.0f);
    };

    PartMesh m_parts[PART_COUNT];
    GLuint m_skinTexture = 0;

    // 第一人称手部挥动状态：m_handSwinging 期间 m_handSwingProgress 从 0 推进到 1；
    // 到 1 时若左键仍按住则减 1 循环，否则结束。点击一次 => 完整挥一下，长按 => 持续挥。
    bool  m_handSwinging = false;
    float m_handSwingProgress = 0.0f;

    // 顶点结构（PlayerModel 专用，避免与全局 Vertex 冲突）
    struct PlayerVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoord;
        glm::vec3 tangent;
        glm::vec3 bitangent;
    };

    // 创建一个长方体部件的顶点和索引
    void createBoxPart(Part part, glm::vec3 size,
                       glm::vec2 uvOrigin, glm::vec3 origin, glm::vec3 pivot);

    // 辅助：添加一个面的4个顶点和6个索引
    void addFace(std::vector<PlayerVertex>& vertices, std::vector<unsigned int>& indices,
                 const glm::vec3& p0, const glm::vec3& p1,
                 const glm::vec3& p2, const glm::vec3& p3,
                 const glm::vec3& normal,
                 float u0, float v0, float u1, float v1);

    // 加载纹理
    GLuint loadTexture(const std::string& path);
};
