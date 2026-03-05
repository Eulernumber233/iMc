#pragma once


#include "../Shader.h"

struct VertexMode {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    int count = 0;// 用于平均计算
};
struct TexCoords {
    unsigned int id;
    std::string type;
    aiString path;
};
class Mesh {
public:
    /*  网格数据  */
    std::vector<VertexMode> vertices;
    std::vector<unsigned int> indices;
    std::vector<TexCoords> textures;

    Mesh(std::vector<VertexMode> vertices, std::vector<unsigned int> indices, std::vector<TexCoords> textures);
    void Draw(Shader& shader);
private:
    /*  渲染数据  */
    unsigned int VAO, VBO, EBO;

    void setupMesh();
    // 计算切线和副切线
    void calculateTangents();
};
