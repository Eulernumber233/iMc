#include "RenderSystem.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "../TextureMgr.h"
#include "../UI/UIHotbar.h"
#include "../Player.h"
#include "../mode/PlayerModel.h"
#include <random>
BlockRenderer::BlockRenderer()
    : VAO(0), VBO(0), m_instanceDataVBO(0), EBO(0) {

}

BlockRenderer::~BlockRenderer() {

}

bool BlockRenderer::initialize() {
    // 创建顶点数据
    createFaceVertices();

    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glGenBuffers(1, &m_instanceDataVBO);

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
    // 位置属性（location 0）
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(FaceVertex),
        (void*)offsetof(FaceVertex, position));
    glEnableVertexAttribArray(0);

    // 3. 法线属性（location 1）
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(FaceVertex),
        (void*)offsetof(FaceVertex, normal));
    glEnableVertexAttribArray(1);

    // 纹理坐标属性 (location 2)
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
        sizeof(FaceVertex), (void*)offsetof(FaceVertex, texCoord));
    glEnableVertexAttribArray(2);

    //glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(FaceVertex),
    //    (void*)offsetof(FaceVertex, tangent));
    //glEnableVertexAttribArray(3);

    //glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(FaceVertex),
    //    (void*)offsetof(FaceVertex, bitangent));
    //glEnableVertexAttribArray(4);


    glBindBuffer(GL_ARRAY_BUFFER, m_instanceDataVBO);
    // 预分配空间（假设最大10000个实例）
    glBufferData(GL_ARRAY_BUFFER, sizeof(InstanceData) * 10000, NULL, GL_DYNAMIC_DRAW);

    // 设置实例属性 (location 5-8)
    // 位置 (vec3)
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)offsetof(InstanceData, position));
    glVertexAttribDivisor(5, 1);

    // 面索引 (int)
    glEnableVertexAttribArray(6);
    glVertexAttribIPointer(6, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, faceIndex));
    glVertexAttribDivisor(6, 1);

    // 方块类型 (int)
    glEnableVertexAttribArray(7);
    glVertexAttribIPointer(7, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, blockType));
    glVertexAttribDivisor(7, 1);

    // 纹理层索引 (int)
    glEnableVertexAttribArray(8);
    glVertexAttribIPointer(8, 1, GL_INT, sizeof(InstanceData), (void*)offsetof(InstanceData, textureLayer));
    glVertexAttribDivisor(8, 1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);


    return true;
}
void BlockRenderer::render(
    const std::vector<InstanceData>& instanceData,
    const glm::mat4& view,
    const glm::mat4& projection
)
{
    if (instanceData.empty()) return;

    m_shader.use();
    m_shader.setMat4("uView", view);
    m_shader.setMat4("uProjection", projection);

    // 绑定纹理数组
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArray);
    m_shader.setInt("uTextureArray", 0);

    // 上传实例数据
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceDataVBO);
    glBufferData(GL_ARRAY_BUFFER, instanceData.size() * sizeof(InstanceData),
        instanceData.data(), GL_STREAM_DRAW);

    // 绘制
    glBindVertexArray(VAO);
    glDrawElementsInstanced(GL_TRIANGLES,
        static_cast<GLsizei>(m_indices.size()),
        GL_UNSIGNED_INT, 0,
        static_cast<GLsizei>(instanceData.size()));
    glBindVertexArray(0);
}

void BlockRenderer::renderDepth(const std::vector<InstanceData>& instanceData,
    const glm::mat4& lightSpaceMatrix,
    float near, float far) {
    if (instanceData.empty()) return;

    m_depthShader.use();
    m_depthShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);
    m_depthShader.setFloat("dir_near", near);
    m_depthShader.setFloat("dir_far", far);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_instanceDataVBO);
    glBufferData(GL_ARRAY_BUFFER, instanceData.size() * sizeof(InstanceData),
        instanceData.data(), GL_STREAM_DRAW);

    glDrawElementsInstanced(GL_TRIANGLES, m_indices.size(),
        GL_UNSIGNED_INT, 0, instanceData.size());
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

    if (m_screenQuadVAO) glDeleteVertexArrays(1, &m_screenQuadVAO);
    if (m_screenQuadVBO) glDeleteBuffers(1, &m_screenQuadVBO);
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

    // 玩家模型由 Player 自己持有和初始化，此处不再加载

    // 配置边框
    BlockOutlineRenderer::OutlineConfig outlineConfig;
    outlineConfig.color = glm::vec3(0.0f, 0.0f, 0.0f);  // 黑色边框
    outlineConfig.lineWidth = 4.0f;
    outlineConfig.pulseSpeed = 3.0f;
    outlineConfig.pulseIntensity = 0.15f;
    outlineConfig.enablePulse = true;
    outlineConfig.depthTest = false;  // 禁用深度测试，确保边框始终可见
    outlineConfig.outlineScale = 1.005f;  // 稍微放大
    m_outlineRenderer.setConfig(outlineConfig);


    // 创建G-Buffer
    if (!createGBuffer()) {
        std::cerr << "Failed to create G-Buffer!" << std::endl;
        return false;
    }

    // 创建全屏四边形
    createScreenQuad();

    m_deferredLightingShader.use();
    m_deferredLightingShader.setInt("gPosition", 0);
    m_deferredLightingShader.setInt("gNormal", 1);
    m_deferredLightingShader.setInt("gAlbedo", 2);
    m_deferredLightingShader.setInt("gProperties", 3);
    m_deferredLightingShader.setInt("ssao", 4);
    m_deferredLightingShader.setInt("varianceShadowMap", 5);

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

    // Noise texture (16x16 for better noise distribution)
    std::vector<glm::vec3> ssaoNoise;
    for (GLuint i = 0; i < 256; i++)  // 16x16 = 256 samples
    {
        glm::vec3 noise(randomFloats(generator) * 2.0 - 1.0, randomFloats(generator) * 2.0 - 1.0, 0.0f); // rotate around z-axis (in tangent space)
        ssaoNoise.push_back(noise);
    }
    glGenTextures(1, &m_noiseTexture);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 16, 16, 0, GL_RGB, GL_FLOAT, &ssaoNoise[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    m_ssaoShader.use();
    m_ssaoShader.setInt("gPositionDepth", 0);
    m_ssaoShader.setInt("gNormal", 1);
    m_ssaoShader.setInt("texNoise", 2);
    m_ssaoShader.setVec2("screenSize", glm::vec2(m_screenWidth, m_screenHeight));

    m_ssaoBlurShader.use();
    m_ssaoBlurShader.setInt("ssaoInput", 0);
    std::cout << "RenderSystem initialized successfully!" << std::endl;



    // 配置深度贴图FBO
    glGenFramebuffers(1, &m_depthMapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);
    // 创建深度纹理
    glGenTextures(1, &m_depthMap);
    glBindTexture(GL_TEXTURE_2D, m_depthMap);

    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
     // 格式：RG32F（R存m1=线性深度，G存m2=线性深度²）
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_RG, GL_FLOAT, NULL);
    // 线性过滤获取区域均值
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    // 将深度纹理附加到FBO的深度附件
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_depthMap, 0);

    GLuint varianceRBO;
    glGenRenderbuffers(1, &varianceRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, varianceRBO);
    // 深度附件用深度格式（GL_DEPTH_COMPONENT32F）
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, SHADOW_WIDTH, SHADOW_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, varianceRBO);

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

    // 创建物品栏
    auto hotbar = std::make_shared<UIHotbar>("hotbar", 10);

    // 设置布局参数
    UIHotbar::LayoutParams params;
    params.baseSlotSize = 40.0f;
    params.borderScaleNormal = 0.95f;
    params.borderScaleSelected = 0.80f;
    params.iconScale = 0.58f;
    params.iconOffsetX = 3.0f;
    params.iconOffsetY = -4.0f;
    params.slotSpacing = -13.0f;
    params.overallScale = 1.0f;
    hotbar->setLayoutParams(params);

    float slotSize = m_screenWidth * 0.06f; // 槽位大小为屏幕宽度的6%
    float hotbarWidth = slotSize * 10.0f;   // 10个槽位紧密排列
    float hotbarHeight = slotSize;          // 槽位为正方形
    hotbar->setSize(hotbarWidth, hotbarHeight);
    hotbar->setPosition(m_screenWidth * 0.5f, 30); // 底部居中，距离底部30像素
    hotbar->anchor = glm::vec2(0.5f, 0.0f); // 水平居中，垂直底部对齐
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

    // Also create framebuffer to hold SSAO processing stage 
    glGenFramebuffers(1, &m_ssaoFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);
    // - SSAO color buffer
    glGenTextures(1, &m_ssaoColorBuffer);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColorBuffer);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_screenWidth, m_screenHeight, 0, GL_RED, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoColorBuffer, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "SSAO Framebuffer not complete!" << std::endl;

    // - and blur stage
    glGenFramebuffers(1, &m_ssaoBlurFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);
    glGenTextures(1, &m_ssaoColorBufferBlur);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColorBufferBlur);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, m_screenWidth, m_screenHeight, 0, GL_RED, GL_FLOAT, nullptr);    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ssaoColorBufferBlur, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cout << "SSAO Blur Framebuffer not complete!" << std::endl;


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
    Player* player
) {
      //glBindFramebuffer(GL_FRAMEBUFFER, 0);
      //glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
      //glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      //renderModel(camera);
      //return;

    m_currentTime += deltaTime;

    // 更新光源位置
    //move_DirLight(deltaTime);

    // 更新粒子系统
    // 获取可见区块位置信息
    std::vector<glm::ivec2> visibleChunkPositions = chunkManager.getVisibleChunkPositions();

    m_particleManager.update(deltaTime, camera, visibleChunkPositions);

    // 几何通道：渲染方块到 G-Buffer
    geometryPass(chunkManager, view, projection);
    
    // SSAO 通道
    ssaoPass(view, projection);
    ssaoBlurPass();
    
    // 阴影映射
    float sunShine_near, sunShine_far;
    glm::mat4 lightSpaceMatrix;
    sunShineShadowMap(chunkManager, camera, sunShine_near, sunShine_far, lightSpaceMatrix);
    
    // 4. 光照通道：计算结果到 lightingFBO
    lightingPass(camera, sunShine_near, sunShine_far, lightSpaceMatrix);

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
    // 必须传入与 G-Buffer 一致的 view/projection，否则模型与场景投影不匹配
    // （Camera::GetProjectionMatrix() 的 AspectRatio 可能与窗口实际比例不同）
    renderModel(camera, view, projection, player);  // 内部已开启深度测试和深度写入
    //renderModel_test(camera, view, projection);

    // 6.2 渲染粒子（半透明，深度测试开启，深度写入关闭）
    // 确保粒子系统内部已设置 glDepthMask(GL_FALSE)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_particleManager.render(view, projection);
    glDisable(GL_BLEND);

    //// 6.3 渲染边框（根据配置，通常关闭深度测试）
    if (m_hasSelectedBlock) {
        renderOutlines(view, projection);
    }

    //// 6.4 渲染 UI（深度测试关闭）
    glDisable(GL_DEPTH_TEST);
    renderUI();
    glEnable(GL_DEPTH_TEST); // 恢复，以备后用

    // 7. 将合成 FBO 的颜色复制到默认帧缓冲
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_compositeFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, m_screenWidth, m_screenHeight,
        0, 0, m_screenWidth, m_screenHeight,
        GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // 解绑
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderSystem::renderOutlines(const glm::mat4& view, const glm::mat4& projection) {
    // 更新边框渲染器的时间
    m_outlineRenderer.updateTime(m_currentTime);

    // 设置深度纹理用于遮挡检测
    m_outlineRenderer.setDepthTexture(m_depthTexture);

    // 渲染选中方块的边框
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

    // 清除缓冲区
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 启用深度测试和面剔除
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    //printt_RenderData(renderData);
    // 获取渲染数据
    const auto& renderData = chunkManager.getRenderData();

    if (!renderData.empty()) {
        m_blockRenderer.render(renderData, view, projection);
        m_drawCalls++;
        m_totalInstances += renderData.size();
    }
    // 解绑FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderSystem::ssaoPass(const glm::mat4& view, const glm::mat4& projection)
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    m_ssaoShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
    // Send kernel + rotation 
    for (GLuint i = 0; i < 64; ++i)
        m_ssaoShader.setVec3("samples[" + std::to_string(i) + "]", ssaoKernel[i]);
    m_ssaoShader.setMat4("projection", projection);
    m_ssaoShader.setMat4("view", view);
    RenderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderSystem::ssaoBlurPass()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    m_ssaoBlurShader.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColorBuffer);
    RenderQuad();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderSystem::sunShineShadowMap(const ChunkManager& chunkManager, const std::shared_ptr<Camera>camera
    , float& sunShine_near, float& sunShine_far, glm::mat4& lightSpaceMatrix)
{
    // 计算光源空间变换矩阵
    glm::mat4 view = camera->GetViewMatrix();
    std::vector<glm::vec3> frustumCorners = camera->GetFrustumCornersWorldSpace(view, MAX_SHADOW_DISTANCE);
    glm::vec3 farLightPos = camera->Position - lightDir * 1000.0f;
    glm::mat4 lightView = glm::lookAt(farLightPos, farLightPos + lightDir, glm::vec3(0, 1, 0));
    glm::vec3 minLight = glm::vec3(FLT_MAX), maxLight = glm::vec3(-FLT_MAX);
    for (const auto& corner : frustumCorners) {
        glm::vec4 cornerLight = lightView * glm::vec4(corner, 1.0f);
        minLight = glm::min(minLight, glm::vec3(cornerLight));
        maxLight = glm::max(maxLight, glm::vec3(cornerLight));
    }

    // 扩展一些边界，避免阴影边缘过于锐利
    float expand = 2.0f;
    minLight -= glm::vec3(expand);
    maxLight += glm::vec3(expand);

    // 构建正交投影（光源空间 z 轴方向）
    glm::mat4 lightProjection = glm::ortho(minLight.x, maxLight.x,
        minLight.y, maxLight.y,
        -maxLight.z, -minLight.z);  // 近平面= -maxZ，远平面= -minZ
    sunShine_near = -maxLight.z;
    sunShine_far = -minLight.z;

    lightSpaceMatrix = lightProjection * lightView;

    // 渲染场景到深度贴图
    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(5.0f, 0.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glCullFace(GL_BACK); // 使用背面剔除减少阴影痤疮

    const auto& renderData = chunkManager.getRenderData();
    if (!renderData.empty()) {
        m_blockRenderer.renderDepth(renderData, lightSpaceMatrix, -maxLight.z, -minLight.z);
        m_drawCalls++;
        m_totalInstances += renderData.size();
    }

    glCullFace(GL_BACK); // 恢复默认的背面剔除
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_screenWidth, m_screenHeight);

    //glBindTexture(GL_TEXTURE_2D, m_depthMap);
   // glGenerateMipmap(GL_TEXTURE_2D);
}



void RenderSystem::lightingPass(const std::shared_ptr<Camera>camera
    , float sunShine_near, float sunShine_far, glm::mat4& lightSpaceMatrix) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_lightingFBO);
    glViewport(0, 0, m_screenWidth, m_screenHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST); // 全屏四边形不需要深度

    m_deferredLightingShader.use();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_gProperties);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, m_ssaoColorBufferBlur);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, m_depthMap);

    m_deferredLightingShader.setVec3("uViewPos", camera->Position);
    m_deferredLightingShader.setVec3("sunShinePos", lightPos);
    m_deferredLightingShader.setVec3("sunShineDir", lightDir);
    m_deferredLightingShader.setVec3("sunShineAmbient", 0.2f, 0.2f, 0.2f);
    m_deferredLightingShader.setVec3("sunShineDiffuse", 0.8f, 0.8f, 0.8f);
    m_deferredLightingShader.setFloat("sunShineSize", 5.0f);
    m_deferredLightingShader.setInt("SHADOW_WIDTH", SHADOW_WIDTH);
    m_deferredLightingShader.setFloat("sunShineFar", sunShine_far);
    m_deferredLightingShader.setFloat("sunShineNear", sunShine_near);
    m_deferredLightingShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

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