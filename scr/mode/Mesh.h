#pragma once


#include "../Shader.h"

struct VertexMode {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    int count = 0;// ����ƽ������
};
struct TexCoords {
    unsigned int id;
    std::string type;
    aiString path;
};
class Mesh {
public:
    /*  ��������  */
    std::vector<VertexMode> vertices;
    std::vector<unsigned int> indices;
    std::vector<TexCoords> textures;

    Mesh(std::vector<VertexMode> vertices, std::vector<unsigned int> indices, std::vector<TexCoords> textures);
    void Draw(Shader& shader);
private:
    /*  ��Ⱦ����  */
    unsigned int VAO, VBO, EBO;

    void setupMesh();
    // �������ߺ͸�����
    void calculateTangents();
};
