#pragma once
#include "../core.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include "../Shader.h"

class BlockOutlineRenderer {
public:
    struct OutlineConfig {
        glm::vec3 color = glm::vec3(10.0f, 10.0f, 10.0f);
        float lineWidth = 0.3f;
        float pulseSpeed = 2.0f;
        float pulseIntensity = 0.15f;
        bool enablePulse = true;
        bool depthTest = false;
        float outlineScale = 1.01f;
    };

    BlockOutlineRenderer();
    ~BlockOutlineRenderer();

    bool initialize();

    void render(const glm::ivec3& blockPos,
        const glm::mat4& view,
        const glm::mat4& projection,
        float time = 0.0f);

    void setConfig(const OutlineConfig& config) { m_config = config; }
    const OutlineConfig& getConfig() const { return m_config; }

    // 更新时间
    void updateTime(float time) { m_currentTime = time; }

    // 设置深度纹理
    void setDepthTexture(GLuint textureID) { m_depthTexture = textureID; }

private:
    void createOutlineGeometry();

    GLuint m_VAO;
    GLuint m_VBO;
    GLuint m_EBO;

    std::vector<glm::vec3> m_vertices;
    std::vector<unsigned int> m_indices;

    OutlineConfig m_config;
    float m_currentTime = 0.0f;
    GLuint m_depthTexture = 0; // 深度纹理，用于遮挡检测

    // 着色器
    Shader m_shader{ {
        { GL_VERTEX_SHADER, "shader/outline.vert" },
        { GL_FRAGMENT_SHADER, "shader/outline.frag" }
     } };
};