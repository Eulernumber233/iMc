#pragma once


#include "Shader.h"

using namespace std;
struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
    glm::vec3 tangent;
    glm::vec3 bitangent;
    int count = 0;// 用于平均计算
};
struct TexCoords {
    unsigned int id;
    string type;
    aiString path;
};
class Mesh {
public:
    /*  网格数据  */
    vector<Vertex> vertices;
    vector<unsigned int> indices;
    vector<TexCoords> textures;

    Mesh(vector<Vertex> vertices, vector<unsigned int> indices, vector<TexCoords> textures);
    void Draw(Shader& shader);
private:
    /*  渲染数据  */
    unsigned int VAO, VBO, EBO;

    void setupMesh();
    // 计算切线和副切线
    void calculateTangents();
};
