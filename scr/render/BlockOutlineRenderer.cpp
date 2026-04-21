#include "BlockOutlineRenderer.h"
#include <iostream>

BlockOutlineRenderer::BlockOutlineRenderer()
    : m_VAO(0), m_VBO(0), m_EBO(0) {
}

BlockOutlineRenderer::~BlockOutlineRenderer() {
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_EBO) glDeleteBuffers(1, &m_EBO);
}

bool BlockOutlineRenderer::initialize() {
    createOutlineGeometry();

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
        m_vertices.size() * sizeof(glm::vec3),
        m_vertices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        m_indices.size() * sizeof(unsigned int),
        m_indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    return true;
}

void BlockOutlineRenderer::createOutlineGeometry() {
    m_vertices.clear();
    m_indices.clear();

    m_vertices.push_back(glm::vec3(-0.5f, -0.5f, 0.5f));
    m_vertices.push_back(glm::vec3(0.5f, -0.5f, 0.5f));
    m_vertices.push_back(glm::vec3(0.5f, 0.5f, 0.5f));
    m_vertices.push_back(glm::vec3(-0.5f, 0.5f, 0.5f));

    m_vertices.push_back(glm::vec3(-0.5f, -0.5f, -0.5f));
    m_vertices.push_back(glm::vec3(0.5f, -0.5f, -0.5f));
    m_vertices.push_back(glm::vec3(0.5f, 0.5f, -0.5f));
    m_vertices.push_back(glm::vec3(-0.5f, 0.5f, -0.5f));

    m_indices.push_back(0); m_indices.push_back(1);
    m_indices.push_back(1); m_indices.push_back(2);
    m_indices.push_back(2); m_indices.push_back(3);
    m_indices.push_back(3); m_indices.push_back(0);

    m_indices.push_back(4); m_indices.push_back(5);
    m_indices.push_back(5); m_indices.push_back(6);
    m_indices.push_back(6); m_indices.push_back(7);
    m_indices.push_back(7); m_indices.push_back(4);

    m_indices.push_back(0); m_indices.push_back(4);
    m_indices.push_back(1); m_indices.push_back(5);
    m_indices.push_back(2); m_indices.push_back(6);
    m_indices.push_back(3); m_indices.push_back(7);
}

void BlockOutlineRenderer::render(const glm::ivec3& blockPos,
    const glm::mat4& view,
    const glm::mat4& projection,
    float time) {
    if (time == 0.0f) time = m_currentTime;

    glm::mat4 model = glm::translate(glm::mat4(1.0f),
        glm::vec3(blockPos) + glm::vec3(0.5, 0.5, 0.5));

    model = glm::scale(model, glm::vec3(m_config.outlineScale));

    glLineWidth(m_config.lineWidth);

    if (m_config.depthTest) {
        glEnable(GL_DEPTH_TEST);
    }
    else {
        glDisable(GL_DEPTH_TEST);
    }

    // 使用着色器
    m_shader.use();

    // 绑定深度纹理（如果存在）
    if (m_depthTexture) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_depthTexture);
        m_shader.setInt("uDepthTexture", 3);
        m_shader.setInt("uDepthTestEnabled", 1);
    } else {
        m_shader.setInt("uDepthTestEnabled", 0);
    }

    // 设置变换矩阵
    m_shader.setMat4("uModel", model);
    m_shader.setMat4("uView", view);
    m_shader.setMat4("uProjection", projection);

    m_shader.setVec3("uOutlineColor", m_config.color);
    //m_shader.setFloat("uTime", time);
    m_shader.setFloat("uPulseSpeed", m_config.pulseSpeed);
    m_shader.setFloat("uPulseIntensity", m_config.pulseIntensity);
    m_shader.setInt("uEnablePulse", m_config.enablePulse ? 1 : 0);

    glBindVertexArray(m_VAO);
    glDrawElements(GL_LINES, static_cast<GLsizei>(m_indices.size()),
        GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
}