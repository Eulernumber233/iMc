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
#include "../mode/PlayerModel.h"
#include "../particle/ParticleManager.h"

class Player;
class NetManager;

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
    // 把 arena VBO 绑定为本 VAO 的实例属性源；arena 扩容后需要重新调用
    void bindArenaVBO(GLuint arenaVBO);
    // MDI 渲染：indirectBuffer 中存放 cmdCount 条 DrawElementsIndirectCommand
    void render(GLuint indirectBuffer, int cmdCount,
        const glm::mat4& view, const glm::mat4& projection);
    void renderDepth(GLuint indirectBuffer, int cmdCount,
        const glm::mat4& lightSpaceMatrix, float nearPlane, float farPlane);
    GLuint getVAO() const { return VAO; }
    void setTextureArray(GLuint texArray) { m_textureArray = texArray; }
    // 一次性上传"每种 BlockType 的端面纹理层"查表给 g_buffer shader。
    // 调用前提：BlockFaceType::init_type_map() 已执行（即纹理已加载）。
    void uploadEndLayerLookup();

private:
    void createFaceVertices();
    void bindInstanceAttribs();

    GLuint VAO;
    GLuint VBO;
    GLuint EBO;
    GLuint m_currentArenaVBO = 0;
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
        Player* player = nullptr,
        NetManager* netManager = nullptr);

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
    UIManager& getUIManager() { return m_uiManager; }
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

    // HBAO（阶段 2：地平线角 AO + 蓝噪声少样本 + 时域累积）
    GLuint m_hbaoFBO, m_hbaoBlurFBO;
    GLuint m_hbaoColorBuffer, m_hbaoColorBufferBlur;
    GLuint m_noiseTexture;                 // 旧 SSAO 随机旋转噪声，HBAO 不用但兼容保留
    std::vector<glm::vec3> ssaoKernel;     // 旧 SSAO 半球核，HBAO 不用但兼容保留

    // ---- AO 时域累积 ----
    // 单帧 HBAO（噪声大）→ 跨帧累积成干净结果。AO 纯几何恒定，可用很强历史权重。
    GLuint m_aoAccumFBO[2] = { 0, 0 };
    GLuint m_aoAccum[2]    = { 0, 0 };   // 累积 AO ping-pong（R16F）
    int    m_aoAccumCurrIdx = 0;
    bool   m_aoAccumValid   = false;

    // 阴影
    GLuint m_depthMapFBO;
    GLuint m_depthMap;                        // 阴影深度图（DEPTH_COMPONENT32F，普通深度，阶段 2）

    // 蓝噪声纹理：阴影 PCSS 的 blocker/filter 抖动源（配合帧序号时域去相关 + TAA 降噪）。
    // 程序生成的 tileable 64² 蓝噪声（void-and-cluster 近似），R 通道存 [0,1] 值。
    GLuint m_blueNoiseTex = 0;
    int    m_blueNoiseSize = 64;

    // ---- 阴影时域累积 ----
    // 单帧 PCSS 可见度（噪声大）→ 跨帧累积成干净结果，专治光源旋转时阴影边缘逐格波动。
    GLuint m_shadowVisFBO   = 0;
    GLuint m_shadowVisCurr  = 0;            // 当前帧单帧可见度（R8）
    GLuint m_shadowAccumFBO[2] = { 0, 0 };
    GLuint m_shadowAccum[2]    = { 0, 0 };  // 累积可见度 ping-pong
    int    m_shadowAccumCurrIdx = 0;
    bool   m_shadowAccumValid   = false;

    // 光照FBO（延迟光照结果）
    GLuint m_lightingFBO;
    GLuint m_lightingColorTexture;            // 光照颜色纹理

    // 合成FBO（用于后续正向渲染，包含颜色和深度）
    GLuint m_compositeFBO;
    GLuint m_compositeColor;                  // 最终颜色纹理
    GLuint m_compositeDepth;                   // 最终深度纹理

    // ---- TAA（时域抗锯齿）----
    // 历史帧 ping-pong：当前帧 resolve 写入 m_taaHistory[m_taaCurrIdx]，
    // 读取 m_taaHistory[1-m_taaCurrIdx] 作为上一帧累积结果。
    GLuint m_taaFBO[2]     = { 0, 0 };
    GLuint m_taaHistory[2] = { 0, 0 };
    int    m_taaCurrIdx    = 0;
    unsigned m_frameIndex  = 0;
    bool   m_taaHistoryValid = false;
    bool   m_taaEnabled    = true;
    // 上一帧的未抖动 viewProj，用于 motion vector 重投影（体素世界静态几何，仅相机动）
    glm::mat4 m_prevViewProj = glm::mat4(1.0f);

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
    void move_DirLight(float deltaTime);
    // 着色器
    Shader m_hbaoShader{ {
        { GL_VERTEX_SHADER,   "shader/hbao.vert" },
        { GL_FRAGMENT_SHADER, "shader/hbao.frag" }
    } };
    Shader m_hbaoBlurShader{ {
        { GL_VERTEX_SHADER,   "shader/hbao_blur.vert" },
        { GL_FRAGMENT_SHADER, "shader/hbao_blur.frag" }
    } };
    Shader m_deferredLightingShader{ {
        { GL_VERTEX_SHADER,   "shader/deferred_lighting.vert" },
        { GL_FRAGMENT_SHADER, "shader/deferred_lighting.frag" }
    } };
    Shader m_modeShader{ {
        { GL_VERTEX_SHADER,   "shader/mode.vert" },
        { GL_FRAGMENT_SHADER, "shader/mode.frag" }
    } };
    Shader m_taaShader{ {
        { GL_VERTEX_SHADER,   "shader/taa_resolve.vert" },
        { GL_FRAGMENT_SHADER, "shader/taa_resolve.frag" }
    } };
    // 阴影可见度（单帧 PCSS）与时域累积（复用延迟光照的全屏 quad 顶点着色器）
    Shader m_shadowVisShader{ {
        { GL_VERTEX_SHADER,   "shader/deferred_lighting.vert" },
        { GL_FRAGMENT_SHADER, "shader/shadow_visibility.frag" }
    } };
    Shader m_shadowAccumShader{ {
        { GL_VERTEX_SHADER,   "shader/deferred_lighting.vert" },
        { GL_FRAGMENT_SHADER, "shader/shadow_accumulate.frag" }
    } };
    // AO 时域累积（复用 hbao 的全屏 quad 顶点着色器）
    Shader m_aoAccumShader{ {
        { GL_VERTEX_SHADER,   "shader/hbao.vert" },
        { GL_FRAGMENT_SHADER, "shader/ao_accumulate.frag" }
    } };

    // 模型
    Model spyglass{ "assert/mode/spyglass_in_hand_obj/spyglass_in_hand.obj" };

    // 渲染器组件
    BlockRenderer m_blockRenderer;
    ParticleManager m_particleManager;
    BlockOutlineRenderer m_outlineRenderer;
    UIManager m_uiManager;

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
    bool createTAATargets();           // TAA 历史 ping-pong 缓冲
    void destroyTAATargets();
    void createBlueNoiseTexture();     // 程序生成蓝噪声纹理（阴影抖动源）
    bool createShadowVisTargets();     // 阴影可见度 + 时域累积 ping-pong 缓冲
    void destroyShadowVisTargets();
    bool createAoAccumTargets();       // AO 时域累积 ping-pong 缓冲
    void destroyAoAccumTargets();
    // TAA resolve：当前帧合成色 + 历史 → 时域累积，结果写入并显示
    void taaResolvePass(const glm::mat4& invViewProj, const glm::mat4& prevViewProj);
    // Halton(2,3) 亚像素抖动偏移（NDC 单位），按帧序号循环
    static glm::vec2 haltonJitterNDC(unsigned frameIndex, int w, int h);

    void RenderQuad();
    void geometryPass(const ChunkManager& chunkManager,
        const glm::mat4& view,
        const glm::mat4& projection);
    void hbaoPass(const glm::mat4& view, const glm::mat4& projection);
    void hbaoBlurPass();
    // AO 时域累积：重投影历史 + 邻域 clamp + 混合 → m_aoAccum[curr]
    void aoAccumulatePass(const glm::mat4& invViewProj, const glm::mat4& prevViewProj);
    void sunShineShadowMap(const ChunkManager& chunkManager, const std::shared_ptr<Camera>camera,
        float& sunShine_near, float& sunShine_far, glm::mat4& lightSpaceMatrix);
    // 单帧 PCSS 可见度 → m_shadowVisCurr
    void shadowVisibilityPass(const glm::mat4& view, const glm::mat4& projection,
        const glm::mat4& lightSpaceMatrix);
    // 时域累积：重投影历史 + 邻域 clamp + 混合 → m_shadowAccum[curr]
    void shadowAccumulatePass(const glm::mat4& invViewProj, const glm::mat4& prevViewProj);
    void lightingPass(const std::shared_ptr<Camera>camera,
        const glm::mat4& view, const glm::mat4& projection,
        float sunShine_near, float sunShine_far, glm::mat4& lightSpaceMatrix);
    void renderOutlines(const glm::mat4& view, const glm::mat4& projection);
    void renderUI();
    void renderModel(const std::shared_ptr<Camera>camera,
        const glm::mat4& view, const glm::mat4& projection, Player* player);
    void renderModel_test(const std::shared_ptr<Camera> camera,
        const glm::mat4& view, const glm::mat4& projection);
    void renderRemotePlayers(NetManager* netManager,
        const glm::mat4& view, const glm::mat4& projection,
        const std::shared_ptr<Camera>& camera);

    // 远程玩家模型（共享几何体，逐玩家设置变换矩阵）
    PlayerModel m_remotePlayerModel;

    // 皮肤纹理缓存（skinName → GLuint texture）
    std::unordered_map<std::string, GLuint> m_skinTextures;
    GLuint loadSkinTexture(const std::string& skinName);
    void loadAllSkinTextures();
};