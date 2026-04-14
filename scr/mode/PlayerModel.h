#pragma once
#include "../core.h"
#include "../Shader.h"
#include <vector>
#include <string>

// Minecraft 人物模型（Steve wide 版本）
// 由6个长方体部件组成：头、身体、左臂、右臂、左腿、右腿
// 每个部件有独立的 VAO/VBO/EBO，支持独立变换（为后续动画预留）
// 使用 mode.vert / mode.frag 着色器

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

    // 绘制整个模型（静态站立姿势）
    // worldPos: 模型脚底中心的世界坐标
    // yaw: 模型朝向（绕Y轴旋转，弧度）
    void draw(Shader& shader, const glm::vec3& worldPos, float yaw = 0.0f);

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
