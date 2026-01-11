#pragma once

// jsoncpp 0.5.0头文件
#include <json/json.h>

// 项目核心头文件
#include "core.h"
#include "Data.h"
// 标准库
#include <unordered_map>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <algorithm>

// 纹理信息结构体：存储2D纹理的关键参数
struct TextureInfo {
    int width = 0;          // 纹理宽度
    int height = 0;         // 纹理高度
    int nrChannels = 0;     // 颜色通道数
    GLenum format = GL_RGB; // 纹理格式（GL_RGB/GL_RGBA等）
};

// 2D纹理配置结构体：对应JSON中的2D纹理配置项
struct Texture2DConfig {
    std::string name;       // 纹理名称（唯一标识）
    std::string path;       // 纹理文件相对路径
    bool isSRGB = false;    // 是否为SRGB格式
    // 采样参数（默认值为常用配置）
    GLint wrapS = GL_REPEAT;
    GLint wrapT = GL_REPEAT;
    GLint minFilter = GL_LINEAR_MIPMAP_LINEAR;
    GLint magFilter = GL_LINEAR;
};

// 立方体贴图配置结构体：对应JSON中的立方体贴图配置项
struct CubeTextureConfig {
    std::string name;       // 立方体贴图名称（唯一标识）
    std::vector<std::string> faces; // 6个面的文件路径（顺序：+X、-X、+Y、-Y、+Z、-Z）
    // 采样参数（默认值为立方体贴图常用配置）
    GLint wrapS = GL_CLAMP_TO_EDGE;
    GLint wrapT = GL_CLAMP_TO_EDGE;
    GLint wrapR = GL_CLAMP_TO_EDGE;
    GLint minFilter = GL_LINEAR;
    GLint magFilter = GL_LINEAR;
};

class TextureMgr {
private:
    // 核心存储：2D纹理（名称→ID）、立方体贴图（名称→ID）
    std::unordered_map<std::string, GLuint> m_textures2D;
    std::unordered_map<std::string, GLuint> m_texturesCube;
    // 2D纹理信息存储（用于后续扩展，如像素数据读取）
    std::unordered_map<std::string, TextureInfo> m_textureInfos;

    // 路径配置
    std::string m_basePath = "assert/textures/";
    std::string m_configPath = "assert/textures/textures_config.json"; // JSON配置文件路径
    // 单例实例
    static std::shared_ptr<TextureMgr> m_instance;

    // 私有构造函数：解析配置文件并加载所有纹理
    TextureMgr() {
        // 初始化stb_image（解决图片翻转问题，立方体贴图通常不需要翻转）
        stbi_set_flip_vertically_on_load(false);
        // 解析JSON配置并加载纹理
        parseConfig();
    }

    // 辅助函数：从2D纹理中读取像素数据（用于后续扩展，如立方体贴图合成）
    unsigned char* getTexturePixels(GLuint texID, const TextureInfo& info) {
        if (texID == 0 || info.width == 0 || info.height == 0) {
            return nullptr;
        }
        unsigned char* pixels = new unsigned char[info.width * info.height * info.nrChannels];
        glBindTexture(GL_TEXTURE_2D, texID);
        glGetTexImage(GL_TEXTURE_2D, 0, info.format, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        return pixels;
    }

    // 核心修改：适配jsoncpp 0.5.0的配置解析函数
    void parseConfig() {
        try {
            // 1. 读取配置文件内容（jsoncpp 0.5.0的Reader不支持直接读取ifstream，需手动读取字符串）
            std::ifstream configFile(m_configPath);
            if (!configFile.is_open()) {
                throw std::runtime_error("无法打开配置文件：" + m_configPath);
            }
            std::stringstream ss;
            ss << configFile.rdbuf();
            std::string configStr = ss.str();
            configFile.close();

            // 2. 解析JSON字符串（使用jsoncpp的Json::Reader和Json::Value）
            Json::Reader reader;
            Json::Value config;
            if (!reader.parse(configStr, config)) {
                throw std::runtime_error("JSON解析失败：");
            }

            // 3. 加载2D纹理
            if (config.isMember("2d_textures") && config["2d_textures"].isArray()) {
                Json::Value& texArray = config["2d_textures"];
                for (int i = 0; i < texArray.size(); i++) {
                    Json::Value& item = texArray[i];
                    Texture2DConfig texConfig;

                    // 读取必选参数（带空值校验）
                    texConfig.name = item.get("name", "").asString();
                    texConfig.path = item.get("path", "").asString();
                    if (texConfig.name.empty() || texConfig.path.empty()) {
                        std::cerr << "警告：第" << i + 1 << "个2D纹理配置项缺少name或path，跳过加载" << std::endl;
                        continue;
                    }

                    // 读取可选参数（判断键是否存在，否则用默认值）
                    texConfig.isSRGB = item.isMember("is_srgb") ? item["is_srgb"].asBool() : false;
                    std::string wrapS = item.isMember("wrap_s") ? item["wrap_s"].asString() : "repeat";
                    std::string wrapT = item.isMember("wrap_t") ? item["wrap_t"].asString() : "repeat";
                    std::string minFilter = item.isMember("min_filter") ? item["min_filter"].asString() : "linear_mipmap_linear";
                    std::string magFilter = item.isMember("mag_filter") ? item["mag_filter"].asString() : "linear";

                    // 转换为GLenum
                    texConfig.wrapS = getGLenumFromStr(wrapS);
                    texConfig.wrapT = getGLenumFromStr(wrapT);
                    texConfig.minFilter = getGLenumFromStr(minFilter);
                    texConfig.magFilter = getGLenumFromStr(magFilter);

                    // 加载2D纹理
                    loadTexture2D(texConfig);
                }
            }

            // 4. 加载立方体贴图
            if (config.isMember("cube_textures") && config["cube_textures"].isArray()) {
                Json::Value& cubeArray = config["cube_textures"];
                for (int i = 0; i < cubeArray.size(); i++) {
                    Json::Value& item = cubeArray[i];
                    CubeTextureConfig cubeConfig;

                    // 读取必选参数
                    cubeConfig.name = item.get("name", "").asString();
                    // 读取faces数组并转换为std::vector<std::string>
                    if (item.isMember("faces") && item["faces"].isArray()) {
                        Json::Value& facesArray = item["faces"];
                        for (int j = 0; j < facesArray.size(); j++) {
                            cubeConfig.faces.push_back(facesArray[j].asString());
                        }
                    }

                    // 必选参数校验
                    if (cubeConfig.name.empty() || cubeConfig.faces.size() != 6) {
                        std::cerr << "警告：立方体贴图[" << cubeConfig.name << "]名称为空或面数量不是6，跳过加载" << std::endl;
                        continue;
                    }

                    // 读取可选参数
                    std::string wrapS = item.isMember("wrap_s") ? item["wrap_s"].asString() : "clamp_to_edge";
                    std::string wrapT = item.isMember("wrap_t") ? item["wrap_t"].asString() : "clamp_to_edge";
                    std::string wrapR = item.isMember("wrap_r") ? item["wrap_r"].asString() : "clamp_to_edge";
                    std::string minFilter = item.isMember("min_filter") ? item["min_filter"].asString() : "linear";
                    std::string magFilter = item.isMember("mag_filter") ? item["mag_filter"].asString() : "linear";

                    // 转换为GLenum
                    cubeConfig.wrapS = getGLenumFromStr(wrapS);
                    cubeConfig.wrapT = getGLenumFromStr(wrapT);
                    cubeConfig.wrapR = getGLenumFromStr(wrapR);
                    cubeConfig.minFilter = getGLenumFromStr(minFilter);
                    cubeConfig.magFilter = getGLenumFromStr(magFilter);

                    // 加载立方体贴图
                    loadTextureCube(cubeConfig);
                }
            }

            std::cout << "纹理配置文件解析完成，共加载2D纹理：" << m_textures2D.size() << "个，立方体贴图：" << m_texturesCube.size() << "个" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "解析纹理配置文件失败：" << e.what() << std::endl;
        }
    }

    // 辅助函数：将配置文件中的字符串转换为GLenum（如"repeat"→GL_REPEAT）
    GLint getGLenumFromStr(const std::string& str) {
        // 统一转换为小写（避免配置文件中大小写不一致的问题）
        std::string lowerStr = str;
        std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), ::tolower);

        if (lowerStr == "repeat") return GL_REPEAT;
        if (lowerStr == "clamp_to_edge") return GL_CLAMP_TO_EDGE;
        if (lowerStr == "mirror_repeat") return GL_MIRRORED_REPEAT;
        if (lowerStr == "linear") return GL_LINEAR;
        if (lowerStr == "nearest") return GL_NEAREST;
        if (lowerStr == "linear_mipmap_linear") return GL_LINEAR_MIPMAP_LINEAR;
        if (lowerStr == "linear_mipmap_nearest") return GL_LINEAR_MIPMAP_NEAREST;
        if (lowerStr == "nearest_mipmap_linear") return GL_NEAREST_MIPMAP_LINEAR;
        if (lowerStr == "nearest_mipmap_nearest") return GL_NEAREST_MIPMAP_NEAREST;
        // 默认值
        return GL_REPEAT;
    }

    // 核心函数：加载2D纹理（接收配置结构体）
    GLuint loadTexture2D(const Texture2DConfig& config) {
        // 检查是否已加载，避免重复加载
        if (m_textures2D.find(config.name) != m_textures2D.end()) {
            std::cout << "2D纹理[" << config.name << "]已加载，直接返回ID" << std::endl;
            return m_textures2D[config.name];
        }

        std::string fullPath = m_basePath + config.path;
        int width, height, nrChannels;
        unsigned char* data = stbi_load(fullPath.c_str(), &width, &height, &nrChannels, 0);

        if (!data) {
            std::cerr << "2D纹理加载失败：" << fullPath << std::endl;
            return 0;
        }

        // 确定纹理内部格式和数据格式
        GLenum internalFormat = GL_RGB;
        GLenum dataFormat = GL_RGB;
        if (config.isSRGB) {
            internalFormat = (nrChannels == 4) ? GL_SRGB8_ALPHA8 : GL_SRGB8;
        }
        else {
            if (nrChannels == 1) {
                internalFormat = GL_RED;
                dataFormat = GL_RED;
            }
            else if (nrChannels == 3) {
                internalFormat = GL_RGB;
                dataFormat = GL_RGB;
            }
            else if (nrChannels == 4) {
                internalFormat = GL_RGBA;
                dataFormat = GL_RGBA;
            }
        }

        // 创建纹理ID
        GLuint texID;
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_2D, texID);

        // 加载纹理数据
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
        // 生成Mipmap（仅当minFilter使用mipmap相关模式时）
        if (config.minFilter != GL_LINEAR && config.minFilter != GL_NEAREST) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }

        // 设置纹理参数
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, config.wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, config.wrapT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, config.minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, config.magFilter);

        // 存储纹理信息
        TextureInfo info;
        info.width = width;
        info.height = height;
        info.nrChannels = nrChannels;
        info.format = dataFormat;
        m_textureInfos[config.name] = info;

        // 释放资源
        stbi_image_free(data);
        glBindTexture(GL_TEXTURE_2D, 0);

        // 存储纹理ID
        m_textures2D[config.name] = texID;
        std::cout << "2D纹理加载成功：" << config.name << " (" << fullPath << ")" << std::endl;

        return texID;
    }

    // 核心函数：加载立方体贴图（接收配置结构体）
    GLuint loadTextureCube(const CubeTextureConfig& config) {
        // 检查是否已加载，避免重复加载
        if (m_texturesCube.find(config.name) != m_texturesCube.end()) {
            std::cout << "立方体贴图[" << config.name << "]已加载，直接返回ID" << std::endl;
            return m_texturesCube[config.name];
        }

        // 创建立方体贴图ID
        GLuint texID;
        glGenTextures(1, &texID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texID);

        // 立方体贴图的6个面枚举（顺序必须与配置中的faces一致：+X、-X、+Y、-Y、+Z、-Z）
        GLenum cubeFaces[] = {
            GL_TEXTURE_CUBE_MAP_POSITIVE_X,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
            GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
            GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
        };

        // 加载每个面的纹理数据
        bool loadSuccess = true;
        for (int i = 0; i < 6; i++) {
            std::string fullPath = m_basePath + config.faces[i];
            int width, height, nrChannels;
            unsigned char* data = stbi_load(fullPath.c_str(), &width, &height, &nrChannels, 0);

            if (!data) {
                std::cerr << "立方体贴图面加载失败：" << fullPath << std::endl;
                loadSuccess = false;
                stbi_image_free(data);
                break;
            }

            // 确定纹理格式
            GLenum internalFormat = (nrChannels == 4) ? GL_RGBA : GL_RGB;
            GLenum dataFormat = (nrChannels == 4) ? GL_RGBA : GL_RGB;

            // 加载面数据
            glTexImage2D(cubeFaces[i], 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }

        if (!loadSuccess) {
            glDeleteTextures(1, &texID);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            return 0;
        }

        // 设置立方体贴图参数
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, config.wrapS);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, config.wrapT);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, config.wrapR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, config.minFilter);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, config.magFilter);

        // 生成Mipmap（仅当minFilter使用mipmap相关模式时）
        if (config.minFilter != GL_LINEAR && config.minFilter != GL_NEAREST) {
            glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
        }

        // 解绑
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

        // 存储纹理ID
        m_texturesCube[config.name] = texID;
        std::cout << "立方体贴图加载成功：" << config.name << std::endl;

        return texID;
    }

public:
    // 单例获取接口（禁止拷贝和赋值）
    static std::shared_ptr<TextureMgr> GetInstance() {
        if (!m_instance) {
            m_instance = std::shared_ptr<TextureMgr>(new TextureMgr());
        }
        return m_instance;
    }

    // 禁止拷贝和赋值
    TextureMgr(const TextureMgr&) = delete;
    TextureMgr& operator=(const TextureMgr&) = delete;

    // 对外接口：获取2D纹理ID
    GLuint GetTexture2D(const std::string& name) {
        auto it = m_textures2D.find(name);
        if (it != m_textures2D.end()) {
            return it->second;
        }
        std::cerr << "错误：2D纹理[" << name << "]未找到" << std::endl;
        return 0;
    }

    // 对外接口：获取立方体贴图ID
    GLuint GetTextureCube(const std::string& name) {
        auto it = m_texturesCube.find(name);
        if (it != m_texturesCube.end()) {
            return it->second;
        }
        std::cerr << "错误：立方体贴图[" << name << "]未找到" << std::endl;
        return 0;
    }

    // 对外接口：获取所有2D纹理的引用（用于调试/扩展）
    std::unordered_map<std::string, GLuint>& GetAllTextures2D() {
        return m_textures2D;
    }

    // 对外接口：获取所有立方体贴图的引用（用于调试/扩展）
    std::unordered_map<std::string, GLuint>& GetAllTexturesCube() {
        return m_texturesCube;
    }

    // 对外接口：手动加载2D纹理（配置文件外的临时加载，可选）
    GLuint LoadTexture2DManual(const std::string& name, const std::string& path, bool isSRGB = false) {
        Texture2DConfig config;
        config.name = name;
        config.path = path;
        config.isSRGB = isSRGB;
        return loadTexture2D(config);
    }

    // 对外接口：手动加载立方体贴图（配置文件外的临时加载，可选）
    GLuint LoadTextureCubeManual(const std::string& name, const std::vector<std::string>& faces) {
        CubeTextureConfig config;
        config.name = name;
        config.faces = faces;
        return loadTextureCube(config);
    }

    // 析构函数：释放所有纹理资源（可选，OpenGL上下文销毁时会自动释放）
    ~TextureMgr() {
        // 释放2D纹理
        for (auto& pair : m_textures2D) {
            glDeleteTextures(1, &pair.second);
        }
        // 释放立方体贴图
        for (auto& pair : m_texturesCube) {
            glDeleteTextures(1, &pair.second);
        }
        std::cout << "纹理管理器已释放所有纹理资源" << std::endl;
    }
};
