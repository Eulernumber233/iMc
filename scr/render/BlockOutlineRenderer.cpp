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
    // 创建立方体边框几何
    createOutlineGeometry();

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);
    glGenBuffers(1, &m_EBO);

    glBindVertexArray(m_VAO);

    // 顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER,
        m_vertices.size() * sizeof(glm::vec3),
        m_vertices.data(), GL_STATIC_DRAW);

    // 位置属性 (location = 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
        sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);

    // 索引数据
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        m_indices.size() * sizeof(unsigned int),
        m_indices.data(), GL_STATIC_DRAW);

    glBindVertexArray(0);

    return true;
}

void BlockOutlineRenderer::createOutlineGeometry() {
    // 创建立方体的12条边
    // 立方体有8个顶点
    m_vertices.clear();
    m_indices.clear();

    // 定义立方体的8个顶点（以原点为中心，边长为1）
    // 前平面的4个顶点
    m_vertices.push_back(glm::vec3(-0.5f, -0.5f, 0.5f));   // 0: 左下前
    m_vertices.push_back(glm::vec3(0.5f, -0.5f, 0.5f));    // 1: 右下前
    m_vertices.push_back(glm::vec3(0.5f, 0.5f, 0.5f));     // 2: 右上前
    m_vertices.push_back(glm::vec3(-0.5f, 0.5f, 0.5f));    // 3: 左上前

    // 后平面的4个顶点
    m_vertices.push_back(glm::vec3(-0.5f, -0.5f, -0.5f));  // 4: 左下后
    m_vertices.push_back(glm::vec3(0.5f, -0.5f, -0.5f));   // 5: 右下后
    m_vertices.push_back(glm::vec3(0.5f, 0.5f, -0.5f));    // 6: 右上后
    m_vertices.push_back(glm::vec3(-0.5f, 0.5f, -0.5f));   // 7: 左上后

    // 定义12条边（每条边2个顶点）
    // 前平面
    m_indices.push_back(0); m_indices.push_back(1);  // 下边
    m_indices.push_back(1); m_indices.push_back(2);  // 右边
    m_indices.push_back(2); m_indices.push_back(3);  // 上边
    m_indices.push_back(3); m_indices.push_back(0);  // 左边

    // 后平面
    m_indices.push_back(4); m_indices.push_back(5);  // 下边
    m_indices.push_back(5); m_indices.push_back(6);  // 右边
    m_indices.push_back(6); m_indices.push_back(7);  // 上边
    m_indices.push_back(7); m_indices.push_back(4);  // 左边

    // 连接前后平面的垂直边
    m_indices.push_back(0); m_indices.push_back(4);  // 左下垂直
    m_indices.push_back(1); m_indices.push_back(5);  // 右下垂直
    m_indices.push_back(2); m_indices.push_back(6);  // 右上垂直
    m_indices.push_back(3); m_indices.push_back(7);  // 左上垂直
}

void BlockOutlineRenderer::render(const glm::ivec3& blockPos,
    const glm::mat4& view,
    const glm::mat4& projection,
    float time) {
    if (time == 0.0f) time = m_currentTime;

    // 构建变换矩阵 TODO
    glm::mat4 model = glm::translate(glm::mat4(1.0f),
        glm::vec3(blockPos)+glm::vec3(0.5,0.5,0.5));

    // 稍微放大一点，让边框在方块外部
    model = glm::scale(model, glm::vec3(m_config.outlineScale));

    // 设置线宽
    glLineWidth(m_config.lineWidth);

    // 设置深度测试
    if (m_config.depthTest) {
        glEnable(GL_DEPTH_TEST);
    }
    else {
        glDisable(GL_DEPTH_TEST);
    }

    // 使用着色器
    m_shader.use();

    // 设置变换矩阵
    m_shader.setMat4("uModel", model);
    m_shader.setMat4("uView", view);
    m_shader.setMat4("uProjection", projection);

    // 设置边框参数
    m_shader.setVec3("uOutlineColor", m_config.color);
    m_shader.setFloat("uTime", time);
    m_shader.setFloat("uPulseSpeed", m_config.pulseSpeed);
    m_shader.setFloat("uPulseIntensity", m_config.pulseIntensity);
    m_shader.setInt("uEnablePulse", m_config.enablePulse ? 1 : 0);

    // 渲染边框
    glBindVertexArray(m_VAO);
    glDrawElements(GL_LINES, static_cast<GLsizei>(m_indices.size()),
        GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // 恢复深度测试
    glEnable(GL_DEPTH_TEST);
}