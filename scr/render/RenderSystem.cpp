#include "RenderSystem.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "../TextureMgr.h"
#include "../UI/UIHotbar.h"
#include "../Player.h"
#include "../mode/PlayerModel.h"
#include "../mode/SkinManager.h"
#include "../stb_image.h"
#include "../Profiler.h"
#include "../net/NetManager.h"
#include "../collision/PhysicsConstants.h"
#include "../RuntimeConfig.h"
#include <random>
#include <cmath>
#include <vector>
BlockRenderer::BlockRenderer()
    : VAO(0), VBO(0), EBO(0) {

}

BlockRenderer::~BlockRenderer() {

}

bool BlockRenderer::initialize() {
    // 创建顶点数据
    createFaceVertices();

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    // 绑定顶点数据
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(FaceVertex),
        m_vertices.data(), GL_STATIC_DRAW);

    // 绑定索引数据
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int),
        m_indices.data(), GL_STATIC_DRAW);

    // 配置顶点属性
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FaceVertex),
        (void*)offsetof(FaceVertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FaceVertex),
        (void*)offsetof(FaceVertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
        sizeof(FaceVertex), (void*)offsetof(FaceVertex, texCoord));
    glEnableVertexAttribArray(2);

    // 实例属性的具体源 VBO 由 ChunkManager 通过 bindArenaVBO 注入
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void BlockRenderer::uploadEndLayerLookup() {
    // BlockFaceType::end_layer_by_type 是 int[256]，shader 端 uniform 用 int 数组接收。
    // glUniform1iv 一次性上传所有 256 个值。
    m_shader.use();
    const int* lookup = BlockFaceType::getEndLayerLookup();
    GLint loc = glGetUniformLocation(m_shader.programID, "uEndLayerLookup");
    if (loc >= 0) {
        glUniform1iv(loc, BlockFaceType::kEndLayerLookupSize, lookup);
    }
}

void BlockRenderer::bindInstanceAttribs() {
    // 调用前需保证 VAO 已绑定且 GL_ARRAY_BUFFER 已绑定到 arena VBO
    // 8 字节 InstanceData 布局：packed32 + blockType16 + textureLayer16
    // location 5: packed (uint, 含 x/y/z/face/orient)
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 1, GL_UNSIGNED_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, packed));
    glVertexAttribDivisor(5, 1);

    // location 7: blockType (ushort)
    glEnableVertexAttribArray(7);
    glVertexAttribIPointer(7, 1, GL_UNSIGNED_SHORT, sizeof(InstanceData), (void*)offsetof(InstanceData, blockType));
    glVertexAttribDivisor(7, 1);

    // location 8: textureLayer (ushort)
    glEnableVertexAttribArray(8);
    glVertexAttribIPointer(8, 1, GL_UNSIGNED_SHORT, sizeof(InstanceData), (void*)offsetof(InstanceData, textureLayer));
    glVertexAttribDivisor(8, 1);
}

void BlockRenderer::bindArenaVBO(GLuint arenaVBO) {
    if (arenaVBO == 0) return;
    if (arenaVBO == m_currentArenaVBO) return;
    m_currentArenaVBO = arenaVBO;

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, arenaVBO);
    bindInstanceAttribs();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void BlockRenderer::render(GLuint indirectBuffer, int cmdCount,
    const glm::mat4& view, const glm::mat4& projection)
{
    if (cmdCount <= 0 || indirectBuffer == 0) return;

    m_shader.use();
    m_shader.setMat4("uView", view);
    m_shader.setMat4("uProjection", projection);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArray);
    m_shader.setInt("uTextureArray", 0);

    glBindVertexArray(VAO);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
        (const void*)0, cmdCount, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
}

void BlockRenderer::renderDepth(GLuint indirectBuffer, int cmdCount,
    const glm::mat4& lightSpaceMatrix, float nearPlane, float farPlane)
{
    if (cmdCount <= 0 || indirectBuffer == 0) return;

    m_depthShader.use();
    m_depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
    m_depthShader.setFloat("dir_near", nearPlane);
    m_depthShader.setFloat("dir_far", farPlane);

    glBindVertexArray(VAO);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, indirectBuffer);
    glMultiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_INT,
        (const void*)0, cmdCount, 0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    glBindVertexArray(0);
}

void BlockRenderer::createFaceVertices() {
    // 创建一个单位正方形的面（+Z方向，即正面）
    // 顶点位置：正方形的4个角
    // 法线：+Z方向
    // 纹理坐标：从(0,0)到(1,1)

    m_vertices.clear();
    m_indices.clear();

    // 定义4个顶点（一个正方形）
    FaceVertex vertices[4] = {
        // 左下角
        {glm::vec3(-0.5f, -0.5f, 0.0f),  // 位置
        glm::vec3(0.0f, 0.0f, 1.0f),    // 法线（+Z方向）
        glm::vec2(0.0f, 1.0f)},         // 纹理坐标

        // 右下角
        {glm::vec3(0.5f, -0.5f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec2(1.0f, 1.0f)},

        // 右上角
        {glm::vec3(0.5f, 0.5f,0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec2(1.0f, 0.0f)},

        // 左上角
        {glm::vec3(-0.5f, 0.5f, 0.0f),
        glm::vec3(0.0f, 0.0f, 1.0f),
        glm::vec2(0.0f, 0.0f)}
    };

    // 复制到成员变量
    m_vertices.assign(vertices, vertices + 4);

    // 定义索引（2个三角形，构成一个正方形）
    unsigned int indices[6] = {
        0, 1, 2,  // 第一个三角形
        0, 2, 3   // 第二个三角形
    };

    m_indices.assign(indices, indices + 6);
}



// RenderSystem实现
RenderSystem::RenderSystem(int screenWidth, int screenHeight)
    : m_screenWidth(screenWidth)
    , m_screenHeight(screenHeight)
    , m_gBuffer(0)
    , m_screenQuadVAO(0)
    , m_screenQuadVBO(0)
    , m_lightDirection(glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f)))
    , m_lightColor(glm::vec3(1.0f, 0.95f, 0.9f))
    , m_lightIntensity(1.0f)
    , m_ambientColor(glm::vec3(0.1f, 0.1f, 0.1f))
    , m_drawCalls(0)
    , m_totalInstances(0) {
}

RenderSystem::~RenderSystem() {
    destroyGBuffer();
    destroyTAATargets();
    destroyShadowVisTargets();
    destroyAoAccumTargets();
    if (m_blueNoiseTex) glDeleteTextures(1, &m_blueNoiseTex);

    if (m_screenQuadVAO) glDeleteVertexArrays(1, &m_screenQuadVAO);
    if (m_screenQuadVBO) glDeleteBuffers(1, &m_screenQuadVBO);

    for (auto& [name, tex] : m_skinTextures) {
        if (tex) glDeleteTextures(1, &tex);
    }
    m_skinTextures.clear();
}

bool RenderSystem::initialize() {
    // 初始化方块渲染器
    if (!m_blockRenderer.initialize()) {
        std::cerr << "Failed to initialize BlockRenderer!" << std::endl;
        return false;
    }
    // 从 TextureMgr 获取纹理数组并设置给 BlockRenderer
    auto texMgr = TextureMgr::GetInstance();
    m_blockRenderer.setTextureArray(texMgr->GetTextureArray());
    // 上传"端面纹理层"查表给 g_buffer shader（带轴方块如原木横躺时朝向主轴的两面用它）。
    // 仅需上传一次：BlockFaceType 已在 World::run 启动早期调用 init_type_map 填好。
    m_blockRenderer.uploadEndLayerLookup();


    // 初始化边框渲染器
    if (!m_outlineRenderer.initialize()) {
        std::cerr << "Failed to initialize BlockOutlineRenderer!" << std::endl;
        return false;
    }

    // 初始化UI系统
    initUI();

    // 初始化粒子管理器
    if (!m_particleManager.initialize()) {
        std::cerr << "Failed to initialize ParticleManager!" << std::endl;
        return false;
    }

    // 远程玩家模型（共享几何体，不同皮肤可后续扩展）
    m_remotePlayerModel.initialize("assert/mode/player/wide/steve.png");

    // 初始化皮肤管理器并预加载所有皮肤纹理
    SkinManager::instance().init("assert/mode/player/wide");
    loadAllSkinTextures();

    // 配置边框
    BlockOutlineRenderer::OutlineConfig outlineConfig;
    outlineConfig.color = glm::vec3(0.0f, 0.0f, 0.0f);  // 黑色边框
    outlineConfig.lineWidth = 4.0f;
    outlineConfig.pulseSpeed = 3.0f;
    outlineConfig.pulseIntensity = 0.15f;
    outlineConfig.enablePulse = true;
    // 启用硬件深度测试（GL_LEQUAL），让被前景方块挡住的边自动剔除。
    // outlineScale 必须保持 1.0 与方块表面共面，否则放大后线段浮在表面前方，
    // LEQUAL 会让 12 条边全部通过测试（背面也画出来）。
    outlineConfig.depthTest = true;
    outlineConfig.outlineScale = 1.0f;
    m_outlineRenderer.setConfig(outlineConfig);


    // 创建G-Buffer
    if (!createGBuffer()) {
        std::cerr << "Failed to create G-Buffer!" << std::endl;
        return false;
    }

    // 创建全屏四边形
    createScreenQuad();

    m_deferredLightingShader.use();
    m_deferredLightingShader.setInt("gDepth", 0);     // 深度纹理（重建世界空间位置）
    m_deferredLightingShader.setInt("gNormal", 1);
    m_deferredLightingShader.setInt("gAlbedo", 2);
    m_deferredLightingShader.setInt("gProperties", 3);
    m_deferredLightingShader.setInt("aoTex", 4);
    m_deferredLightingShader.setInt("shadowVisibility", 5);

    m_modeShader.use();

    m_modeShader.setVec3("light.direction", lightDir);
    m_modeShader.setVec3("light.ambient", 0.4f, 0.4f, 0.4f);
    m_modeShader.setVec3("light.diffuse", 0.9f, 0.9f, 0.9f);
    m_modeShader.setVec3("light.specular", 1.0f, 1.0f, 1.0f);
    m_modeShader.setFloat("shininess", 256.0f);

    // Sample kernel
    std::uniform_real_distribution<GLfloat> randomFloats(0.0, 1.0);
    std::default_random_engine generator;
    ssaoKernel.clear();
    for (GLuint i = 0; i < 64; ++i) {
        glm::vec3 sample;
        // 在半球内生成随机方向（z >= 0）
        do {
            sample = glm::vec3(randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator) * 2.0f - 1.0f,
                randomFloats(generator));  // z在[0,1]
        } while (glm::length(sample) > 1.0f);  // 保证在单位半球内
        sample = glm::normalize(sample);
        sample *= randomFloats(generator);     // 随机距离 [0,1]

        // 按索引缩放，使采样点更集中在中心
        float scale = (float)i / 64.0f;
        scale = 0.1f + scale * scale * 0.9f;   // 从0.1线性增加到1.0
        sample *= scale;

        ssaoKernel.push_back(sample);
    }

    // Noise texture 4x4：与下面的 4x4 box blur 周期严格匹配，
    // 确保 blur 能在一个 tile 内把所有方向的随机旋转都平均到，完全消除条纹/三角图案
    std::vector<glm::vec3> ssaoNoise;
    for (GLuint i = 0; i < 16; i++)  // 4x4 = 16 samples
    {
        glm::vec3 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f);
        ssaoNoise.push_back(noise);
    }
    glGenTextures(1, &m_noiseTexture);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    m_hbaoShader.use();
    m_hbaoShader.setInt("gDepth", 0);          // 深度纹理（重建视图空间位置）
    m_hbaoShader.setInt("gNormal", 1);
    m_hbaoShader.setInt("texNoise", 2);
    m_hbaoShader.setVec2("screenSize", glm::vec2(m_screenWidth, m_screenHeight));

    m_hbaoBlurShader.use();
    m_hbaoBlurShader.setInt("aoInput", 0);
    std::cout << "RenderSystem initialized successfully!" << std::endl;



    // 配置阴影深度贴图 FBO（阶段 2：退 VSSM → 普通深度 + PCSS/PCF）
    // 不再存 (d, d²) 的 RG32F 颜色，改用纯深度纹理附件，硬件直接写深度，
    // FBO 无颜色附件（glDrawBuffer/ReadBuffer = GL_NONE），最省带宽且无每帧 mipmap 生成。
    glGenFramebuffers(1, &m_depthMapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);

    glGenTextures(1, &m_depthMap);
    glBindTexture(GL_TEXTURE_2D, m_depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, SHADOW_WIDTH, SHADOW_HEIGHT,
        0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    // 必须 GL_LINEAR：PCSS 的半影连续过渡依赖深度图亚纹素插值。
    // consumer 端 PCF 每个 tap 是硬比较（currentDepth <= d ? 1 : 0），可见度 = N 个 tap 的平均。
    // 用 GL_NEAREST 时，接触处半影收窄会让所有 tap 落进同一纹素读到相同深度，
    // 平均恰好为 0 或 1 —— 半影梯度被抹平，整面非黑即白（"只有亮暗没有阴影"）。
    // GL_LINEAR 下 COMPARE_MODE=GL_NONE 返回的就是插值后的原始深度，行为是定义良好的。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    // 显式关闭深度比较模式：保证 texture(shadowMap, uv).r 返回（插值后的）原始深度而非比较结果。
    // 默认值各驱动可能不一致 —— 显式设 GL_NONE 消除环境相关行为。
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    // 边界深度=1.0（最远），保证阴影框外的 receiver 采到"无遮挡"，不产生假阴影
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthMap, 0);

    // 无颜色附件：显式关闭颜色读写
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    // 检查阴影FBO是否完整
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "ERROR::FRAMEBUFFER::m_depthMapFBO is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);



    // 创建光照FBO
    if (!createLightingFBO()) {
        std::cerr << "Failed to create lighting FBO!" << std::endl;
        return false;
    }

    // 创建合成FBO
    if (!createCompositeFBO()) {
        std::cerr << "Failed to create composite FBO!" << std::endl;
        return false;
    }

    // 创建 TAA 历史缓冲
    if (!createTAATargets()) {
        std::cerr << "Failed to create TAA targets!" << std::endl;
        return false;
    }

    // 程序生成蓝噪声纹理（阴影 PCSS 抖动源）
    createBlueNoiseTexture();

    // 创建阴影可见度 + 时域累积缓冲
    if (!createShadowVisTargets()) {
        std::cerr << "Failed to create shadow visibility targets!" << std::endl;
        return false;
    }

    // 创建 AO 时域累积缓冲
    if (!createAoAccumTargets()) {
        std::cerr << "Failed to create AO accum targets!" << std::endl;
        return false;
    }
    // TAA shader 纹理单元绑定（固定）
    m_taaShader.use();
    m_taaShader.setInt("currColor", 0);
    m_taaShader.setInt("history", 1);
    m_taaShader.setInt("depthTex", 2);
    m_taaShader.setVec2("screenSize", glm::vec2(m_screenWidth, m_screenHeight));

    return true;
}


// 添加新的initUI方法
void RenderSystem::initUI() {
    m_uiManager.initialize(m_screenWidth, m_screenHeight);

    // 加载UI纹理（如果尚未加载）
    auto texMgr = TextureMgr::GetInstance();
    // 物品栏纹理
    if (texMgr->GetTexture2D("hotbar_selection") == 0) {
        texMgr->LoadTexture2DManual("hotbar_selection", "UI/hotbar_selection.png", false);
    }
    if (texMgr->GetTexture2D("hotbar_offhand_left") == 0) {
        texMgr->LoadTexture2DManual("hotbar_offhand_left", "UI/hotbar_offhand_left.png", false);
    }
    if (texMgr->GetTexture2D("hotbar_offhand_right") == 0) {
        texMgr->LoadTexture2DManual("hotbar_offhand_right", "UI/hotbar_offhand_right.png", false);
    }
    if (texMgr->GetTexture2D("inventory") == 0) {
        texMgr->LoadTexture2DManual("inventory", "UI/inventory.png", false);
    }
    // 物品图标纹理（示例）- 配置文件已加载，无需手动加载

    // 创建示例UI组件
    createSampleUI();
}

// 创建示例UI组件（可根据需要修改）
void RenderSystem::createSampleUI() {
    // 十字准星（不透明）
    auto crosshair = std::make_shared<UIImage>("crosshair");
    crosshair->setSize(40, 40);
    crosshair->setPosition(m_screenWidth * 0.5f, m_screenHeight * 0.5f);
    crosshair->setColor(1.0f, 1.0f, 1.0f, 1.0f);
    crosshair->anchor = glm::vec2(0.5f); // 居中
    crosshair->zIndex = 100; // 最高层级
    // 如果要使用原版原图需: “硬像素对齐 + 最近邻过滤 + 正确 Alpha 混合”，
    crosshair->loadTextureByTextureName("crosshair");


    //// 水平线
    //auto crosshair2 = std::make_shared<UIRect>("crosshair2");
    //crosshair2->setSize(20, 4);
    //crosshair2->setPosition(m_screenWidth * 0.5f, m_screenHeight * 0.5f);
    //crosshair2->setColor(1.0f, 1.0f, 1.0f, 1.0f);
    //crosshair2->anchor = glm::vec2(0.5f);
    //crosshair2->zIndex = 100;

    //// 物品栏（半透明）
    //auto inventory = std::make_shared<UIRect>("inventory");
    //inventory->setSize(400, 80);
    //inventory->setPosition(m_screenWidth * 0.5f, 50);
    //inventory->setColor(0.1f, 0.1f, 0.1f, 0.7f); // 半透明黑色
    //inventory->borderRadius = 10.0f;
    //inventory->anchor = glm::vec2(0.5f, 0.0f); // 底部居中
    //inventory->zIndex = 50;

    // 添加UI组件
    m_uiManager.addComponent(crosshair);
    //m_uiManager.addComponent(crosshair2);
    //m_uiManager.addComponent(inventory);

    // 创建物品栏（像素完美，参照原版 MC 度量）
    auto hotbar = std::make_shared<UIHotbar>("hotbar", 10);
    hotbar->setGuiScaleForScreen(m_screenWidth, m_screenHeight);
    hotbar->anchor = glm::vec2(0.5f, 0.0f);         // 水平居中、底部对齐
    // 距屏幕底 2 个逻辑像素（等同原版 MC 的 hotbar 底距）
    hotbar->setPosition(m_screenWidth * 0.5f, static_cast<float>(2 * hotbar->getGuiScale()));
    hotbar->zIndex = 50;
    // 设置一些示例物品
    hotbar->setSlotItem(0, "Stone");
    hotbar->setSlotItem(1, "Birch_Log");
    hotbar->setSlotItem(2, "Cobblestone");
    hotbar->setSlotItem(3, "Oak_Planks");
    hotbar->setSlotItem(4, "spyglass");
    m_uiManager.addComponent(hotbar);
}
// 添加UI渲染方法
void RenderSystem::renderUI() {
    //static float uiTime = 0.0f;
    //uiTime += 0.016f; // 假设60FPS

    // 更新UI
    m_uiManager.update(0.016f);

    // 渲染UI
    m_uiManager.render();
}


// 添加屏幕尺寸设置方法
void RenderSystem::setScreenSize(int width, int height) {
    m_screenWidth = width;
    m_screenHeight = height;

    // 通知UI管理器屏幕尺寸变化
    m_uiManager.onScreenResize(width, height);
}


void RenderSystem::move_DirLight(float deltaTime)
{
    // test
    //deltaTime = 0;

    static float time_now = 1.0f;
    const float rotate_speed = 0.08f;   // 一个完整昼夜周期约 2π/0.08 ≈ 78 秒
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


bool RenderSystem::createGBuffer() {
    glGenFramebuffers(1, &m_gBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer);

    // 位置缓冲区
    glGenTextures(1, &m_gPosition);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_screenWidth, m_screenHeight,
        0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_gPosition, 0);

    // 法线缓冲区
    glGenTextures(1, &m_gNormal);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_screenWidth, m_screenHeight,
        0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_gNormal, 0);

    // 反照率缓冲区
    glGenTextures(1, &m_gAlbedo);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_screenWidth, m_screenHeight,
        0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_gAlbedo, 0);

    // 属性缓冲区
    glGenTextures(1, &m_gProperties);
    glBindTexture(GL_TEXTURE_2D, m_gProperties);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_screenWidth, m_screenHeight,
        0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, m_gProperties, 0);

    // 告诉OpenGL我们要绘制到哪些颜色附件
    GLuint attachments[4] = {
        GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3
    };
    glDrawBuffers(4, attachments);

    // 创建深度纹理
    glGenTextures(1, &m_depthTexture);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, m_screenWidth, m_screenHeight,
        0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);

    // 检查FBO是否完整
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer not complete!" << std::endl;
        return false;
    }

    // Also create framebuffer to hold HBAO processing stage
    glGenFramebuffers(1, &m_hbaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hbaoFBO);
    // - HBAO color buffer
    glGenTextures(1, &m_hbaoColorBuffer);
    glBindTexture(GL_TEXTURE_2D, m_hbaoColorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_screenWidth, m_screenHeight, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hbaoColorBuffer, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "HBAO Framebuffer not complete!" << std::endl;

    // - and blur stage
    glGenFramebuffers(1, &m_hbaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hbaoBlurFBO);
    glGenTextures(1, &m_hbaoColorBufferBlur);
    glBindTexture(GL_TEXTURE_2D, m_hbaoColorBufferBlur);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_screenWidth, m_screenHeight, 0, GL_RED, GL_FLOAT, nullptr);    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hbaoColorBufferBlur, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "HBAO Blur Framebuffer not complete!" << std::endl;


    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void RenderSystem::destroyGBuffer() {
    if (m_gBuffer) glDeleteFramebuffers(1, &m_gBuffer);
    if (m_depthTexture) glDeleteTextures(1, &m_depthTexture);

    if (m_gPosition) glDeleteTextures(1, &m_gPosition);
    if (m_gNormal) glDeleteTextures(1, &m_gNormal);
    if (m_gAlbedo) glDeleteTextures(1, &m_gAlbedo);
    if (m_gProperties) glDeleteTextures(1, &m_gProperties);

    m_gBuffer = 0;
}

bool RenderSystem::createLightingFBO() {
    glGenFramebuffers(1, &m_lightingFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_lightingFBO);

    glGenTextures(1, &m_lightingColorTexture);
    glBindTexture(GL_TEXTURE_2D, m_lightingColorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_screenWidth, m_screenHeight,
        0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_lightingColorTexture, 0);

    GLuint attachments[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, attachments);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Lighting FBO not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool RenderSystem::createCompositeFBO() {
    glGenFramebuffers(1, &m_compositeFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_compositeFBO);

    // 颜色纹理（与光照纹理格式一致）
    glGenTextures(1, &m_compositeColor);
    glBindTexture(GL_TEXTURE_2D, m_compositeColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_screenWidth, m_screenHeight,
        0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_compositeColor, 0);

    // 共享 G-Buffer 的深度纹理，避免 blit 深度的兼容性问题
    // m_depthTexture 在 createGBuffer() 中已创建，这里直接附加到 compositeFBO
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);

    GLuint attachments[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, attachments);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Composite FBO not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void RenderSystem::destroyCompositeFBO() {
    if (m_compositeFBO) glDeleteFramebuffers(1, &m_compositeFBO);
    if (m_compositeColor) glDeleteTextures(1, &m_compositeColor);
    // 注意：不删除深度纹理，它是 G-Buffer 的 m_depthTexture，由 destroyGBuffer() 管理
    m_compositeFBO = 0;
    m_compositeColor = 0;
    m_compositeDepth = 0;
}

// TAA 历史 ping-pong：两张 RGBA16F 全屏纹理 + 各自 FBO。
bool RenderSystem::createTAATargets() {
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &m_taaFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_taaFBO[i]);

        glGenTextures(1, &m_taaHistory[i]);
        glBindTexture(GL_TEXTURE_2D, m_taaHistory[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_screenWidth, m_screenHeight,
            0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_taaHistory[i], 0);

        GLuint attachments[1] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, attachments);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "TAA FBO " << i << " not complete!" << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_taaHistoryValid = false;
    return true;
}

void RenderSystem::destroyTAATargets() {
    for (int i = 0; i < 2; ++i) {
        if (m_taaFBO[i]) glDeleteFramebuffers(1, &m_taaFBO[i]);
        if (m_taaHistory[i]) glDeleteTextures(1, &m_taaHistory[i]);
        m_taaFBO[i] = 0;
        m_taaHistory[i] = 0;
    }
    m_taaHistoryValid = false;
}

// 阴影可见度（单帧）+ 时域累积 ping-pong 缓冲。全部 R8 单通道（可见度 [0,1]）。
bool RenderSystem::createShadowVisTargets() {
    // 当前帧单帧可见度
    glGenFramebuffers(1, &m_shadowVisFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowVisFBO);
    glGenTextures(1, &m_shadowVisCurr);
    glBindTexture(GL_TEXTURE_2D, m_shadowVisCurr);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_screenWidth, m_screenHeight, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_shadowVisCurr, 0);
    { GLuint a[1] = { GL_COLOR_ATTACHMENT0 }; glDrawBuffers(1, a); }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Shadow visibility FBO not complete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // 累积 ping-pong
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &m_shadowAccumFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowAccumFBO[i]);
        glGenTextures(1, &m_shadowAccum[i]);
        glBindTexture(GL_TEXTURE_2D, m_shadowAccum[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, m_screenWidth, m_screenHeight, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_shadowAccum[i], 0);
        GLuint a[1] = { GL_COLOR_ATTACHMENT0 }; glDrawBuffers(1, a);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "Shadow accum FBO " << i << " not complete!" << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_shadowAccumValid = false;
    return true;
}

void RenderSystem::destroyShadowVisTargets() {
    if (m_shadowVisFBO) glDeleteFramebuffers(1, &m_shadowVisFBO);
    if (m_shadowVisCurr) glDeleteTextures(1, &m_shadowVisCurr);
    m_shadowVisFBO = 0; m_shadowVisCurr = 0;
    for (int i = 0; i < 2; ++i) {
        if (m_shadowAccumFBO[i]) glDeleteFramebuffers(1, &m_shadowAccumFBO[i]);
        if (m_shadowAccum[i]) glDeleteTextures(1, &m_shadowAccum[i]);
        m_shadowAccumFBO[i] = 0; m_shadowAccum[i] = 0;
    }
    m_shadowAccumValid = false;
}

// AO 时域累积 ping-pong 缓冲（R16F，与 HBAO buffer 同格式）。
bool RenderSystem::createAoAccumTargets() {
    for (int i = 0; i < 2; ++i) {
        glGenFramebuffers(1, &m_aoAccumFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_aoAccumFBO[i]);
        glGenTextures(1, &m_aoAccum[i]);
        glBindTexture(GL_TEXTURE_2D, m_aoAccum[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_screenWidth, m_screenHeight, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_aoAccum[i], 0);
        GLuint a[1] = { GL_COLOR_ATTACHMENT0 }; glDrawBuffers(1, a);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "AO accum FBO " << i << " not complete!" << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_aoAccumValid = false;
    return true;
}

void RenderSystem::destroyAoAccumTargets() {
    for (int i = 0; i < 2; ++i) {
        if (m_aoAccumFBO[i]) glDeleteFramebuffers(1, &m_aoAccumFBO[i]);
        if (m_aoAccum[i]) glDeleteTextures(1, &m_aoAccum[i]);
        m_aoAccumFBO[i] = 0; m_aoAccum[i] = 0;
    }
    m_aoAccumValid = false;
}

// 程序生成 tileable 蓝噪声纹理（void-and-cluster 算法，Ulichney 1993）。
// 输出每像素一个 [0,1] 的 rank/256，能量谱呈高频（蓝噪声）。供阴影 PCSS 抖动用：
// 配合"帧序号驱动的黄金角旋转"，让每帧采样方向去相关，TAA 跨帧累积成干净结果。
void RenderSystem::createBlueNoiseTexture() {
    const int N = m_blueNoiseSize;       // 64
    const int total = N * N;
    const float sigma = 1.9f;            // 高斯能量核标准差
    const float twoSigma2 = 2.0f * sigma * sigma;

    // 预计算环形（toroidal）距离的高斯权重 LUT，半径取 ~3σ
    const int R = 6;
    std::vector<float> gauss((2 * R + 1) * (2 * R + 1));
    for (int dy = -R; dy <= R; ++dy)
        for (int dx = -R; dx <= R; ++dx)
            gauss[(dy + R) * (2 * R + 1) + (dx + R)] =
                std::exp(-float(dx * dx + dy * dy) / twoSigma2);

    std::vector<float> energy(total, 0.0f);     // 当前能量场
    std::vector<unsigned char> binary(total, 0); // 当前二值图（1=有点）
    std::vector<int> rank(total, 0);

    auto idx = [N](int x, int y) {
        x = (x % N + N) % N;  y = (y % N + N) % N;
        return y * N + x;
    };
    // 在 (px,py) 处增/删一个点，环形地更新能量场
    auto splat = [&](int px, int py, float sign) {
        for (int dy = -R; dy <= R; ++dy)
            for (int dx = -R; dx <= R; ++dx)
                energy[idx(px + dx, py + dy)] += sign * gauss[(dy + R) * (2 * R + 1) + (dx + R)];
    };

    std::default_random_engine rng(12345u);
    std::uniform_int_distribution<int> pick(0, total - 1);

    // 初始：随机撒约 1/10 的点作为初始二值图
    const int initialPoints = total / 10;
    int placed = 0;
    while (placed < initialPoints) {
        int p = pick(rng);
        if (!binary[p]) { binary[p] = 1; splat(p % N, p / N, +1.0f); ++placed; }
    }

    // 阶段 1：反复把"最紧簇"的点移到"最大空洞"，直到稳定（去相关初始分布）
    for (int iter = 0; iter < total * 4; ++iter) {
        // 最紧簇：二值=1 中能量最大者
        int tight = -1; float emax = -1e30f;
        for (int i = 0; i < total; ++i)
            if (binary[i] && energy[i] > emax) { emax = energy[i]; tight = i; }
        binary[tight] = 0; splat(tight % N, tight / N, -1.0f);
        // 最大空洞：二值=0 中能量最小者
        int vd = -1; float emin = 1e30f;
        for (int i = 0; i < total; ++i)
            if (!binary[i] && energy[i] < emin) { emin = energy[i]; vd = i; }
        binary[vd] = 1; splat(vd % N, vd / N, +1.0f);
        if (vd == tight) break; // 稳定
    }

    // 阶段 2：保存初始二值图副本，给前 initialPoints 个点逆向赋 rank
    std::vector<unsigned char> initialBinary = binary;
    std::vector<float> energyCopy = energy;
    int rankCounter = initialPoints - 1;
    for (int k = 0; k < initialPoints; ++k) {
        int tight = -1; float emax = -1e30f;
        for (int i = 0; i < total; ++i)
            if (binary[i] && energy[i] > emax) { emax = energy[i]; tight = i; }
        rank[tight] = rankCounter--;
        binary[tight] = 0; splat(tight % N, tight / N, -1.0f);
    }

    // 阶段 3：从初始二值图开始，逐个填最大空洞，递增赋 rank
    binary = initialBinary;
    energy = energyCopy;
    for (int k = initialPoints; k < total; ++k) {
        int vd = -1; float emin = 1e30f;
        for (int i = 0; i < total; ++i)
            if (!binary[i] && energy[i] < emin) { emin = energy[i]; vd = i; }
        rank[vd] = k;
        binary[vd] = 1; splat(vd % N, vd / N, +1.0f);
    }

    // rank → [0,1]，写成单通道 8-bit 纹理
    std::vector<unsigned char> pixels(total);
    for (int i = 0; i < total; ++i)
        pixels[i] = (unsigned char)((rank[i] * 255) / (total - 1));

    glGenTextures(1, &m_blueNoiseTex);
    glBindTexture(GL_TEXTURE_2D, m_blueNoiseTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, N, N, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Halton 低差异序列：base 进制的 radical inverse
static float haltonRadicalInverse(unsigned i, unsigned base) {
    float f = 1.0f, r = 0.0f;
    while (i > 0) {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}

// Halton(2,3) 亚像素抖动，返回 NDC 单位的偏移。序列长度 8，按帧序号循环。
glm::vec2 RenderSystem::haltonJitterNDC(unsigned frameIndex, int w, int h) {
    const unsigned kSeqLen = 8;
    unsigned idx = (frameIndex % kSeqLen) + 1;   // Halton 从 1 开始（0 给 0）
    float jx = haltonRadicalInverse(idx, 2) - 0.5f;   // 像素单位 [-0.5, 0.5]
    float jy = haltonRadicalInverse(idx, 3) - 0.5f;
    // 像素 → NDC：一个像素跨度 = 2/分辨率
    return glm::vec2(jx * 2.0f / (float)w, jy * 2.0f / (float)h);
}

void RenderSystem::createScreenQuad() {
    float quadVertices[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_screenQuadVAO);
    glGenBuffers(1, &m_screenQuadVBO);

    glBindVertexArray(m_screenQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_screenQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void RenderSystem::render(const ChunkManager& chunkManager,
    const glm::mat4& view,
    const glm::mat4& projection,
    std::shared_ptr<Camera> camera,
    float deltaTime,
    Player* player,
    NetManager* netManager
) {

    PROFILE_SCOPE("RenderSystem::render");
    m_currentTime += deltaTime;

    // 更新光源位置（含日出/日落平滑过渡强度 m_sunIntensity）
    move_DirLight(deltaTime);

    // 粒子系统按 active chunk 范围限制采样
    {
        PROFILE_SCOPE("particles.update");
        std::vector<glm::ivec2> activeChunkPositions = chunkManager.getActiveChunkPositions();
        m_particleManager.update(deltaTime, camera, activeChunkPositions);
    }

    // ---- TAA：投影矩阵亚像素抖动 ----
    // jitteredProj 用于所有写入当前帧颜色/深度的几何（G-Buffer + forward），
    // 让每帧采样落在像素内不同位置，多帧累积等效超采样。
    // projection（未抖动）保留给 HBAO（AO 不应吃几何 jitter）与 motion vector 反算。
    glm::mat4 jitteredProj = projection;
    if (m_taaEnabled) {
        glm::vec2 jitterNDC = haltonJitterNDC(m_frameIndex, m_screenWidth, m_screenHeight);
        jitteredProj[2][0] += jitterNDC.x;   // 列主序 m[col][row]，平移作用于裁剪空间 xy
        jitteredProj[2][1] += jitterNDC.y;
    }
    const glm::mat4& geoProj = m_taaEnabled ? jitteredProj : projection;

    // 几何通道：渲染方块到 G-Buffer（抖动投影）
    { PROFILE_SCOPE("geometryPass"); geometryPass(chunkManager, view, geoProj); }

    // HBAO 通道：位置重建/投影必须用与深度一致的 geoProj（深度由 geoProj 写入）。
    // jitter 是全屏统一的亚像素平移，不会给 AO 引入偏置。
    // 流程：单帧 HBAO（noisy）→ 时域累积（motion vector 重投影，与 TAA 同一套）→ 轻量 blur。
    { PROFILE_SCOPE("hbaoPass"); hbaoPass(view, geoProj); }
    {
        PROFILE_SCOPE("aoAccumulatePass");
        glm::mat4 invViewProj = glm::inverse(projection * view);  // 未抖动，与 TAA 一致
        aoAccumulatePass(invViewProj, m_prevViewProj);
    }
    { PROFILE_SCOPE("hbaoBlurPass"); hbaoBlurPass(); }

    // 阴影映射
    float sunShine_near, sunShine_far;
    glm::mat4 lightSpaceMatrix;
    {
        PROFILE_SCOPE("sunShineShadowMap");
        sunShineShadowMap(chunkManager, camera, sunShine_near, sunShine_far, lightSpaceMatrix);
    }

    // 3.5 阴影可见度（单帧 PCSS）+ 时域累积。
    //     - 可见度 pass 用 geoProj 重建世界位置（与写入 m_depthTexture 的投影一致）。
    //     - 累积 pass 的 motion vector 用未抖动 viewProj（与 TAA 同一套），prevViewProj
    //       是上一帧未抖动 viewProj。光源旋转导致的逐 shadow-texel 翻转在此被跨帧平滑。
    {
        PROFILE_SCOPE("shadowVisibilityPass");
        shadowVisibilityPass(view, geoProj, lightSpaceMatrix);
    }
    {
        PROFILE_SCOPE("shadowAccumulatePass");
        // 与 TAA 同一套：未抖动 viewProj 的逆做 motion vector（jitter < 1px，误差可忽略）
        glm::mat4 invViewProj = glm::inverse(projection * view);
        shadowAccumulatePass(invViewProj, m_prevViewProj);
    }

    // 4. 光照通道：计算结果到 lightingFBO（读取累积后的阴影可见度）
    //    深度由 geoProj（抖动）写入，故位置重建必须用同一 geoProj 的逆，否则世界坐标有偏移
    { PROFILE_SCOPE("lightingPass"); lightingPass(camera, view, geoProj, sunShine_near, sunShine_far, lightSpaceMatrix); }

    // 5. 将光照颜色复制到合成 FBO
    //    深度无需 blit：compositeFBO 直接共享 G-Buffer 的深度纹理
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_lightingFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_compositeFBO);
    glBlitFramebuffer(0, 0, m_screenWidth, m_screenHeight,
        0, 0, m_screenWidth, m_screenHeight,
        GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // 6. 绑定合成 FBO，开始正向渲染
    //    此时深度缓冲已包含 G-Buffer 中的场景深度（共享纹理）
    glBindFramebuffer(GL_FRAMEBUFFER, m_compositeFBO);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);


    // 6.1 渲染模型（不透明，写入深度）
    // 用 geoProj（与 G-Buffer 一致的抖动投影），否则模型与场景投影不匹配
    renderModel(camera, view, geoProj, player);  // 内部已开启深度测试和深度写入
    //renderModel_test(camera, view, projection);

    // 远程玩家模型
    if (netManager) {
        PROFILE_SCOPE("render.remotePlayers");
        renderRemotePlayers(netManager, view, geoProj, camera);
    }

    // 6.2 渲染粒子（半透明，深度测试开启，深度写入关闭）
    // 确保粒子系统内部已设置 glDepthMask(GL_FALSE)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_particleManager.render(view, geoProj);
    glDisable(GL_BLEND);

    //// 6.3 渲染边框（3D 几何，吃 jitter 才与场景对齐）
    if (m_hasSelectedBlock) {
        renderOutlines(view, geoProj);
    }

    // 7. TAA resolve：对当前帧合成色（含场景+模型+粒子+选中框，不含 UI）做时域累积。
    //    结果写入 m_taaHistory[m_taaCurrIdx]，既是显示源也是下一帧的历史。
    glm::mat4 currViewProj = projection * view;          // 未抖动，用于 motion vector
    if (m_taaEnabled) {
        glm::mat4 invViewProj = glm::inverse(currViewProj);
        { PROFILE_SCOPE("taaResolvePass"); taaResolvePass(invViewProj, m_prevViewProj); }

        // 8. 把 TAA 结果复制到默认帧缓冲
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_taaFBO[m_taaCurrIdx]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, m_screenWidth, m_screenHeight,
            0, 0, m_screenWidth, m_screenHeight,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);
    } else {
        // TAA 关闭：直接把合成色上屏
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_compositeFBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, m_screenWidth, m_screenHeight,
            0, 0, m_screenWidth, m_screenHeight,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }

    // 9. UI 在 TAA 之后、直接画到默认帧缓冲——绝不让准星/物品栏被时域累积模糊或拖影
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glDisable(GL_DEPTH_TEST);
    renderUI();
    glEnable(GL_DEPTH_TEST);

    // 10. 帧末更新 TAA 时域状态
    m_prevViewProj   = currViewProj;     // 下一帧 motion vector 用本帧未抖动 viewProj
    m_taaHistoryValid = true;            // 本帧已写入历史，下一帧可用
    m_taaCurrIdx     ^= 1;               // ping-pong 翻转

    // 阴影时域累积状态：本帧累积已写入 m_shadowAccum[curr]（lightingPass 已读取），
    // 翻转后下一帧把它当历史。先标 valid 再翻转。
    m_shadowAccumValid   = true;
    m_shadowAccumCurrIdx ^= 1;

    // AO 时域累积状态：同理（本帧累积已被 hbaoBlurPass 读取）
    m_aoAccumValid   = true;
    m_aoAccumCurrIdx ^= 1;

    m_frameIndex++;

    // 解绑
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderSystem::renderOutlines(const glm::mat4& view, const glm::mat4& projection) {
    // 遮挡交由 compositeFBO 的深度附件做硬件深度测试，无需再传入深度纹理。
    m_outlineRenderer.updateTime(m_currentTime);
    m_outlineRenderer.render(m_selectedBlockPos, view, projection, m_currentTime);
}

void RenderSystem::RenderQuad()
{
    glBindVertexArray(m_screenQuadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

void RenderSystem::geometryPass(const ChunkManager& chunkManager,
    const glm::mat4& view,
    const glm::mat4& projection) {
    // 绑定G-Buffer
    glBindFramebuffer(GL_FRAMEBUFFER, m_gBuffer);
    glViewport(0, 0, m_screenWidth, m_screenHeight);

    // 生产者自保：本 pass 是整帧的深度生产者。深度 clear 受 glDepthMask 门控，
    // 深度 write 受 GL_DEPTH_TEST 门控 —— 必须在 clear 前显式开好，绝不假设上游留对了。
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // 清除缓冲区
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 启用面剔除
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    // arena 可能因扩容换了底层 VBO，每帧重绑（内部已做相等短路）
    m_blockRenderer.bindArenaVBO(chunkManager.getArenaVBO());

    // section base SSBO 绑到 binding=0，shader 用 gl_DrawID 索引还原方块世界坐标
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, chunkManager.getSectionBaseSSBO());

    const auto& cmds = chunkManager.getDrawCommands();
    if (!cmds.empty()) {
        m_blockRenderer.render(chunkManager.getIndirectBuffer(), (int)cmds.size(), view, projection);
        m_drawCalls++;     // 一次 MDI 算一次 draw call
        m_totalInstances += chunkManager.getVisibleInstanceCount();
    }
    // 解绑FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// HBAO pass：单帧地平线角 AO → m_hbaoColorBuffer。蓝噪声抖动方向 + 帧序号时域去相关，
// 单帧少方向/少步数（噪声大），由 aoAccumulatePass 跨帧累积降噪。
void RenderSystem::hbaoPass(const glm::mat4& view, const glm::mat4& projection)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_hbaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    m_hbaoShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);   // 深度纹理，重建视图空间位置
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);   // 兼容保留
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_blueNoiseTex);   // HBAO 方向抖动源

    m_hbaoShader.setInt("gDepth", 0);
    m_hbaoShader.setInt("gNormal", 1);
    m_hbaoShader.setInt("texNoise", 2);
    m_hbaoShader.setInt("blueNoiseTex", 3);
    m_hbaoShader.setMat4("projection", projection);
    m_hbaoShader.setMat4("view", view);
    m_hbaoShader.setMat4("invProjection", glm::inverse(projection));
    m_hbaoShader.setVec2("screenSize", glm::vec2(m_screenWidth, m_screenHeight));

    const RuntimeConfig& rc = RuntimeConfig::get();
    m_hbaoShader.setInt("blueNoiseSize", m_blueNoiseSize);
    m_hbaoShader.setInt("frameIndex", (int)m_frameIndex);
    m_hbaoShader.setInt("uDirections", rc.aoDirections);
    m_hbaoShader.setInt("uSteps", rc.aoSteps);
    m_hbaoShader.setFloat("uRadius", rc.aoRadius);
    m_hbaoShader.setFloat("uIntensity", rc.aoIntensity);
    m_hbaoShader.setFloat("uBias", rc.aoBias);

    RenderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// AO 时域累积：重投影上一帧累积 + 邻域 clamp + 混合 → m_aoAccum[curr]。
void RenderSystem::aoAccumulatePass(const glm::mat4& invViewProj, const glm::mat4& prevViewProj)
{
    int curr = m_aoAccumCurrIdx;
    int prev = 1 - curr;

    glBindFramebuffer(GL_FRAMEBUFFER, m_aoAccumFBO[curr]);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_aoAccumShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_hbaoColorBuffer);   // 当前帧单帧 HBAO
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_aoAccum[prev]);     // 历史
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);

    m_aoAccumShader.setInt("aoCurr", 0);
    m_aoAccumShader.setInt("aoHist", 1);
    m_aoAccumShader.setInt("depthTex", 2);
    m_aoAccumShader.setMat4("invViewProj", invViewProj);
    m_aoAccumShader.setMat4("prevViewProj", prevViewProj);
    m_aoAccumShader.setVec2("screenSize", glm::vec2(m_screenWidth, m_screenHeight));
    m_aoAccumShader.setInt("frameIndex", m_aoAccumValid ? (int)m_frameIndex : 0);

    RenderQuad();
    // 关键：恢复深度测试。否则状态泄漏到随后的 sunShineShadowMap —— 深度测试关闭时
    // OpenGL 不写深度缓冲（glDepthMask 不足以单独开启写入），阴影贴图渲染将得到一张
    // 仍为清空值(1.0)的空深度图，PCSS 永远找不到 blocker，退化成"只有亮暗、没有阴影"。
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 累积后的 AO 再过一遍轻量空间 blur → m_hbaoColorBufferBlur（清理时域累积的残留颗粒）。
void RenderSystem::hbaoBlurPass()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_hbaoBlurFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    m_hbaoBlurShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_aoAccum[m_aoAccumCurrIdx]);  // 累积后的 AO
    RenderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// TAA resolve：把当前帧合成色（m_compositeColor）与历史累积混合，结果写入
// m_taaHistory[m_taaCurrIdx]（既作本帧显示源，也作下一帧的历史）。
void RenderSystem::taaResolvePass(const glm::mat4& invViewProj, const glm::mat4& prevViewProj)
{
    GLuint dst = m_taaHistory[m_taaCurrIdx];
    GLuint src = m_taaHistory[1 - m_taaCurrIdx];   // 上一帧历史

    glBindFramebuffer(GL_FRAMEBUFFER, m_taaFBO[m_taaCurrIdx]);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glDisable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT);

    m_taaShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_compositeColor);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, src);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);

    m_taaShader.setMat4("invViewProj", invViewProj);
    m_taaShader.setMat4("prevViewProj", prevViewProj);
    m_taaShader.setVec2("screenSize", glm::vec2(m_screenWidth, m_screenHeight));
    // 历史无效（首帧 / resize 后）时强制纯当前帧
    m_taaShader.setInt("frameIndex", m_taaHistoryValid ? (int)m_frameIndex : 0);

    RenderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    (void)dst;
}

void RenderSystem::sunShineShadowMap(const ChunkManager& chunkManager, const std::shared_ptr<Camera>camera
    , float& sunShine_near, float& sunShine_far, glm::mat4& lightSpaceMatrix)
{
    // 夜晚（阳光强度为 0）：冻结上一帧的阴影矩阵，不再重建。
    // 避免光方向接近水平时 lookAt 退化、阴影贴图乱跳；且夜晚着色器不使用阴影，
    // 首次进入夜晚若无缓存则跳过阴影渲染。
    if (m_sunIntensity <= 0.001f && m_hasCachedLightMatrix) {
        lightSpaceMatrix = m_cachedLightSpaceMatrix;
        sunShine_near    = m_cachedSunNear;
        sunShine_far     = m_cachedSunFar;
        return;
    }

    const float radius = MAX_SHADOW_DISTANCE;
    glm::vec3 center = camera->Position;

    // 光源视图：看向 center
    glm::vec3 lightDirNorm = glm::normalize(lightDir);
    // 近平面留 radius 余量，远平面 2*radius，保证 center 附近各方向都能命中
    glm::vec3 lightEye = center - lightDirNorm * (radius + 50.0f);
    glm::mat4 lightView = glm::lookAt(lightEye, center, glm::vec3(0.0f, 1.0f, 0.0f));

    // ---- snap-to-texel ----
    // 只有 center 跨过纹素边界时才会跳一格
    {
        const float worldUnitsPerTexel = (2.0f * radius) / float(SHADOW_WIDTH);
        glm::vec4 centerLS = lightView * glm::vec4(center, 1.0f);
        // 阶段 2：snap Z 也对齐到纹素整数倍，减少光源/相机移动时深度方向的量化跳变
        glm::vec3 snappedLS = glm::vec3(
            std::floor(centerLS.x / worldUnitsPerTexel) * worldUnitsPerTexel,
            std::floor(centerLS.y / worldUnitsPerTexel) * worldUnitsPerTexel,
            std::floor(centerLS.z / worldUnitsPerTexel) * worldUnitsPerTexel
        );
        // 把 snappedLS 变回世界空间，作为新的 center
        glm::mat4 invLightView = glm::inverse(lightView);
        glm::vec4 snappedWS = invLightView * glm::vec4(snappedLS, 1.0f);
        glm::vec3 newCenter = glm::vec3(snappedWS);

        // 重建 lightView：对齐后的视图矩阵把 newCenter 映射到纹素整数位置
        lightEye = newCenter - lightDirNorm * (radius + 50.0f);
        lightView = glm::lookAt(lightEye, newCenter, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // 正交框固定为 [-radius, radius]
    float left = -radius, right = radius;
    float bottom = -radius, top = radius;
    float nearP = 0.0f;
    float farP = 2.0f * radius + 100.0f;

    glm::mat4 lightProjection = glm::ortho(left, right, bottom, top, nearP, farP);

    // 与 shader 约定：currentDepth 使用 projCoords.z（正交投影下即 [0,1] 线性），
    // 这里的 sunShine_near/far 仅用于 PCSS 的世界尺度换算，不再用于 LinearizeDepth。
    sunShine_near = nearP;
    sunShine_far  = farP;
    lightSpaceMatrix = lightProjection * lightView;


    // 渲染场景到深度贴图（仅深度，无颜色附件）
    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);
    // 生产者自保：深度图渲染必须 GL_DEPTH_TEST + glDepthMask 都开才会写深度。
    // 显式开启，防止上游全屏 pass 泄漏的关闭状态让阴影贴图渲成空图
    // （这正是 AO 重构曾引入的 regression 根因 —— 详见 CLAUDE.md「GL 状态约定」）。
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glCullFace(GL_BACK);
    m_blockRenderer.bindArenaVBO(chunkManager.getArenaVBO());
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, chunkManager.getSectionBaseSSBO());
    const auto& cmds = chunkManager.getDrawCommands();
    if (!cmds.empty()) {
        m_blockRenderer.renderDepth(chunkManager.getIndirectBuffer(), (int)cmds.size(),
            lightSpaceMatrix, nearP, farP);
        m_drawCalls++;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_screenWidth, m_screenHeight);

    // 阶段 2：普通深度 + 手动 PCF，不再需要每帧 glGenerateMipmap（省一笔开销）

    // 缓存当前阴影矩阵，供夜晚冻结使用
    m_cachedLightSpaceMatrix = lightSpaceMatrix;
    m_cachedSunNear = sunShine_near;
    m_cachedSunFar  = sunShine_far;
    m_hasCachedLightMatrix = true;
}

// 单帧 PCSS 可见度 → m_shadowVisCurr（R8，1=受光，0=阴影）。蓝噪声 + 帧序号抖动，
// 单帧噪声大，由 shadowAccumulatePass 跨帧累积降噪。
void RenderSystem::shadowVisibilityPass(const glm::mat4& view, const glm::mat4& projection,
    const glm::mat4& lightSpaceMatrix)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowVisFBO);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_shadowVisShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_depthMap);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_blueNoiseTex);

    m_shadowVisShader.setInt("gDepth", 0);
    m_shadowVisShader.setInt("gNormal", 1);
    m_shadowVisShader.setInt("shadowMap", 2);
    m_shadowVisShader.setInt("blueNoiseTex", 3);

    m_shadowVisShader.setMat4("invProjection", glm::inverse(projection));
    m_shadowVisShader.setMat4("invView", glm::inverse(view));
    m_shadowVisShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
    m_shadowVisShader.setVec3("sunShineDir", lightDir);
    m_shadowVisShader.setFloat("sunShineIntensity", m_sunIntensity);

    const RuntimeConfig& rc = RuntimeConfig::get();
    m_shadowVisShader.setInt("blueNoiseSize", m_blueNoiseSize);
    m_shadowVisShader.setInt("frameIndex", (int)m_frameIndex);
    m_shadowVisShader.setInt("uBlockerSamples", rc.shadowBlockerSamples);
    m_shadowVisShader.setInt("uFilterSamples", rc.shadowFilterSamples);
    m_shadowVisShader.setFloat("uLightSizeUV", rc.shadowLightSize);

    RenderQuad();
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// 阴影时域累积：重投影上一帧累积 + 邻域 clamp + 混合 → m_shadowAccum[curr]。
void RenderSystem::shadowAccumulatePass(const glm::mat4& invViewProj, const glm::mat4& prevViewProj)
{
    int curr = m_shadowAccumCurrIdx;
    int prev = 1 - curr;

    glBindFramebuffer(GL_FRAMEBUFFER, m_shadowAccumFBO[curr]);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    m_shadowAccumShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_shadowVisCurr);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_shadowAccum[prev]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);

    m_shadowAccumShader.setInt("shadowVisCurr", 0);
    m_shadowAccumShader.setInt("shadowVisHist", 1);
    m_shadowAccumShader.setInt("depthTex", 2);
    m_shadowAccumShader.setMat4("invViewProj", invViewProj);
    m_shadowAccumShader.setMat4("prevViewProj", prevViewProj);
    m_shadowAccumShader.setVec2("screenSize", glm::vec2(m_screenWidth, m_screenHeight));
    m_shadowAccumShader.setInt("frameIndex", m_shadowAccumValid ? (int)m_frameIndex : 0);

    RenderQuad();
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}



void RenderSystem::lightingPass(const std::shared_ptr<Camera>camera
    , const glm::mat4& view, const glm::mat4& projection
    , float sunShine_near, float sunShine_far, glm::mat4& lightSpaceMatrix) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_lightingFBO);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST); // 全屏四边形不需要深度

    m_deferredLightingShader.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);   // 深度纹理，重建世界空间位置
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_gProperties);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_hbaoColorBufferBlur);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_shadowAccum[m_shadowAccumCurrIdx]); // 时域累积后的阴影可见度

    // 从深度重建位置所需的逆矩阵
    m_deferredLightingShader.setMat4("invProjection", glm::inverse(projection));
    m_deferredLightingShader.setMat4("invView", glm::inverse(view));

    m_deferredLightingShader.setVec3("uViewPos", camera->Position);
    m_deferredLightingShader.setVec3("sunShinePos", lightPos);
    m_deferredLightingShader.setVec3("sunShineDir", lightDir);

    // ---- 光照能量分配（环境光 + 阳光两份，按昼夜分配，参考原版 MC）----
    // 把直接光照拆成「始终保留的环境底光」+「随日照强度平滑增减的阳光」两份。
    // 环境光份额夜晚/清晨大、正午小 → 未受阳光直射处也够亮；阳光份额夜晚=0、地平线平滑过渡。
    const RuntimeConfig& rc = RuntimeConfig::get();

    // 环境光权重：白天/夜晚两个底光级别按日照强度插值（都是「占比」，再乘总预算）
    float ambientWeight = rc.lightBudget * glm::mix(rc.ambientNight, rc.ambientDay, m_sunIntensity);
    // 阳光权重：正午满功率 sunStrength，乘 sunIntensity 平滑昼夜 + 地平线过渡（夜晚=0）
    float sunWeight     = rc.lightBudget * rc.sunStrength * m_sunIntensity;

    // 环境光颜色：白天近中性、夜晚偏冷蓝（色调随昼夜插值，亮度由 ambientWeight 控）
    glm::vec3 dayAmbientColor   = glm::vec3(1.00f, 1.00f, 1.00f);
    glm::vec3 nightAmbientColor = glm::vec3(0.64f, 0.73f, 1.00f); // 冷蓝调
    glm::vec3 ambientColor = glm::mix(nightAmbientColor, dayAmbientColor, m_sunIntensity);
    m_deferredLightingShader.setVec3("sunShineAmbient", ambientColor * ambientWeight);

    // 阳光颜色：按色温（m_sunDiffuseColor 已在 move_DirLight 计算），亮度由 sunWeight 控
    m_deferredLightingShader.setVec3("sunShineDiffuse", m_sunDiffuseColor * sunWeight);
    // 注：sunIntensity 已折进 sunWeight，shader 内不再二次乘，避免重复衰减
    m_deferredLightingShader.setFloat("sunShineIntensity", 1.0f);

    RenderQuad();

    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderSystem::renderModel(const std::shared_ptr<Camera> camera,
    const glm::mat4& view, const glm::mat4& projection, Player* player)
{
    if (!player) return;
    PlayerModel* model = player->getModel();
    if (!model) return;
    // 第一人称时不渲染自己的模型（避免从头部内部看到贴图）
    if (!player->shouldRenderOwnModel()) return;

    // 渲染模型（正向渲染到合成FBO，保留已有的颜色和深度）
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);          // 确保写入深度
    glEnable(GL_CULL_FACE);

    // 清除延迟渲染残留的纹理绑定，避免模型着色器采样到G-Buffer纹理
    for (int i = 0; i < 6; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    m_modeShader.use();
    m_modeShader.setMat4("projection", projection);
    m_modeShader.setMat4("view", view);
    m_modeShader.setVec3("viewPos", camera->Position);
    m_modeShader.setVec3("light.direction", lightDir);

    // 使用动画姿态绘制
    model->drawPosed(m_modeShader, player->getModelFootPosition(), player->getPose());
}

void RenderSystem::renderRemotePlayers(NetManager* netManager,
    const glm::mat4& view, const glm::mat4& projection,
    const std::shared_ptr<Camera>& camera)
{
    if (!netManager || !netManager->isConnected()) return;

    const auto& players = netManager->getPlayers();
    uint16_t localId = netManager->getLocalPlayerId();

    for (const auto& [id, player] : players) {
        if (id == localId) continue;  // 跳过本地玩家

        // 使用缓存的渲染位置（由 OnRep 回调或 join 消息设置）
        glm::vec3 pos = player->getRenderPosition();
        // 跳过尚未收到位置同步的玩家
        if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) continue;
        float yawDeg = player->getRenderYaw();

        // 身体朝向：Camera.Yaw → bodyYaw 转换（与 PlayerAnimator 一致）
        float bodyYaw = glm::half_pi<float>() - glm::radians(yawDeg);

        // 脚底位置：碰撞箱中心减半高
        glm::vec3 footPos = pos;
        footPos.y -= PhysicsConstants::PLAYER_HEIGHT_STANDING * 0.5f;

        // 简单站立姿态
        PlayerPose idlePose{};
        idlePose.bodyYaw = bodyYaw;

        // 绑定皮肤纹理 + 绘制模型
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);

        for (int i = 0; i < 6; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);

        m_modeShader.use();
        m_modeShader.setMat4("projection", projection);
        m_modeShader.setMat4("view", view);
        m_modeShader.setVec3("viewPos", camera->Position);
        m_modeShader.setVec3("light.direction", lightDir);
        m_modeShader.setVec3("light.ambient", 0.4f, 0.4f, 0.4f);
        m_modeShader.setVec3("light.diffuse", 0.9f, 0.9f, 0.9f);
        m_modeShader.setVec3("light.specular", 1.0f, 1.0f, 1.0f);
        m_modeShader.setFloat("shininess", 256.0f);

        // 查找并绑定该玩家的皮肤纹理
        GLuint skinTex = 0;
        auto it = m_skinTextures.find(player->skinName);
        if (it != m_skinTextures.end()) skinTex = it->second;
        if (skinTex && skinTex != m_remotePlayerModel.getTextureID()) {
            m_remotePlayerModel.drawPosed(m_modeShader, footPos, idlePose, skinTex);
        } else {
            m_remotePlayerModel.drawPosed(m_modeShader, footPos, idlePose);
        }
    }
}

GLuint RenderSystem::loadSkinTexture(const std::string& skinName) {
    auto it = m_skinTextures.find(skinName);
    if (it != m_skinTextures.end()) return it->second;

    std::string path = SkinManager::instance().getSkinPath(skinName);
    if (path.empty()) return 0;

    int width, height, channels;
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) return 0;

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    m_skinTextures[skinName] = tex;
    return tex;
}

void RenderSystem::loadAllSkinTextures() {
    for (const auto& name : SkinManager::instance().getSkinNames()) {
        loadSkinTexture(name);
    }
}

void RenderSystem::renderModel_test(const std::shared_ptr<Camera> camera,
    const glm::mat4& view, const glm::mat4& projection)
{
    // 渲染模型（正向渲染到合成FBO，保留已有的颜色和深度）
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);          // 确保写入深度
    glEnable(GL_CULL_FACE);

    // 清除延迟渲染残留的纹理绑定，避免模型着色器采样到G-Buffer纹理
    for (int i = 0; i < 6; i++) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    m_modeShader.use();
    m_modeShader.setMat4("projection", projection);
    m_modeShader.setMat4("view", view);
    m_modeShader.setVec3("viewPos", camera->Position);
    m_modeShader.setVec3("light.direction", lightDir);

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, glm::vec3(2.0f, 43.5f, -3.0f));
    model = glm::scale(model, glm::vec3(1.1f, 1.1f, 1.1f));
    //model = glm::rotate(model, glm::radians((float)glfwGetTime() * 50.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    m_modeShader.setMat4("model", model);

    spyglass.Draw(m_modeShader);

}