#pragma once
#include "Mesh.h"

unsigned int TextureFromFile(const char* path
    , const string& directory, bool gamma = false);
class Model
{
public:
    /*  函数   */
    Model(const char* path)
    {
        loadModel(path);
    }
    void Draw(Shader shader);
private:
    // 所有加载过的纹理全局储存
    vector<TexCoords> textures_loaded;
    /*  模型数据  */
    vector<Mesh> meshes;
    string directory;
    /*  函数   */
    void loadModel(string path);
    void processNode(aiNode* node, const aiScene* scene);
    Mesh processMesh(aiMesh* mesh, const aiScene* scene);
    vector<TexCoords> loadMaterialTextures(aiMaterial* mat, aiTextureType type,
        string typeName);
};
