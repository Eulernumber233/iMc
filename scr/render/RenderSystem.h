#pragma once
#include "../core.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/BlockType.h"
#include <unordered_map>
#include <vector>
#include <memory>
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
class DroppedItemManager;

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
        NetManager* netManager = nullptr,
        DroppedItemManager* droppedItems = nullptr);

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

    // ---- 世界时间控制（o/p/l + 8/9/0 按键）----
    // 调时间比例：delta 为增量（游戏小时/现实秒），可把比例调成负值（时间倒流）。
    // 夹到 [-kTimeScaleMax, kTimeScaleMax]。
    void adjustTimeScale(float delta) {
        m_timeScale = glm::clamp(m_timeScale + delta, -kTimeScaleMax, kTimeScaleMax);
    }
    void toggleSunMoving() { m_sunMoving = !m_sunMoving; }
    // 预设时间：直接把世界时间跳到 hour（[0,24)），不改比例（调用方决定是否顺便暂停）。
    void setWorldTime(float hour) { m_worldTimeHours = glm::mod(hour, 24.0f); }
    void setSunMoving(bool moving) { m_sunMoving = moving; }
    float getTimeScale() const { return m_timeScale; }
    float getWorldTime() const { return m_worldTimeHours; }
    bool  isSunMoving() const { return m_sunMoving; }

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

    // 阴影（阶段 3：CSM 级联阴影）
    // 单张 shadow map → 深度纹理数组（每级联一个 layer）。producer 逐级联渲染深度，
    // consumer 按片元视距选级联在对应 layer 里跑 PCSS。时域累积/光照 pass 不受影响。
    GLuint m_csmFBO   = 0;                     // 无颜色附件，逐级联重附 m_csmDepth 的不同 layer
    GLuint m_csmDepth = 0;                     // GL_TEXTURE_2D_ARRAY，DEPTH_COMPONENT32F，depth=CASCADE_COUNT
    int    m_csmSize  = CSM_SHADOW_SIZE;       // 每级联分辨率（创建时从 RuntimeConfig 取）
    // 逐级联光空间矩阵 + 切分远边界（视图空间正距离，consumer 比对片元视距选级联）
    glm::mat4 m_cascadeLightMatrix[CASCADE_COUNT];
    float     m_cascadeSplitView[CASCADE_COUNT] = { 0.0f };
    // 每级联的世界跨度（正交框全宽 = 2*包围球半径）。consumer 用它把半影按世界尺度归一化：
    // lightSizeUV = shadowLightSize * (refExtent / extent)，使世界半影宽度跨级联一致
    // （否则 UV 半影在近级联对应的世界半影远小于远级联 → 近处阴影偏锐，软度丢失）。
    float     m_cascadeWorldExtent[CASCADE_COUNT] = { 0.0f };

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

    // ---- 世界时间驱动的太阳（o/p/l + 8/9/0 按键）----
    // 抽象出 0-24 的世界时间（小时），由它换算太阳角度：angle = (h/24)*2π - π/2，
    // 使 12 点太阳最高、6/18 点在地平线、0 点最低（与 lightPos.y=R*sin(angle) 对齐）。
    // 时间按"现实秒 → 游戏小时"的比例推进：m_worldTimeHours += m_timeScale * dt。
    float m_worldTimeHours = 12.0f;   // 当前世界时间 [0,24)，默认正午
    float m_timeScale      = 0.2f;    // 时间比例：1 现实秒 = 多少游戏小时（可负=倒流）
    bool  m_sunMoving      = true;    // 时间是否流动（l 切换；关 = 冻结时间）
    static constexpr float kTimeScaleMax = 6.0f; // 比例上限（1 现实秒最多 6 游戏小时 → 4 秒一昼夜）

    // 白天->夜晚的强度系数 [0,1]，地平线附近平滑过渡
    float m_sunIntensity = 1.0f;
    // 日出/日落暖色系数 [0,1]，1=完全暖色(橙红)，0=中午白光
    float m_sunWarmth = 0.0f;
    // 太阳当前色温对应的 diffuse 颜色（随 m_sunWarmth 在冷白↔暖橙间插值）
    glm::vec3 m_sunDiffuseColor = glm::vec3(1.0f);

    // 夜晚冻结用的缓存（避免光方向退化时级联矩阵抖动）。阶段 3：缓存整个级联数组。
    glm::mat4 m_cachedCascadeLightMatrix[CASCADE_COUNT];
    float     m_cachedCascadeSplitView[CASCADE_COUNT] = { 0.0f };
    float     m_cachedCascadeWorldExtent[CASCADE_COUNT] = { 0.0f };
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
    // 挤出物品模型（掉落物 / 手持）：alpha discard 出轮廓
    Shader m_itemShader{ {
        { GL_VERTEX_SHADER,   "shader/item.vert" },
        { GL_FRAGMENT_SHADER, "shader/item.frag" }
    } };
    // 方块物品立方体（掉落物 / 手持 / UI 图标）：采样方块纹理数组
    Shader m_blockItemShader{ {
        { GL_VERTEX_SHADER,   "shader/block_item.vert" },
        { GL_FRAGMENT_SHADER, "shader/block_item.frag" }
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
    bool createShadowMapTargets();     // CSM 深度纹理数组 + FBO
    void destroyShadowMapTargets();
    // 计算 N 个级联的视图空间切分远边界（practical split scheme：对数+均匀混合）
    void computeCascadeSplits(float nearP, float farP, float lambda, int count, float* outSplitView);
    // 为级联 i（视距区间 [splitNear, splitFar]）算贴合子视锥包围球的正交光空间矩阵 +
    // 各自 snap-to-texel。返回该级联的 lightSpaceMatrix。
    glm::mat4 computeCascadeMatrix(float splitNear, float splitFar,
        const glm::mat4& camView, const glm::mat4& camProj,
        const glm::vec3& lightDirNorm, int cascadeSize, float& outWorldExtent);
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
    // CSM：逐级联算光锥 + 渲染深度到 m_csmDepth 各 layer。结果写入成员
    // m_cascadeLightMatrix[] / m_cascadeSplitView[]（不再走出参）。
    // camView/camProj 用未抖动的相机矩阵，按视距切子视锥。
    void sunShineShadowMap(const ChunkManager& chunkManager, const std::shared_ptr<Camera>camera,
        const glm::mat4& camView, const glm::mat4& camProj);
    // 单帧 PCSS 可见度 → m_shadowVisCurr（按视距选级联，采 m_csmDepth）
    void shadowVisibilityPass(const glm::mat4& view, const glm::mat4& projection);
    // 时域累积：重投影历史 + 邻域 clamp + 混合 → m_shadowAccum[curr]
    void shadowAccumulatePass(const glm::mat4& invViewProj, const glm::mat4& prevViewProj);
    void lightingPass(const std::shared_ptr<Camera>camera,
        const glm::mat4& view, const glm::mat4& projection);
    void renderOutlines(const glm::mat4& view, const glm::mat4& projection);
    void renderUI();
    // 掉落物：用挤出模型 / 方块立方体 + item 着色器在世界空间绘制
    void renderDroppedItems(const glm::mat4& view, const glm::mat4& projection,
        DroppedItemManager* items);

    // 为所有方块物品离屏渲染等距立方体 UI 图标，回填 ItemDefinition::guiIconTexture。
    // 需在方块纹理数组 + BlockFaceType 映射 + ItemRegistry 就绪后调用（initialize 末尾）。
    void generateBlockIcons();
    GLuint m_blockTextureArray = 0;             // 方块纹理数组（generateBlockIcons / 掉落物立方体用）
    std::vector<GLuint> m_blockIconTextures;    // 生成的 UI 图标纹理（析构时释放）

    void renderModel(const std::shared_ptr<Camera>camera,
        const glm::mat4& view, const glm::mat4& projection, Player* player);
    // 第一人称手部模型：常驻镜头右下角，点击左键挥一下、长按循环挥。
    // 在 TAA/合成之后、UI 之前画到默认帧缓冲（避免时域拖影，且被准星/物品栏覆盖）。
    // 所有可调参数硬编码在该函数体顶部的「可调参数」块里。
    void renderFirstPersonHand(Player* player, float deltaTime);
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

    // 物品自定义 OBJ 模型缓存（modelPath → Model），按需懒加载（如望远镜）
    std::unordered_map<std::string, std::unique_ptr<Model>> m_itemModels;
    Model* getItemModel(const std::string& path);
};