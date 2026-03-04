#include "RenderSystem.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include "../TextureMgr.h"
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
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::mat4) * 10000, NULL, GL_DYNAMIC_DRAW);

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

    // 配置边框
    BlockOutlineRenderer::OutlineConfig outlineConfig;
    outlineConfig.color = glm::vec3(0.0f, 0.0f, 0.0f);  // 黑色边框
    outlineConfig.lineWidth = 2.5f;
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
    m_ssaoShader.setVec2("uScreenSize", glm::vec2(m_screenWidth, m_screenHeight));

    m_ssaoBlurShader.use();
    m_ssaoBlurShader.setInt("ssaoInput", 0);
    std::cout << "RenderSystem initialized successfully!" << std::endl;



    // 配置深度贴图FBO
    glGenFramebuffers(1, &m_depthMapFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);
    // 创建深度纹理
    glGenTextures(1, &m_depthMap);
    glBindTexture(GL_TEXTURE_2D, m_depthMap);

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





    return true;
}


// 添加新的initUI方法
void RenderSystem::initUI() {
    m_uiManager.initialize(m_screenWidth, m_screenHeight);

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
}
// 添加UI渲染方法
void RenderSystem::renderUI() {
    // 注意：需要在主渲染循环中更新UI时间
    static float uiTime = 0.0f;
    uiTime += 0.016f; // 假设60FPS

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

    // 创建深度缓冲区
    glGenRenderbuffers(1, &m_rboDepth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_rboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, m_screenWidth, m_screenHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_rboDepth);

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
    if (m_rboDepth) glDeleteRenderbuffers(1, &m_rboDepth);

    if (m_gPosition) glDeleteTextures(1, &m_gPosition);
    if (m_gNormal) glDeleteTextures(1, &m_gNormal);
    if (m_gAlbedo) glDeleteTextures(1, &m_gAlbedo);
    if (m_gProperties) glDeleteTextures(1, &m_gProperties);

    m_gBuffer = 0;
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
    float deltaTime
) {
    //move_DirLight(deltaTime); // 禁用光源旋转以保持阴影稳定

    m_drawCalls = 0;
    m_totalInstances = 0;

    // 几何Pass：填充G-Buffer
    geometryPass(chunkManager, view, projection);

    // SSAO Pass
    ssaoPass(view, projection);
    // SSAO模糊Pass
    ssaoBlurPass();

    // 全局平行光阴影贴图Pass
    float sunShine_near = 0, sunShine_far = 0;
    glm::mat4 lightSpaceMatrix;
    sunShineShadowMap(chunkManager, camera, sunShine_near, sunShine_far, lightSpaceMatrix);

    // 光照Pass：计算光照
    lightingPass(camera, sunShine_near, sunShine_far, lightSpaceMatrix);

    // 渲染选中的方块边框 TODOs
    if (m_hasSelectedBlock) {
        renderOutlines(view, projection);
    }

    renderUI();
}

// 新增：渲染边框函数
void RenderSystem::renderOutlines(const glm::mat4& view, const glm::mat4& projection) {
    // 更新边框渲染器的时间
    m_outlineRenderer.updateTime(m_currentTime);

    // 渲染选中方块的边框
    m_outlineRenderer.render(m_selectedBlockPos, view, projection, m_currentTime);
}


void printMat4(const glm::mat4& mat) {
    for (int row = 0; row < 4; ++row) {
        std::cout << "[";
        for (int col = 0; col < 4; ++col) {
            std::cout << mat[col][row];
            if (col < 3) std::cout << ", ";
        }
        std::cout << "]\n";
    }
}
void printt_RenderData(const std::unordered_map<BlockFaceType, std::vector<glm::mat4>>& a) {
    for (auto& b : a) {
        std::cout << "BlockFaceType: " << static_cast<int>(b.first.type) << ", Face ID: " << static_cast<int>(b.first.face_id) << ", Matrices count: " << b.second.size() << std::endl;
        int cnt = 0;
        for (auto& c : b.second) {
            printMat4(c);
            std::cout << "----\n";
            cnt++;
            if (cnt > 10)break;
        }
    }
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
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glCullFace(GL_BACK); // 使用背面剔除减少阴影痤疮

    const auto& renderData = chunkManager.getRenderData();
    if (!renderData.empty()) {
        m_blockRenderer.renderDepth(renderData, lightSpaceMatrix, -maxLight.z, -minLight.z);
        m_drawCalls++;
        m_totalInstances += renderData.size();
    }

    glCullFace(GL_BACK); // 恢复默认的背面剔除
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, m_screenWidth, m_screenHeight);

    //glBindTexture(GL_TEXTURE_2D, m_depthMap);
    //glGenerateMipmap(GL_TEXTURE_2D);
}



void RenderSystem::lightingPass(const std::shared_ptr<Camera>camera
    , float sunShine_near, float sunShine_far, glm::mat4& lightSpaceMatrix) {
    // 清除屏幕
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 禁用深度测试，因为我们要渲染全屏四边形
    glDisable(GL_DEPTH_TEST);

    // 使用延迟光照着色器
    m_deferredLightingShader.use();

    // 绑定G-Buffer纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gPosition);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_gNormal);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_gAlbedo);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_gProperties);
    glActiveTexture(GL_TEXTURE4); // ssao阴影贴图
    glBindTexture(GL_TEXTURE_2D, m_ssaoColorBufferBlur);
    glActiveTexture(GL_TEXTURE5); // 平行光阴影贴图
    glBindTexture(GL_TEXTURE_2D, m_depthMap);

    // 设置光照参数  
    m_deferredLightingShader.setVec3("uViewPos", camera->Position);
    m_deferredLightingShader.setMat4("uView", camera->GetViewMatrix());
    m_deferredLightingShader.setMat4("projection", camera->GetViewProjectionMatrix());
    m_deferredLightingShader.setVec3("sunShinePos", lightPos);
    m_deferredLightingShader.setVec3("sunShineDir", lightDir);
    m_deferredLightingShader.setVec3("sunShineAmbient", 0.2f, 0.2f, 0.2f);
    m_deferredLightingShader.setVec3("sunShineDiffuse", 0.8f, 0.8f, 0.8f);
    m_deferredLightingShader.setFloat("sunShineSize", 5.0f);// 光源大小
    m_deferredLightingShader.setInt("SHADOW_WIDTH", SHADOW_WIDTH);
    m_deferredLightingShader.setFloat("sunShineFar", sunShine_far);
    m_deferredLightingShader.setFloat("sunShineNear", sunShine_near);
    m_deferredLightingShader.setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // 渲染屏幕四边形
    RenderQuad();

    // 重新启用深度测试
    glEnable(GL_DEPTH_TEST);
}