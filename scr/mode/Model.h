#pragma once
#include "Mesh.h"
#include <string>

unsigned int TextureFromFile(const char* path
    , const std::string& directory, bool gamma = false);
class Model
{
public:
    /*  ����   */
    Model(const char* path)
    {
        loadModel(path);
    }
    void Draw(Shader& shader);
private:
    // ���м��ع�������ȫ�ִ���
    std::vector<TexCoords> textures_loaded;
    /*  ģ������  */
    std::vector<Mesh> meshes;
    std::string directory;
    /*  ����   */
    void loadModel(std::string path);
    void processNode(aiNode* node, const aiScene* scene);
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);
    std::vector<TexCoords> loadMaterialTextures(aiMaterial* mat, aiTextureType type,
        std::string typeName);
};
