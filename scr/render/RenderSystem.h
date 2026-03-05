#pragma once
#include "../core.h"
#include "../chunk/ChunkManager.h"
#include "../chunk/BlockType.h"
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "../Shader.h"
#include "BlockOutlineRenderer.h"  // ����
#include "../collision/Ray.h"                   // ����
#include "../UI/UIManager.h"
// �����涥������
struct FaceVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
};

// ������Ⱦ��
class BlockRenderer {
public:
    BlockRenderer();
    ~BlockRenderer();

    // ��ʼ��
    bool initialize();

    // ��Ⱦ��������
    void render(const std::vector<InstanceData>& instanceMatrices,
        const glm::mat4& view, const glm::mat4& projection);

    // ���ɷ�����Ӱ����
    void renderDepth(const std::vector<InstanceData>& instanceData
        , const glm::mat4& lightSpaceMatrix, float near, float far);

    // ��ȡVAO
    GLuint getVAO() const { return VAO; }

    // ������������
    void setTextureArray(GLuint texArray) { m_textureArray = texArray; }

private:
    // ������λ���������
    void createFaceVertices();

    GLuint VAO;            // �����������
    GLuint VBO;            // ���㻺��������
    GLuint EBO;			// ��������������
    GLuint m_instanceDataVBO;    // ʵ�������󻺳���
    GLuint m_textureArray = 0;
    std::vector<unsigned int> m_indices; // ���������������
    std::vector<FaceVertex> m_vertices;


    // ��ɫ������
    Shader m_shader{ {
            { GL_VERTEX_SHADER,"shader/g_buffer.vert" },
            { GL_FRAGMENT_SHADER, "shader/g_buffer.frag" }
            } };

    // ȫ��ƽ�й�(����)��Ӱ��ͼ������ɫ��
    Shader m_depthShader{ {
        { GL_VERTEX_SHADER,"shader/shadow_mapping_depth.vert" },
        { GL_FRAGMENT_SHADER,  "shader/shadow_mapping_depth.frag" }
        } };

};

// ��Ⱦϵͳ
class RenderSystem {
public:
    RenderSystem(int screenWidth, int screenHeight);
    ~RenderSystem();

    // ��ʼ��
    bool initialize();

    // ��Ⱦһ֡
    void render(const ChunkManager& chunkManager,
        const glm::mat4& view,
        const glm::mat4& projection,
        std::shared_ptr<Camera> camera,
        float deltaTime);

    // ����ѡ�еķ��飨���ڱ߿���Ⱦ��
    void setSelectedBlock(const glm::ivec3& blockPos) {
        m_selectedBlockPos = blockPos;
        m_hasSelectedBlock = true;
    }

    void clearSelectedBlock() {
        m_hasSelectedBlock = false;
    }

    // ���ø�������
    void setOutlineConfig(const BlockOutlineRenderer::OutlineConfig& config) {
        m_outlineRenderer.setConfig(config);
    }

    // ���ù��ղ���
    void setLightDirection(const glm::vec3& direction) { m_lightDirection = direction; }
    void setLightColor(const glm::vec3& color) { m_lightColor = color; }
    void setLightIntensity(float intensity) { m_lightIntensity = intensity; }
    void setAmbientColor(const glm::vec3& color) { m_ambientColor = color; }

    // ��ȡ��Ⱦͳ��
    int getDrawCalls() const { return m_drawCalls; }
    int getTotalInstances() const { return m_totalInstances; }

    // UI��ط���
    void initUI();
    UIManager& getUIManager() { return UIManager::getInstance(); }
    // ������Ļ�ߴ繩UIʹ��
    void setScreenSize(int width, int height);

private:
    // ��Ļ�ߴ�
    int m_screenWidth;
    int m_screenHeight;

    // G-Buffer
    GLuint m_gBuffer;
    GLuint m_gPosition, m_gNormal, m_gAlbedo, m_gProperties;
    GLuint m_depthTexture = 0; // 深度纹理，用于边框遮挡

    GLuint m_ssaoFBO, m_ssaoBlurFBO;
    GLuint m_ssaoColorBuffer, m_ssaoColorBufferBlur;
    GLuint m_noiseTexture;// SSAO��������
    std::vector<glm::vec3> ssaoKernel;// SSAO������

    GLuint m_depthMapFBO; //  dirVarianceMap; VSSM��
    GLuint m_depthMap; // ȫ��ƽ�й���գ���Ӱ��ͼ

    glm::vec3 lightPos = { -1, 1.0f, -1.0f }; // ȫ��ƽ�й�(λ���������)
    glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, -1.0f, 1.0f)); // ȫ��ƽ�й�

    void move_DirLight(float deltaTime) {
        static float time_now = 0.0f;
        const float rotate_speed = 0.2f; // ��ת�ٶ�

        // �ۼ�ʱ�䣬������ת
        time_now += deltaTime;
        // ���㵱ǰ��ת�Ƕ�
        float angle = rotate_speed * time_now;
        lightPos.x = -100.0f * sin(angle);  // x/z����ͬ���仯��ʵ�֡�б��Գơ�
        lightPos.y = 100.0f * cos(angle);  // y���������·��򲨶�
        lightPos.z = -100.0f * sin(angle);

        // 2. ��Դ����ʼ��ָ��ԭ�㣨ԭ�� - ��Դλ�ã��ٹ�һ����
        lightDir = glm::normalize(glm::vec3(0.0f, 0.0f, 0.0f) - lightPos);
    }


    // ssao�������ڱ���ɫ��
    Shader m_ssaoShader{ {
        { GL_VERTEX_SHADER,"shader/ssao.vert" },
        { GL_FRAGMENT_SHADER, "shader/ssao.frag" }
        } };

    // ssaoģ����ɫ��
    Shader m_ssaoBlurShader{ {
        { GL_VERTEX_SHADER,"shader/ssao_blur.vert" },
        { GL_FRAGMENT_SHADER, "shader/ssao_blur.frag" }
        } };

    // �ӳٹ�����ɫ��
    Shader m_deferredLightingShader{ {
            { GL_VERTEX_SHADER,"shader/deferred_lighting.vert" },
            { GL_FRAGMENT_SHADER, "shader/deferred_lighting.frag" }
            } };



    // ������Ⱦ��
    BlockRenderer m_blockRenderer;

    // �߿���Ⱦ��
    BlockOutlineRenderer m_outlineRenderer;

    // UI������
    UIManager& m_uiManager = UIManager::getInstance();

    // ѡ�еķ���
    glm::ivec3 m_selectedBlockPos;
    bool m_hasSelectedBlock = false;
    float m_currentTime = 0.0f;


    // ȫ���ı���
    GLuint m_screenQuadVAO;
    GLuint m_screenQuadVBO;

    // ���ղ���
    glm::vec3 m_lightDirection;
    glm::vec3 m_lightColor;
    float m_lightIntensity;
    glm::vec3 m_ambientColor;

    // ��Ⱦͳ��
    int m_drawCalls;
    int m_totalInstances;

    // ˽�з���
    bool createGBuffer();
    void destroyGBuffer();
    void createScreenQuad();
    void createSampleUI();

    // ��Ⱦ
    // ȫ���ı���
    void RenderQuad();
    // ���ν׶�
    void geometryPass(const ChunkManager& chunkManager,
        const glm::mat4& view,
        const glm::mat4& projection);
    // ssao�׶�
    void ssaoPass(const glm::mat4& view, const glm::mat4& projection);
    // ssaoģ��
    void ssaoBlurPass();
    // ȫ��ƽ�й���Ӱ��ͼ����
    void sunShineShadowMap(const ChunkManager& chunkManager, const std::shared_ptr<Camera>camera
        , float& sunShine_near, float& sunShine_far, glm::mat4& lightSpaceMatrix);
    // ���ռ���
    void lightingPass(const std::shared_ptr<Camera>camera
        , float sunShine_near, float sunShine_far, glm::mat4& lightSpaceMatrix);
    // ��Ⱦ�߿�
    void renderOutlines(const glm::mat4& view, const glm::mat4& projection);
    // ��ȾUI
    void renderUI();
};