#pragma once
#ifndef _SHADER_H_
#define _shADER_H_


#include "core.h"

class Shader
{
public:
    // 构造器读取并构建着色器
    Shader(const std::initializer_list<
        std::pair<GLenum, const char*>>& shaders);
    ~Shader();
    // 使用/激活程序
    void use();
    // 程序programID
    GLuint programID;

    // 命令行覆盖强制重编开关。命令行 --rebuild-shaders / --no-rebuild-shaders 调用它，
    // 优先级高于 runtime_config.json 的 force_recompile_shaders。未调用则由配置决定。
    static void setForceRecompile(bool v) { s_forceRecompileOverride = v ? 1 : 0; }
    // uniform工具函数
    void setBool(const std::string& name, bool value) const;
    void setInt(const std::string& name, int value) const;
    void setFloat(const std::string& name, float value) const;
    void setVec2(const std::string& name, const glm::vec2& value) const;
    void setVec2(const std::string& name, float x, float y) const;
    void setVec3(const std::string& name, const glm::vec3& value) const;
    void setVec3(const std::string& name, float x, float y, float z) const;
    void setVec4(const std::string& name, const glm::vec4& value) const;
    void setVec4(const std::string& name, float x, float y, float z, float w) const;
    void setMat2(const std::string& name, const glm::mat2& mat) const;
    void setMat3(const std::string& name, const glm::mat3& mat) const;
    void setMat4(const std::string& name, const glm::mat4& mat) const;
private:
    // 命令行覆盖：-1 = 未设置（由配置决定），0 = 强制不重编，1 = 强制重编。
    // 命令行优先级高于 runtime_config.json。
    static int s_forceRecompileOverride;

    std::string readShaderSource(const std::string& filePath);
    GLuint createShader(GLenum type, const std::string& filePath);
    GLuint createProgram(std::vector<GLuint> shaders);

    // ---- Program Binary 磁盘缓存 ----
    // 缓存文件名 = 本程序所有着色器路径拼接后的 FNV-1a 哈希（cache/shaders/<hash>.bin）
    std::string cachePathFor(const std::initializer_list<
        std::pair<GLenum, const char*>>& shaders) const;
    // 命中并成功灌入 programID 返回 true；文件缺失/驱动变更/校验失败返回 false
    bool loadProgramBinary(const std::string& path);
    // 把当前已链接的 programID 取出二进制写入磁盘
    void saveProgramBinary(const std::string& path) const;
};

#endif // !_SHADER_H——