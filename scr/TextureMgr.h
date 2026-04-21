#pragma once

#include <json/json.h>
#include "core.h"
#include "Data.h"
#include <unordered_map>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <algorithm>

struct TextureInfo {
    int width = 0;
    int height = 0;
    int nrChannels = 0;
    GLenum format = GL_RGB;
};

struct Texture2DConfig {
    std::string name;
    std::string path;
    std::string category = "block";   // "block" 或 "ui"
    bool isSRGB = false;
    int index = -1;                    // 仅对 block 有效
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    GLint minFilter = GL_NEAREST_MIPMAP_NEAREST; // 默认最近邻 mipmap
    GLint magFilter = GL_NEAREST;
};

struct CubeTextureConfig {
    std::string name;
    std::vector<std::string> faces;
    GLint wrapS = GL_CLAMP_TO_EDGE;
    GLint wrapT = GL_CLAMP_TO_EDGE;
    GLint wrapR = GL_CLAMP_TO_EDGE;
    GLint minFilter = GL_LINEAR;
    GLint magFilter = GL_LINEAR;
};

class TextureMgr {
private:
    // 核心存储：2D纹理（名称→ID，包含所有纹理）、立方体贴图
    std::unordered_map<std::string, GLuint> m_textures2D;
    std::unordered_map<std::string, GLuint> m_texturesCube;
    std::unordered_map<std::string, TextureInfo> m_textureInfos;

    // 纹理数组相关（仅方块纹理）
    GLuint m_textureArray = 0;
    std::unordered_map<std::string, int> m_textureLayerIndex; // 名称 → 层索引

    std::string m_basePath = "assert/textures/";
    std::string m_configPath = "assert/textures/textures_config.json";
    static std::shared_ptr<TextureMgr> m_instance;

    TextureMgr() {
        stbi_set_flip_vertically_on_load(false);
        parseConfig();
    }

    TextureInfo GetTextureInfo(const std::string& name) const;
    unsigned char* GetTexturePixels(const std::string& name) const;
    unsigned char* getTexturePixels(GLuint texID, const TextureInfo& info);

    void parseConfig();
    GLint getGLenumFromStr(const std::string& str);

    // 加载单个2D纹理（内部使用，根据类别设置过滤）
    GLuint loadTexture2D(const Texture2DConfig& config);

    GLuint loadTextureCube(const CubeTextureConfig& config);

public:
    static std::shared_ptr<TextureMgr> GetInstance();
    TextureMgr(const TextureMgr&) = delete;
    TextureMgr& operator=(const TextureMgr&) = delete;

    GLuint GetTexture2D(const std::string& name);
    GLuint GetTextureCube(const std::string& name);
    std::unordered_map<std::string, GLuint>& GetAllTextures2D() { return m_textures2D; }
    std::unordered_map<std::string, GLuint>& GetAllTexturesCube() { return m_texturesCube; }

    GLuint LoadTexture2DManual(const std::string& name, const std::string& path, bool isSRGB = false);
    GLuint LoadTextureCubeManual(const std::string& name, const std::vector<std::string>& faces);

    // 新增接口
    GLuint GetTextureArray() const { return m_textureArray; }
    int GetTextureLayerIndex(const std::string& name) const {
        auto it = m_textureLayerIndex.find(name);
        return (it != m_textureLayerIndex.end()) ? it->second : -1;
    }

    ~TextureMgr();
};