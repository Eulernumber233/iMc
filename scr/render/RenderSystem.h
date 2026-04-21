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
    glm::vec3 lightPos = { -100.0f, 100.0f, -50.0f };
    glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, -1.0f, 0.5f));

    // 白天->夜晚的强度系数 [0,1]，地平线附近平滑过渡
    float m_sunIntensity = 1.0f;
    // 日出/日落暖色系数 [0,1]，1=完全暖色(橙红)，0=中午白光
    float m_sunWarmth = 0.0f;
    // 太阳当前色温对应的 diffuse 颜色（随 m_sunWarmth 在冷白↔暖橙间插值）
    glm::vec3 m_sunDiffuseColor = glm::vec3(1.0f);

    // 夜晚冻结用的缓存（避免光方向退化时 lightSpaceMatrix 抖动）
    glm::mat4 m_cachedLightSpaceMatrix = glm::mat4(1.0f);
    float     m_cachedSunNear = 0.0f;
    float     m_cachedSunFar  = 1.0f;
    bool      m_hasCachedLightMatrix = false;

    // 太阳绕固定轴做圆周运动：在 YZ 平面内旋转（Y 为高度），X 固定
    // 这样 lightDir.y 的符号变化对应昼夜，且 up=(0,1,0) 的 lookAt 永远不会退化（除非正好与 Y 轴共线，
    // 由于 X 分量固定非零，始终能保证光方向与 up 的夹角不为零）
    void move_DirLight(float deltaTime) {
        static float time_now = 1.0f;
        const float rotate_speed = 0.08f;   // 一个完整昼夜周期约 2π/0.2 ≈ 31 秒
        time_now += deltaTime;
        float angle = rotate_speed * time_now;

        const float R = 100.0f;
        // 在 YZ 平面旋转，X 固定 —— 形成东升西落的日照弧线
        lightPos.x = 30.0f;                 // 固定非零 X 分量，避免 lookAt 退化
        lightPos.y = R * sin(angle);
        lightPos.z = R * cos(angle);
        lightDir = glm::normalize(glm::vec3(0.0f) - lightPos);

        // 高度角的正弦（>0 为白天，<0 为夜晚）
        float sinElev = sin(angle);
        // 地平线附近 ±0.1 弧度（约 ±5.7°）平滑过渡
        m_sunIntensity = glm::smoothstep(-0.05f, 0.1f, sinElev);

        // 色温：太阳越靠近地平线越暖
        // sinElev ∈ [0.0, 0.35] → 暖色从 1 过渡到 0；更高时保持冷白
        m_sunWarmth = 1.0f - glm::smoothstep(0.05f, 0.35f, sinElev);
        // 冷白（中午）到暖橙（日出/日落）
        glm::vec3 coolWhite(1.00f, 0.97f, 0.92f);
        glm::vec3 warmOrange(1.00f, 0.55f, 0.25f);
        m_sunDiffuseColor = glm::mix(coolWhite, warmOrange, m_sunWarmth);
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