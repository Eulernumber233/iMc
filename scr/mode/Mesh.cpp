#include "Mesh.h"

Mesh::Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<TexCoords> textures)
{
    this->vertices = vertices;
    this->indices = indices;
    this->textures = textures;

    calculateTangents();
    setupMesh();
}

void Mesh::Draw(Shader& shader)
{
    unsigned int diffuseNr = 0;
    unsigned int specularNr = 0;
    unsigned int normalNr = 0;
    for (unsigned int i = 0; i < textures.size(); i++)
    {
        glActiveTexture(GL_TEXTURE0 + i); // 在绑定之前激活相应的纹理单元
        // 获取纹理序号（diffuse_textureN 中的 N）
        string number;
        string name = textures[i].type;
        if (name == "texture_diffuse")
            number = std::to_string(++diffuseNr);
        else if (name == "texture_specular")
            number = std::to_string(++specularNr);
        else if (name == "texture_normal")
            number = std::to_string(++normalNr);
        else continue;
        glUniform1i(glGetUniformLocation(shader.programID, (name + number).c_str()), i);
        // and finally bind the texture
        glBindTexture(GL_TEXTURE_2D, textures[i].id);
    }
    glActiveTexture(GL_TEXTURE0);

    // 绘制网格
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Mesh::setupMesh()
{
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);

    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int),
        &indices[0], GL_STATIC_DRAW);

    // 顶点位置
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    // 顶点法线
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
    // 顶点纹理坐标
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
    // 切线
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
    // 副切线
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, bitangent));

    glBindVertexArray(0);
}

void Mesh::calculateTangents()
{
    // 遍历所有三角形
    for (size_t i = 0; i < indices.size(); i += 3) {
        // 获取三角形的三个顶点索引
        unsigned int idx0 = indices[i];
        unsigned int idx1 = indices[i + 1];
        unsigned int idx2 = indices[i + 2];

        // 获取顶点数据
        Vertex& v0 = vertices[idx0];
        Vertex& v1 = vertices[idx1];
        Vertex& v2 = vertices[idx2];

        // 计算三角形的边向量
        glm::vec3 edge1 = v1.Position - v0.Position;
        glm::vec3 edge2 = v2.Position - v0.Position;

        // 计算纹理坐标的差异
        glm::vec2 deltaUV1 = v1.TexCoords - v0.TexCoords;
        glm::vec2 deltaUV2 = v2.TexCoords - v0.TexCoords;

        // 计算行列式
        float det = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;

        // 避免除以零
        if (fabs(det) < 1e-6f) {
            continue;
        }

        float invDet = 1.0f / det;

        // 计算切线和副切线
        glm::vec3 tangent, bitangent;

        tangent.x = invDet * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
        tangent.y = invDet * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
        tangent.z = invDet * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
        tangent = glm::normalize(tangent);

        bitangent.x = invDet * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
        bitangent.y = invDet * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
        bitangent.z = invDet * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
        bitangent = glm::normalize(bitangent);

        // 累加到三个顶点
        vertices[idx0].tangent += tangent;
        vertices[idx0].bitangent += bitangent;
        vertices[idx0].count++;

        vertices[idx1].tangent += tangent;
        vertices[idx1].bitangent += bitangent;
        vertices[idx1].count++;

        vertices[idx2].tangent += tangent;
        vertices[idx2].bitangent += bitangent;
        vertices[idx2].count++;
    }

    // 平均并归一化切线和副切线，并确保它们与法线正交
    for (size_t i = 0; i < vertices.size(); i++) {
        if (vertices[i].count > 0) {
            // 平均切线和副切线
            glm::vec3 tangent = vertices[i].tangent / (float)vertices[i].count;
            glm::vec3 bitangent = vertices[i].bitangent / (float)vertices[i].count;

            // 确保切线与法线正交（Gram-Schmidt正交化）
            glm::vec3 normal = vertices[i].Normal;
            tangent = glm::normalize(tangent - glm::dot(tangent, normal) * normal);

            // 重新计算副切线以确保正交性
            bitangent = glm::normalize(glm::cross(normal, tangent));

            // 检查手性
            if (glm::dot(glm::cross(tangent, bitangent), normal) < 0.0f) {
                tangent = -tangent;
            }
        }
    }
}
