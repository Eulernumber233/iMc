#pragma once
#include "../core.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/BlockType.h"
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "../Shader.h"
#include "BlockOutlineRenderer.h"  // 新增
#include "../collision/Ray.h"                   // 新增
#include "../UI/UIManager.h"
// 方块面顶点数据
struct FaceVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

// 方块渲染器
class BlockRenderer {
public:
    BlockRenderer();
    ~BlockRenderer();

    // 初始化
    bool initialize();

    // 渲染方块类型
    void render(BlockFaceType type, const std::vector<glm::mat4>& instanceMatrices,
        const glm::mat4& view, const glm::mat4& projection);

    // 获取VAO
    GLuint getVAO() const { return VAO; }

private:
    // 创建单位立方体的面
    void createFaceVertices();

    GLuint VAO;            // 顶点数组对象
    GLuint VBO;            // 顶点缓冲区对象
	GLuint EBO;			// 索引缓冲区对象
    GLuint m_instanceVBO;    // 实例化矩阵缓冲区

    std::vector<unsigned int> m_indices; // 单个面的索引数据
    std::vector<FaceVertex> m_vertices;

    // 着色器程序
    Shader m_shader{ {
            { GL_VERTEX_SHADER,"shader/g_buffer.vert" },
            { GL_FRAGMENT_SHADER, "shader/g_buffer.frag" }
            } };
};

// 渲染系统
class RenderSystem {
public:
    RenderSystem(int screenWidth, int screenHeight);
    ~RenderSystem();

    // 初始化
    bool initialize();

    // 渲染一帧
    void render(const ChunkManager& chunkManager,
        const glm::mat4& view,
        const glm::mat4& projection,
        std::shared_ptr<Camera> camera);

    // 设置选中的方块（用于边框渲染）
    void setSelectedBlock(const glm::ivec3& blockPos) {
        m_selectedBlockPos = blockPos;
        m_hasSelectedBlock = true;
    }

    void clearSelectedBlock() {
        m_hasSelectedBlock = false;
    }

    // 设置高亮配置
    void setOutlineConfig(const BlockOutlineRenderer::OutlineConfig& config) {
        m_outlineRenderer.setConfig(config);
    }

    // 设置光照参数
    void setLightDirection(const glm::vec3& direction) { m_lightDirection = direction; }
    void setLightColor(const glm::vec3& color) { m_lightColor = color; }
    void setLightIntensity(float intensity) { m_lightIntensity = intensity; }
    void setAmbientColor(const glm::vec3& color) { m_ambientColor = color; }

    // 获取渲染统计
    int getDrawCalls() const { return m_drawCalls; }
    int getTotalInstances() const { return m_totalInstances; }

    // UI相关方法
    void initUI();
    void renderUI();
    UIManager& getUIManager() { return UIManager::getInstance(); }
    // 设置屏幕尺寸供UI使用
    void setScreenSize(int width, int height);

private:
    // 屏幕尺寸
    int m_screenWidth;
    int m_screenHeight;

    // G-Buffer
    GLuint m_gBuffer;
    GLuint m_gPosition, m_gNormal, m_gAlbedo, m_gProperties;
    GLuint m_rboDepth;

    // 着色器程序
    Shader m_deferredLightingShader{ {
            { GL_VERTEX_SHADER,"shader/deferred_lighting.vert" },
            { GL_FRAGMENT_SHADER, "shader/deferred_lighting.frag" }
            } };

    // 方块渲染器
    BlockRenderer m_blockRenderer;

    // 边框渲染器
    BlockOutlineRenderer m_outlineRenderer;

    // UI管理器
    UIManager& m_uiManager = UIManager::getInstance();

    // 选中的方块
    glm::ivec3 m_selectedBlockPos;
    bool m_hasSelectedBlock = false;
    float m_currentTime = 0.0f;


    // 全屏四边形
    GLuint m_screenQuadVAO;
    GLuint m_screenQuadVBO;

    // 光照参数
    glm::vec3 m_lightDirection;
    glm::vec3 m_lightColor;
    float m_lightIntensity;
    glm::vec3 m_ambientColor;

    // 渲染统计
    int m_drawCalls;
    int m_totalInstances;

    // 私有方法
    bool createGBuffer();
    void destroyGBuffer();
    void createScreenQuad();
    void createSampleUI();

    // 渲染阶段
    void geometryPass(const ChunkManager& chunkManager,
        const glm::mat4& view,
        const glm::mat4& projection);

    // 渲染边框
    void renderOutlines(const glm::mat4& view, const glm::mat4& projection);

    void lightingPass(const glm::vec3& cameraPos);

};