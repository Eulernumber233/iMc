#pragma once
#include "../core.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/BlockType.h"
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "../Shader.h"
#include "BlockOutlineRenderer.h"
#include "../collision/Ray.h"
#include "../UI/UIManager.h"
#include "../mode/Model.h"
#include "../particle/ParticleManager.h"

class Player;

struct FaceVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

class BlockRenderer {
public:
    BlockRenderer();
    ~BlockRenderer();
    bool initialize();
    void render(const std::vector<InstanceData>& instanceMatrices,
        const glm::mat4& view, const glm::mat4& projection);
    void renderDepth(const std::vector<InstanceData>& instanceData,
        const glm::mat4& lightSpaceMatrix, float near, float far);
    GLuint getVAO() const { return VAO; }
    void setTextureArray(GLuint texArray) { m_textureArray = texArray; }

private:
    void createFaceVertices();

    GLuint VAO;
    GLuint VBO;
    GLuint EBO;
    GLuint m_instanceDataVBO;
    GLuint m_textureArray = 0;
    std::vector<unsigned int> m_indices;
    std::vector<FaceVertex> m_vertices;

    Shader m_shader{ {
        { GL_VERTEX_SHADER,   "shader/g_buffer.vert" },
        { GL_FRAGMENT_SHADER, "shader/g_buffer.frag" }
    } };
    Shader m_depthShader{ {
        { GL_VERTEX_SHADER,   "shader/shadow_mapping_depth.vert" },
        { GL_FRAGMENT_SHADER, "shader/shadow_mapping_depth.frag" }
    } };
};

class RenderSystem {
public:
    RenderSystem(int screenWidth, int screenHeight);
    ~RenderSystem();

    bool initialize();
    void render(const ChunkManager& chunkManager,
        const glm::mat4& view,
        const glm::mat4& projection,
        std::shared_ptr<Camera> camera,
        float deltaTime,
        Player* player = nullptr);

    void setSelectedBlock(const glm::ivec3& blockPos) {
        m_selectedBlockPos = blockPos;
        m_hasSelectedBlock = true;
    }
    void clearSelectedBlock() {
        m_hasSelectedBlock = false;
    }
    void setOutlineConfig(const BlockOutlineRenderer::OutlineConfig& config) {
        m_outlineRenderer.setConfig(config);
    }
    void setLightDirection(const glm::vec3& direction) { m_lightDirection = direction; }
    void setLightColor(const glm::vec3& color) { m_lightColor = color; }
    void setLightIntensity(float intensity) { m_lightIntensity = intensity; }
    void setAmbientColor(const glm::vec3& color) { m_ambientColor = color; }

    int getDrawCalls() const { return m_drawCalls; }
    int getTotalInstances() const { return m_totalInstances; }

    // UI
    void initUI();
    UIManager& getUIManager() { return UIManager::getInstance(); }
    void setScreenSize(int width, int height);

    // 粒子系统
    void toggleWeather() { m_particleManager.toggleWeather(); }
    void emitBlockDebris(const glm::vec3& blockPosition, BlockType blockType, int count = 50) {
        m_particleManager.emitBlockDebris(blockPosition, blockType, count);
    }
    int getParticleCount() const { return m_particleManager.getTotalParticleCount(); }

private:
    // 屏幕尺寸
    int m_screenWidth;
    int m_screenHeight;

    // G-Buffer
    GLuint m_gBuffer;
    GLuint m_gPosition, m_gNormal, m_gAlbedo, m_gProperties;
    GLuint m_depthTexture = 0;               // G-Buffer深度纹理

    // SSAO
    GLuint m_ssaoFBO, m_ssaoBlurFBO;
    GLuint m_ssaoColorBuffer, m_ssaoColorBufferBlur;
    GLuint m_noiseTexture;
    std::vector<glm::vec3> ssaoKernel;

    // 阴影
    GLuint m_depthMapFBO;
    GLuint m_depthMap;                        // 阴影深度图

    // 光照FBO（延迟光照结果）
    GLuint m_lightingFBO;
    GLuint m_lightingColorTexture;            // 光照颜色纹理

    // 合成FBO（用于后续正向渲染，包含颜色和深度）
    GLuint m_compositeFBO;
    GLuint m_compositeColor;                  // 最终颜色纹理
    GLuint m_compositeDepth;                   // 最终深度纹理

    // 光源参数
    glm::vec3 lightPos = { -1, 1.0f, -1.0f };
    glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, -1.0f, 1.0f));

    void move_DirLight(float deltaTime) {
        static float time_now = 0.0f;
        const float rotate_speed = 0.2f;
        time_now += deltaTime;
        float angle = rotate_speed * time_now;
        lightPos.x = -100.0f * sin(angle);
        lightPos.y = 100.0f * cos(angle);
        lightPos.z = -100.0f * sin(angle);
        lightDir = glm::normalize(glm::vec3(0.0f) - lightPos);
    }

    // 着色器
    Shader m_ssaoShader{ {
        { GL_VERTEX_SHADER,   "shader/ssao.vert" },
        { GL_FRAGMENT_SHADER, "shader/ssao.frag" }
    } };
    Shader m_ssaoBlurShader{ {
        { GL_VERTEX_SHADER,   "shader/ssao_blur.vert" },
        { GL_FRAGMENT_SHADER, "shader/ssao_blur.frag" }
    } };
    Shader m_deferredLightingShader{ {
        { GL_VERTEX_SHADER,   "shader/deferred_lighting.vert" },
        { GL_FRAGMENT_SHADER, "shader/deferred_lighting.frag" }
    } };
    Shader m_modeShader{ {
        { GL_VERTEX_SHADER,   "shader/mode.vert" },
        { GL_FRAGMENT_SHADER, "shader/mode.frag" }
    } };

    // 模型
    Model spyglass{ "assert/mode/spyglass_in_hand_obj/spyglass_in_hand.obj" };

    // 渲染器组件
    BlockRenderer m_blockRenderer;
    ParticleManager m_particleManager;
    BlockOutlineRenderer m_outlineRenderer;
    UIManager& m_uiManager = UIManager::getInstance();

    // 选中方块
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

    // 统计
    int m_drawCalls;
    int m_totalInstances;

    // 私有方法
    bool createGBuffer();
    void destroyGBuffer();
    void createScreenQuad();
    void createSampleUI();
    bool createLightingFBO();          // 新增：创建光照FBO
    bool createCompositeFBO();         // 新增：创建合成FBO
    void destroyCompositeFBO();        // 新增：销毁合成FBO

    void RenderQuad();
    void geometryPass(const ChunkManager& chunkManager,
        const glm::mat4& view,
        const glm::mat4& projection);
    void ssaoPass(const glm::mat4& view, const glm::mat4& projection);
    void ssaoBlurPass();
    void sunShineShadowMap(const ChunkManager& chunkManager, const std::shared_ptr<Camera>camera,
        float& sunShine_near, float& sunShine_far, glm::mat4& lightSpaceMatrix);
    void lightingPass(const std::shared_ptr<Camera>camera,
        float sunShine_near, float sunShine_far, glm::mat4& lightSpaceMatrix);
    void renderOutlines(const glm::mat4& view, const glm::mat4& projection);
    void renderUI();
    void renderModel(const std::shared_ptr<Camera>camera,
        const glm::mat4& view, const glm::mat4& projection, Player* player);
    void renderModel_test(const std::shared_ptr<Camera> camera,
        const glm::mat4& view, const glm::mat4& projection);
};