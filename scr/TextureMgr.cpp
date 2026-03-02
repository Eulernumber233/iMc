#include "TextureMgr.h"

std::shared_ptr<TextureMgr> TextureMgr::m_instance = nullptr;

std::shared_ptr<TextureMgr> TextureMgr::GetInstance() {
    if (!m_instance) {
        m_instance = std::shared_ptr<TextureMgr>(new TextureMgr());
    }
    return m_instance;
}

TextureInfo TextureMgr::GetTextureInfo(const std::string& name) const {
    auto it = m_textureInfos.find(name);
    if (it != m_textureInfos.end()) return it->second;
    return TextureInfo{};
}

unsigned char* TextureMgr::GetTexturePixels(const std::string& name) const {
    auto itTex = m_textures2D.find(name);
    if (itTex == m_textures2D.end()) return nullptr;
    auto itInfo = m_textureInfos.find(name);
    if (itInfo == m_textureInfos.end()) return nullptr;

    const TextureInfo& info = itInfo->second;
    unsigned char* pixels = new unsigned char[info.width * info.height * info.nrChannels];
    glBindTexture(GL_TEXTURE_2D, itTex->second);
    glGetTexImage(GL_TEXTURE_2D, 0, info.format, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return pixels;
}

unsigned char* TextureMgr::getTexturePixels(GLuint texID, const TextureInfo& info) {
    if (texID == 0 || info.width == 0 || info.height == 0) return nullptr;
    unsigned char* pixels = new unsigned char[info.width * info.height * info.nrChannels];
    glBindTexture(GL_TEXTURE_2D, texID);
    glGetTexImage(GL_TEXTURE_2D, 0, info.format, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return pixels;
}

GLint TextureMgr::getGLenumFromStr(const std::string& str) {
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
    return GL_REPEAT;
}

GLuint TextureMgr::loadTexture2D(const Texture2DConfig& config) {
    if (m_textures2D.find(config.name) != m_textures2D.end()) {
        std::cout << "纹理[" << config.name << "]已存在，返回现有ID" << std::endl;
        return m_textures2D[config.name];
    }

    std::string fullPath = m_basePath + config.path;
    int width, height, nrChannels;
    unsigned char* data = stbi_load(fullPath.c_str(), &width, &height, &nrChannels, 4); // 强制RGBA
    if (!data) {
        std::cerr << "纹理加载失败：" << fullPath << std::endl;
        return 0;
    }

    // 确定内部格式（统一使用 RGBA8）
    GLenum internalFormat = GL_RGBA8;
    GLenum dataFormat = GL_RGBA;

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_2D, texID);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);

    // 根据类别设置过滤和mipmap
    if (config.category == "ui") {
        // UI纹理：最近邻，不生成mipmap
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    else {
        // 方块纹理：使用配置的过滤，并生成mipmap（如果需要）
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, config.minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, config.magFilter);
        if (config.minFilter != GL_NEAREST && config.minFilter != GL_LINEAR) {
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, config.wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, config.wrapT);
    glBindTexture(GL_TEXTURE_2D, 0);

    TextureInfo info;
    info.width = width;
    info.height = height;
    info.nrChannels = 4; // 强制RGBA
    info.format = GL_RGBA;
    m_textureInfos[config.name] = info;
    m_textures2D[config.name] = texID;

    stbi_image_free(data);
    std::cout << "2D纹理加载成功：" << config.name << " (" << fullPath << ")" << std::endl;
    return texID;
}

GLuint TextureMgr::loadTextureCube(const CubeTextureConfig& config) {
    if (m_texturesCube.find(config.name) != m_texturesCube.end()) {
        std::cout << "立方体贴图[" << config.name << "]已存在，返回现有ID" << std::endl;
        return m_texturesCube[config.name];
    }

    GLuint texID;
    glGenTextures(1, &texID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, texID);

    GLenum cubeFaces[] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };

    bool loadSuccess = true;
    for (int i = 0; i < 6; ++i) {
        std::string fullPath = m_basePath + config.faces[i];
        int w, h, ch;
        unsigned char* data = stbi_load(fullPath.c_str(), &w, &h, &ch, 4);
        if (!data) {
            std::cerr << "立方体贴图面加载失败：" << fullPath << std::endl;
            loadSuccess = false;
            stbi_image_free(data);
            break;
        }
        glTexImage2D(cubeFaces[i], 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
    }

    if (!loadSuccess) {
        glDeleteTextures(1, &texID);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        return 0;
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, config.wrapS);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, config.wrapT);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, config.wrapR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, config.minFilter);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, config.magFilter);
    if (config.minFilter != GL_NEAREST && config.minFilter != GL_LINEAR) {
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    m_texturesCube[config.name] = texID;
    std::cout << "立方体贴图加载成功：" << config.name << std::endl;
    return texID;
}

void TextureMgr::parseConfig() {
    try {
        std::ifstream configFile(m_configPath);
        if (!configFile.is_open()) {
            throw std::runtime_error("无法打开配置文件：" + m_configPath);
        }
        std::stringstream ss;
        ss << configFile.rdbuf();
        std::string configStr = ss.str();
        configFile.close();

        Json::Reader reader;
        Json::Value config;
        if (!reader.parse(configStr, config)) {
            throw std::runtime_error("JSON解析失败");
        }

        std::vector<Texture2DConfig> blockConfigs;
        std::vector<Texture2DConfig> uiConfigs;
        std::vector<CubeTextureConfig> cubeConfigs;

        // 解析2D纹理
        if (config.isMember("2d_textures") && config["2d_textures"].isArray()) {
            Json::Value& texArray = config["2d_textures"];
            for (int i = 0; i < texArray.size(); ++i) {
                Json::Value& item = texArray[i];
                Texture2DConfig texConfig;
                texConfig.name = item.get("name", "").asString();
                texConfig.path = item.get("path", "").asString();
                if (texConfig.name.empty() || texConfig.path.empty()) {
                    std::cerr << "警告：第" << i + 1 << "个2D纹理缺少name或path，跳过" << std::endl;
                    continue;
                }

                texConfig.category = item.isMember("category") ? item["category"].asString() : "block";
                texConfig.isSRGB = item.isMember("is_srgb") ? item["is_srgb"].asBool() : false;
                texConfig.index = item.isMember("index") ? item["index"].asInt() : -1;

                // 读取采样参数，若缺失则根据类别设置默认
                std::string wrapS = item.isMember("wrap_s") ? item["wrap_s"].asString() : "repeat";
                std::string wrapT = item.isMember("wrap_t") ? item["wrap_t"].asString() : "repeat";
                std::string minFilter = item.isMember("min_filter") ? item["min_filter"].asString() : "";
                std::string magFilter = item.isMember("mag_filter") ? item["mag_filter"].asString() : "";

                texConfig.wrapS = getGLenumFromStr(wrapS);
                texConfig.wrapT = getGLenumFromStr(wrapT);

                if (texConfig.category == "ui") {
                    // UI纹理强制最近邻，不生成mipmap
                    texConfig.minFilter = GL_NEAREST;
                    texConfig.magFilter = GL_NEAREST;
                }
                else {
                    // 方块纹理：若未指定，使用最近邻mipmap最近邻
                    texConfig.minFilter = minFilter.empty() ? GL_NEAREST_MIPMAP_NEAREST : getGLenumFromStr(minFilter);
                    texConfig.magFilter = magFilter.empty() ? GL_NEAREST : getGLenumFromStr(magFilter);
                }

                if (texConfig.category == "block") {
                    blockConfigs.push_back(texConfig);
                }
                else {
                    uiConfigs.push_back(texConfig);
                }
            }
        }

        // 解析立方体贴图
        if (config.isMember("cube_textures") && config["cube_textures"].isArray()) {
            Json::Value& cubeArray = config["cube_textures"];
            for (int i = 0; i < cubeArray.size(); ++i) {
                Json::Value& item = cubeArray[i];
                CubeTextureConfig cubeConfig;
                cubeConfig.name = item.get("name", "").asString();
                if (item.isMember("faces") && item["faces"].isArray()) {
                    Json::Value& facesArray = item["faces"];
                    for (int j = 0; j < facesArray.size(); ++j) {
                        cubeConfig.faces.push_back(facesArray[j].asString());
                    }
                }
                if (cubeConfig.name.empty() || cubeConfig.faces.size() != 6) {
                    std::cerr << "警告：立方体贴图[" << cubeConfig.name << "]无效，跳过" << std::endl;
                    continue;
                }
                std::string wrapS = item.isMember("wrap_s") ? item["wrap_s"].asString() : "clamp_to_edge";
                std::string wrapT = item.isMember("wrap_t") ? item["wrap_t"].asString() : "clamp_to_edge";
                std::string wrapR = item.isMember("wrap_r") ? item["wrap_r"].asString() : "clamp_to_edge";
                std::string minFilter = item.isMember("min_filter") ? item["min_filter"].asString() : "linear";
                std::string magFilter = item.isMember("mag_filter") ? item["mag_filter"].asString() : "linear";
                cubeConfig.wrapS = getGLenumFromStr(wrapS);
                cubeConfig.wrapT = getGLenumFromStr(wrapT);
                cubeConfig.wrapR = getGLenumFromStr(wrapR);
                cubeConfig.minFilter = getGLenumFromStr(minFilter);
                cubeConfig.magFilter = getGLenumFromStr(magFilter);
                cubeConfigs.push_back(cubeConfig);
            }
        }

        // 先加载UI纹理（单独2D纹理）
        for (const auto& config : uiConfigs) {
            loadTexture2D(config);
        }

        // 处理方块纹理：构建纹理数组
        if (!blockConfigs.empty()) {
            // 检查尺寸一致性，取第一个纹理的尺寸
            int texWidth = 0, texHeight = 0;
            std::string firstPath = m_basePath + blockConfigs[0].path;
            unsigned char* firstData = stbi_load(firstPath.c_str(), &texWidth, &texHeight, nullptr, 4);
            if (!firstData) {
                std::cerr << "无法加载第一个方块纹理：" << firstPath << std::endl;
            }
            else {
                stbi_image_free(firstData);
            }

            // 创建纹理数组
            glGenTextures(1, &m_textureArray);
            glBindTexture(GL_TEXTURE_2D_ARRAY, m_textureArray);
            glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, texWidth, texHeight, blockConfigs.size(),
                0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

            // 分配层索引
            std::vector<int> assignedIndices(blockConfigs.size(), -1);
            std::vector<bool> indexUsed(blockConfigs.size(), false);
            for (size_t i = 0; i < blockConfigs.size(); ++i) {
                int idx = blockConfigs[i].index;
                if (idx >= 0 && idx < (int)blockConfigs.size()) {
                    if (!indexUsed[idx]) {
                        assignedIndices[i] = idx;
                        indexUsed[idx] = true;
                    }
                    else {
                        std::cerr << "警告：方块纹理 " << blockConfigs[i].name << " 指定的 index " << idx << " 已被占用，将自动分配" << std::endl;
                    }
                }
            }
            int nextFree = 0;
            for (size_t i = 0; i < blockConfigs.size(); ++i) {
                if (assignedIndices[i] == -1) {
                    while (indexUsed[nextFree]) ++nextFree;
                    assignedIndices[i] = nextFree;
                    indexUsed[nextFree] = true;
                }
            }

            // 加载每个方块纹理到数组，并创建单独的2D纹理
            for (size_t i = 0; i < blockConfigs.size(); ++i) {
                const auto& config = blockConfigs[i];
                int layer = assignedIndices[i];

                std::string fullPath = m_basePath + config.path;
                int w, h, ch;
                unsigned char* data = stbi_load(fullPath.c_str(), &w, &h, &ch, 4);
                if (!data) {
                    std::cerr << "方块纹理加载失败：" << fullPath << std::endl;
                    continue;
                }

                if (w != texWidth || h != texHeight) {
                    std::cerr << "错误：方块纹理 " << config.name << " 尺寸 (" << w << "x" << h << ") 与第一个纹理不一致，跳过" << std::endl;
                    stbi_image_free(data);
                    continue;
                }

                // 上传到纹理数组
                glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer,
                    texWidth, texHeight, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, data);

                // 创建单独的2D纹理（用于其他用途）
                GLuint texID;
                glGenTextures(1, &texID);
                glBindTexture(GL_TEXTURE_2D, texID);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texWidth, texHeight, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, data);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, config.minFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, config.magFilter);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, config.wrapS);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, config.wrapT);
                if (config.minFilter != GL_NEAREST && config.minFilter != GL_LINEAR) {
                    glGenerateMipmap(GL_TEXTURE_2D);
                }
                glBindTexture(GL_TEXTURE_2D, 0);

                TextureInfo info;
                info.width = w;
                info.height = h;
                info.nrChannels = 4;
                info.format = GL_RGBA;
                m_textureInfos[config.name] = info;
                m_textures2D[config.name] = texID;
                m_textureLayerIndex[config.name] = layer;

                stbi_image_free(data);
                std::cout << "方块纹理加载成功：" << config.name << " 层索引 " << layer << std::endl;
            }

            // 设置纹理数组过滤为最近邻 mipmap 最近邻
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        }

        // 加载立方体贴图
        for (const auto& config : cubeConfigs) {
            loadTextureCube(config);
        }

        std::cout << "纹理配置文件解析完成，共加载2D纹理：" << m_textures2D.size()
            << "个，立方体贴图：" << m_texturesCube.size() << "个" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "解析纹理配置文件失败：" << e.what() << std::endl;
    }
}

GLuint TextureMgr::GetTexture2D(const std::string& name) {
    auto it = m_textures2D.find(name);
    if (it != m_textures2D.end()) return it->second;
    std::cerr << "错误：2D纹理[" << name << "]未找到" << std::endl;
    return 0;
}

GLuint TextureMgr::GetTextureCube(const std::string& name) {
    auto it = m_texturesCube.find(name);
    if (it != m_texturesCube.end()) return it->second;
    std::cerr << "错误：立方体贴图[" << name << "]未找到" << std::endl;
    return 0;
}

GLuint TextureMgr::LoadTexture2DManual(const std::string& name, const std::string& path, bool isSRGB) {
    Texture2DConfig config;
    config.name = name;
    config.path = path;
    config.isSRGB = isSRGB;
    config.category = "ui"; // 手动加载默认为UI，可根据需要调整
    return loadTexture2D(config);
}

GLuint TextureMgr::LoadTextureCubeManual(const std::string& name, const std::vector<std::string>& faces) {
    CubeTextureConfig config;
    config.name = name;
    config.faces = faces;
    return loadTextureCube(config);
}

TextureMgr::~TextureMgr() {
    for (auto& pair : m_textures2D) {
        glDeleteTextures(1, &pair.second);
    }
    for (auto& pair : m_texturesCube) {
        glDeleteTextures(1, &pair.second);
    }
    if (m_textureArray) glDeleteTextures(1, &m_textureArray);
    std::cout << "纹理管理器已释放所有纹理资源" << std::endl;
}