#pragma once
#include "../core.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "../Shader.h"

class BlockOutlineRenderer {
public:
    struct OutlineConfig {
        glm::vec3 color = glm::vec3(0.0f, 0.0f, 0.0f);  // 默认黑色边框
        float lineWidth = 2.0f;
        float pulseSpeed = 2.0f;    // 脉动动画速度
        float pulseIntensity = 0.15f; // 脉动强度
        bool enablePulse = true;     // 启用脉动效果
        bool depthTest = false;      // 是否启用深度测试
        float outlineScale = 1.01f;  // 边框缩放比例
    };

    BlockOutlineRenderer();
    ~BlockOutlineRenderer();

    // 初始化
    bool initialize();

    // 渲染单个方块的边框
    void render(const glm::ivec3& blockPos,
        const glm::mat4& view,
        const glm::mat4& projection,
        float time = 0.0f);

    // 设置配置
    void setConfig(const OutlineConfig& config) { m_config = config; }
    const OutlineConfig& getConfig() const { return m_config; }

    // 更新动画时间
    void updateTime(float time) { m_currentTime = time; }

private:
    void createOutlineGeometry();

    GLuint m_VAO;
    GLuint m_VBO;
    GLuint m_EBO;

    std::vector<glm::vec3> m_vertices;
    std::vector<unsigned int> m_indices;

    OutlineConfig m_config;
    float m_currentTime = 0.0f;

    // 着色器
    Shader m_shader{ {
        { GL_VERTEX_SHADER, "shader/outline.vert" },
        { GL_FRAGMENT_SHADER, "shader/outline.frag" }
     } };
};