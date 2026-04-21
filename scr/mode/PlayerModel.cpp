#include "PlayerModel.h"
#include "../stb_image.h"
#include <iostream>

// Minecraft 皮肤纹理尺寸
static constexpr float TEX_W = 64.0f;
static constexpr float TEX_H = 64.0f;

// 1 像素 = 1/16 方块大小
static constexpr float PIXEL_SCALE = 1.0f / 16.0f;

PlayerModel::PlayerModel() {}

PlayerModel::~PlayerModel() {
    for (int i = 0; i < PART_COUNT; i++) {
        if (m_parts[i].VAO) glDeleteVertexArrays(1, &m_parts[i].VAO);
        if (m_parts[i].VBO) glDeleteBuffers(1, &m_parts[i].VBO);
        if (m_parts[i].EBO) glDeleteBuffers(1, &m_parts[i].EBO);
    }
    if (m_skinTexture) glDeleteTextures(1, &m_skinTexture);
}

bool PlayerModel::initialize(const std::string& skinPath) {
    // 加载皮肤纹理
    m_skinTexture = loadTexture(skinPath);
    if (m_skinTexture == 0) {
        std::cerr << "PlayerModel: Failed to load skin texture: " << skinPath << std::endl;
        return false;
    }

    // =====================================================
    // Minecraft Steve (wide) 模型几何定义
    // 所有尺寸单位：像素（1px = 1/16 方块）
    // 模型原点：脚底中心
    //
    // MC 皮肤 UV 展开约定（64x64 纹理）：
    // 一个 WxHxD 的长方体，UV 起点 (u0, v0) 展开为：
    //
    //          [top]    [bottom]
    //          D+u0     D+W+u0
    //   v0 -> +--------+--------+
    //         |  DxW   |  DxW   |
    //   D+v0->+---+----+---+----+
    //         |   | front|  | back |
    //         | L |  W  | R |  W  |
    //         | D |     | D |     |
    //  D+H+v0+---+-----+---+-----+
    //          u0  D    D+W D+W+D D+2W+2D (... 不一定全用)
    //
    // 面顺序: right(+X), left(-X), top(+Y), bottom(-Y), front(+Z), back(-Z)
    // UV 区域（相对于 uvOrigin）:
    //   top:    (D,    0,    D+W,  D)
    //   bottom: (D+W,  0,    D+2W, D)       -- 注意 MC bottom 是镜像的
    //   right:  (0,    D,    D,    D+H)
    //   front:  (D,    D,    D+W,  D+H)
    //   left:   (D+W,  D,    D+W+D,D+H)
    //   back:   (D+W+D,D,    D+2W+2D, D+H) -- 实际宽度=W（2D+2W总宽）
    // =====================================================

    // 部件定义: size(W,H,D), uvOrigin(u,v), origin, pivot
    // origin 是部件底部中心在模型坐标的位置（Y=0 在脚底）

    // 头 (Head): 8x8x8, UV 起点 (0, 0)
    // Y: 24(脖子)~32(头顶)。origin 放在头顶，向下生长到 24
    createBoxPart(HEAD,
        glm::vec3(8, 8, 8),       // size: 8宽 8高 8深
        glm::vec2(0, 0),          // UV origin
        glm::vec3(0, 32, 0),      // origin (头顶)
        glm::vec3(0, -4, 0));     // pivot (头部中心)

    // 身体 (Body): 8x12x4, UV 起点 (16, 16)
    // Y: 12(底) ~ 24(顶)
    createBoxPart(BODY,
        glm::vec3(8, 12, 4),
        glm::vec2(16, 16),
        glm::vec3(0, 24, 0),      // origin (身体顶部)
        glm::vec3(0, 0, 0));

    // 右臂 (Right Arm): 4x12x4, UV 起点 (40, 16)
    // 肩膀 X 轴心 = -6 (身体半宽4 + 手臂半宽2，紧贴不重叠)
    // 静止时手臂几何占 Y:[12,24]，与身体齐平；pivot y=22 仅用于动画摆动
    createBoxPart(RIGHT_ARM,
        glm::vec3(4, 12, 4),
        glm::vec2(40, 16),
        glm::vec3(-6, 24, 0),     // origin (肩膀顶部，与身体顶齐平)
        glm::vec3(0, -2, 0));     // pivot (肩膀 y=22，相对 origin 下移 2)

    // 左臂 (Left Arm): 4x12x4, UV 起点 (32, 48)
    createBoxPart(LEFT_ARM,
        glm::vec3(4, 12, 4),
        glm::vec2(32, 48),
        glm::vec3(6, 24, 0),
        glm::vec3(0, -2, 0));

    // 右腿 (Right Leg): 4x12x4, UV 起点 (0, 16)
    createBoxPart(RIGHT_LEG,
        glm::vec3(4, 12, 4),
        glm::vec2(0, 16),
        glm::vec3(-2, 12, 0),     // origin (胯部)
        glm::vec3(0, 0, 0));      // pivot (胯部)

    // 左腿 (Left Leg): 4x12x4, UV 起点 (16, 48)
    createBoxPart(LEFT_LEG,
        glm::vec3(4, 12, 4),
        glm::vec2(16, 48),
        glm::vec3(2, 12, 0),
        glm::vec3(0, 0, 0));

    std::cout << "PlayerModel initialized successfully!" << std::endl;
    return true;
}

void PlayerModel::createBoxPart(Part part, glm::vec3 size,
                                 glm::vec2 uvOrigin, glm::vec3 origin, glm::vec3 pivot) {
    float W = size.x;  // 宽度（X轴）
    float H = size.y;  // 高度（Y轴）
    float D = size.z;  // 深度（Z轴）

    // 像素转世界坐标
    float hw = W * 0.5f * PIXEL_SCALE;  // 半宽
    float hh = H * PIXEL_SCALE;          // 全高（从origin向下生长）
    float hd = D * 0.5f * PIXEL_SCALE;  // 半深

    // 长方体8个角点（origin 在部件顶部中心，向下生长）
    // 使用右手坐标系：+X右, +Y上, +Z前
    float x0 = -hw, x1 = hw;   // 左右
    float y0 = -hh, y1 = 0.0f; // 下上（从origin向下生长H高度）
    float z0 = -hd, z1 = hd;   // 后前

    // UV 坐标转换：像素 -> [0,1]
    float u0 = uvOrigin.x;
    float v0 = uvOrigin.y;

    std::vector<PlayerVertex> vertices;
    std::vector<unsigned int> indices;

    // === 6 个面 ===
    // 每个面的 UV 区域按 MC 皮肤展开约定

    // Right face (+X): UV区域 (u0, v0+D, u0+D, v0+D+H)
    addFace(vertices, indices,
        glm::vec3(x1, y0, z1),   // 右下前
        glm::vec3(x1, y0, z0),   // 右下后
        glm::vec3(x1, y1, z0),   // 右上后
        glm::vec3(x1, y1, z1),   // 右上前
        glm::vec3(1, 0, 0),
        u0 / TEX_W,         (v0 + D) / TEX_H,
        (u0 + D) / TEX_W,   (v0 + D + H) / TEX_H);

    // Front face (+Z): UV区域 (u0+D, v0+D, u0+D+W, v0+D+H)
    addFace(vertices, indices,
        glm::vec3(x0, y0, z1),   // 左下前
        glm::vec3(x1, y0, z1),   // 右下前
        glm::vec3(x1, y1, z1),   // 右上前
        glm::vec3(x0, y1, z1),   // 左上前
        glm::vec3(0, 0, 1),
        (u0 + D) / TEX_W,       (v0 + D) / TEX_H,
        (u0 + D + W) / TEX_W,   (v0 + D + H) / TEX_H);

    // Left face (-X): UV区域 (u0+D+W, v0+D, u0+D+W+D, v0+D+H)
    addFace(vertices, indices,
        glm::vec3(x0, y0, z0),   // 左下后
        glm::vec3(x0, y0, z1),   // 左下前
        glm::vec3(x0, y1, z1),   // 左上前
        glm::vec3(x0, y1, z0),   // 左上后
        glm::vec3(-1, 0, 0),
        (u0 + D + W) / TEX_W,       (v0 + D) / TEX_H,
        (u0 + D + W + D) / TEX_W,   (v0 + D + H) / TEX_H);

    // Back face (-Z): UV区域 (u0+D+W+D, v0+D, u0+2D+2W, v0+D+H)
    addFace(vertices, indices,
        glm::vec3(x1, y0, z0),   // 右下后
        glm::vec3(x0, y0, z0),   // 左下后
        glm::vec3(x0, y1, z0),   // 左上后
        glm::vec3(x1, y1, z0),   // 右上后
        glm::vec3(0, 0, -1),
        (u0 + D + W + D) / TEX_W,       (v0 + D) / TEX_H,
        (u0 + 2 * D + 2 * W) / TEX_W,   (v0 + D + H) / TEX_H);

    // Top face (+Y): UV区域 (u0+D, v0, u0+D+W, v0+D)
    addFace(vertices, indices,
        glm::vec3(x0, y1, z1),   // 左上前
        glm::vec3(x1, y1, z1),   // 右上前
        glm::vec3(x1, y1, z0),   // 右上后
        glm::vec3(x0, y1, z0),   // 左上后
        glm::vec3(0, 1, 0),
        (u0 + D) / TEX_W,       v0 / TEX_H,
        (u0 + D + W) / TEX_W,   (v0 + D) / TEX_H);

    // Bottom face (-Y): UV区域 (u0+D+W, v0, u0+D+2W, v0+D)
    // MC 皮肤约定：底面 UV 存储时垂直翻转，故 v0 对应前边、v0+D 对应后边
    // 保持与其他面一致的顶点顺序（左下后、右下后、右下前、左下前），
    // 仅对 UV v 方向反向映射来实现翻转
    addFace(vertices, indices,
        glm::vec3(x0, y0, z0),   // 左下后
        glm::vec3(x1, y0, z0),   // 右下后
        glm::vec3(x1, y0, z1),   // 右下前
        glm::vec3(x0, y0, z1),   // 左下前
        glm::vec3(0, -1, 0),
        (u0 + D + W) / TEX_W,       (v0 + D) / TEX_H,
        (u0 + D + 2 * W) / TEX_W,   v0 / TEX_H);

    // 存储部件信息
    m_parts[part].origin = origin * PIXEL_SCALE;
    m_parts[part].pivot = pivot * PIXEL_SCALE;
    m_parts[part].indexCount = static_cast<unsigned int>(indices.size());

    // 创建 VAO/VBO/EBO
    glGenVertexArrays(1, &m_parts[part].VAO);
    glGenBuffers(1, &m_parts[part].VBO);
    glGenBuffers(1, &m_parts[part].EBO);

    glBindVertexArray(m_parts[part].VAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_parts[part].VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(PlayerVertex),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_parts[part].EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
                 indices.data(), GL_STATIC_DRAW);

    // 顶点属性布局（与 mode.vert 一致）
    // location 0: position (vec3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PlayerVertex),
                          (void*)offsetof(PlayerVertex, position));
    // location 1: normal (vec3)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PlayerVertex),
                          (void*)offsetof(PlayerVertex, normal));
    // location 2: texCoord (vec2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PlayerVertex),
                          (void*)offsetof(PlayerVertex, texCoord));
    // location 3: tangent (vec3)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(PlayerVertex),
                          (void*)offsetof(PlayerVertex, tangent));
    // location 4: bitangent (vec3)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(PlayerVertex),
                          (void*)offsetof(PlayerVertex, bitangent));

    glBindVertexArray(0);
}

void PlayerModel::addFace(std::vector<PlayerVertex>& vertices, std::vector<unsigned int>& indices,
                           const glm::vec3& p0, const glm::vec3& p1,
                           const glm::vec3& p2, const glm::vec3& p3,
                           const glm::vec3& normal,
                           float u0, float v0, float u1, float v1) {
    unsigned int base = static_cast<unsigned int>(vertices.size());

    // 计算切线和副切线
    glm::vec3 edge1 = p1 - p0;
    glm::vec3 edge2 = p2 - p0;
    glm::vec2 deltaUV1 = glm::vec2(u1 - u0, 0.0f);
    glm::vec2 deltaUV2 = glm::vec2(u1 - u0, v1 - v0);

    float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
    glm::vec3 tangent(1, 0, 0), bitangent(0, 1, 0);
    if (fabs(det) > 1e-6f) {
        float invDet = 1.0f / det;
        tangent = glm::normalize(invDet * (deltaUV2.y * edge1 - deltaUV1.y * edge2));
        bitangent = glm::normalize(invDet * (-deltaUV2.x * edge1 + deltaUV1.x * edge2));
    }

    // 4个顶点，UV 按照 (左下, 右下, 右上, 左上) 映射
    // MC 皮肤 v0 对应图片上方，v1 对应下方
    vertices.push_back({p0, normal, glm::vec2(u0, v1), tangent, bitangent});  // 左下
    vertices.push_back({p1, normal, glm::vec2(u1, v1), tangent, bitangent});  // 右下
    vertices.push_back({p2, normal, glm::vec2(u1, v0), tangent, bitangent});  // 右上
    vertices.push_back({p3, normal, glm::vec2(u0, v0), tangent, bitangent});  // 左上

    // 两个三角形
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
    indices.push_back(base + 0);
    indices.push_back(base + 2);
    indices.push_back(base + 3);
}

void PlayerModel::draw(Shader& shader, const glm::vec3& worldPos, float yaw) {
    // 绑定皮肤纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_skinTexture);
    shader.setInt("texture_diffuse1", 0);

    for (int i = 0; i < PART_COUNT; i++) {
        if (m_parts[i].VAO == 0) continue;

        // 构建部件的 model 矩阵
        glm::mat4 model = glm::mat4(1.0f);

        // 1. 移动到世界位置
        model = glm::translate(model, worldPos);

        // 2. 整体旋转（朝向）
        model = glm::rotate(model, yaw, glm::vec3(0, 1, 0));

        // 3. 移动到部件的 origin 位置
        model = glm::translate(model, m_parts[i].origin);

        shader.setMat4("model", model);

        glBindVertexArray(m_parts[i].VAO);
        glDrawElements(GL_TRIANGLES, m_parts[i].indexCount, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
}

void PlayerModel::drawPosed(Shader& shader, const glm::vec3& worldPos, const PlayerPose& pose) {
    // 绑定皮肤纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_skinTexture);
    shader.setInt("texture_diffuse1", 0);

    // 根变换：世界位置 -> 身体整体偏移 -> 身体朝向（yaw） -> 身体前倾（pitch）
    // 身体前倾以胯部（y = 12 像素 = 0.75 模型单位）作为枢轴，避免绕脚底翻转
    glm::mat4 root = glm::mat4(1.0f);
    root = glm::translate(root, worldPos + pose.rootOffset);
    root = glm::rotate(root, pose.bodyYaw, glm::vec3(0, 1, 0));

    const glm::vec3 hipPivot = glm::vec3(0.0f, 12.0f / 16.0f, 0.0f);
    glm::mat4 bodyTilt = glm::mat4(1.0f);
    if (pose.bodyPitch != 0.0f) {
        bodyTilt = glm::translate(bodyTilt, hipPivot);
        bodyTilt = glm::rotate(bodyTilt, pose.bodyPitch, glm::vec3(1, 0, 0));
        bodyTilt = glm::translate(bodyTilt, -hipPivot);
    }

    for (int i = 0; i < PART_COUNT; i++) {
        if (m_parts[i].VAO == 0) continue;

        // 每个部件先应用身体根变换（位置+yaw+身体前倾）
        glm::mat4 model = root * bodyTilt;

        // 移到部件 origin
        model = glm::translate(model, m_parts[i].origin);

        // 部件级局部旋转（绕 pivot）
        // pivot 以部件 origin 为原点的偏移
        glm::vec3 pv = m_parts[i].pivot;

        auto applyJointRotation = [&](float pitch, float roll, float yaw) {
            if (pitch == 0.0f && roll == 0.0f && yaw == 0.0f) return;
            model = glm::translate(model, pv);
            if (yaw != 0.0f)   model = glm::rotate(model, yaw,   glm::vec3(0, 1, 0));
            if (pitch != 0.0f) model = glm::rotate(model, pitch, glm::vec3(1, 0, 0));
            if (roll != 0.0f)  model = glm::rotate(model, roll,  glm::vec3(0, 0, 1));
            model = glm::translate(model, -pv);
        };

        switch (i) {
        case HEAD:
            applyJointRotation(pose.headPitch, 0.0f, pose.headYaw);
            break;
        case RIGHT_ARM:
            applyJointRotation(pose.rightArmPitch, pose.rightArmRoll, 0.0f);
            break;
        case LEFT_ARM:
            applyJointRotation(pose.leftArmPitch, pose.leftArmRoll, 0.0f);
            break;
        case RIGHT_LEG:
            applyJointRotation(pose.rightLegPitch, 0.0f, 0.0f);
            break;
        case LEFT_LEG:
            applyJointRotation(pose.leftLegPitch, 0.0f, 0.0f);
            break;
        case BODY:
        default:
            break;
        }

        shader.setMat4("model", model);

        glBindVertexArray(m_parts[i].VAO);
        glDrawElements(GL_TRIANGLES, m_parts[i].indexCount, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
}

GLuint PlayerModel::loadTexture(const std::string& path) {
    int width, height, channels;
    // MC 皮肤通常有透明通道，强制加载为 RGBA
    unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!data) {
        std::cerr << "PlayerModel: Failed to load texture: " << path << std::endl;
        return 0;
    }

    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    // MC 像素风格：最近邻过滤
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);

    std::cout << "PlayerModel: Loaded skin " << path
              << " (" << width << "x" << height << ")" << std::endl;
    return textureID;
}
