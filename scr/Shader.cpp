#include "Shader.h"
#include "RuntimeConfig.h"
#include <filesystem>
#include <cstdint>
#include <cstdio>

// 命令行覆盖，默认 -1（未设置，由 runtime_config.json 决定）
int Shader::s_forceRecompileOverride = -1;

Shader::Shader(const std::initializer_list<
    std::pair<GLenum, const char*>>& shaders)
{
    // 缓存文件名由各着色器路径拼接哈希得到
    std::string cachePath = cachePathFor(shaders);

    // 是否强制重编：命令行显式设置优先，否则读配置
    bool forceRecompile = (s_forceRecompileOverride >= 0)
        ? (s_forceRecompileOverride != 0)
        : RuntimeConfig::get().forceRecompileShaders;

    if (forceRecompile) {
        // 强制重编：删掉旧缓存，直接走源码编译分支
        std::error_code ec;
        std::filesystem::remove(cachePath, ec);
    } else if (loadProgramBinary(cachePath)) {
        // 命中磁盘缓存且校验通过，programID 已就绪，跳过编译链接
        return;
    }
    // 未命中 / 强制重编 / 加载失败：从源码编译链接
    std::vector<GLuint> shaderObjects;
    for (const auto& pair : shaders) {
        GLenum type = pair.first;
        const char* path = pair.second;
        GLuint shader = createShader(type, path);
        if (shader != 0) {
            shaderObjects.push_back(shader);
        }
        else {
            for (GLuint s : shaderObjects)
                glDeleteShader(s);
            programID = 0;
            return;
        }
    }
    programID = createProgram(shaderObjects);

    // 编译链接成功则写回磁盘缓存，供下次直接加载
    if (programID != 0) {
        saveProgramBinary(cachePath);
    }
}
Shader::~Shader()
{
    if (programID != 0) {
        glDeleteProgram(programID);
    }
}


// 调用着色器程序
void Shader::use()
{
    if (programID != 0) {
        glUseProgram(programID);
    }
    else {
        std::cout << "程序:" << programID << "启动失败\n";
    }
}

// 读取Shader文件内容，返回字符串
std::string Shader::readShaderSource(const std::string& filePath) {
    std::ifstream shaderFile;
    // 确保ifstream对象可以抛出异常
    shaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    try {
        // 打开Shader文件（注意路径：对应你截图里的shader文件夹，路径是"shader/xxx.frag"）
        shaderFile.open(filePath);
        // 用stringstream读取文件内容
        std::stringstream shaderStream;
        shaderStream << shaderFile.rdbuf(); // 一次性读取所有内容
        shaderFile.close(); // 关闭文件
        // 返回读取到的字符串
        return shaderStream.str();
    }
    catch (std::ifstream::failure& e) {
        std::cerr << "ERROR::SHADER::FILE_NOT_SUCCESFULLY_READ: " << e.what() << std::endl;
        return nullptr; // 读取失败返回空字符串
    }
}

// 编译Shader
GLuint Shader::createShader(GLenum type, const std::string& filePath) {

    std::string shaderSourceStr = readShaderSource(filePath);
    if (shaderSourceStr.empty()) {
        std::cerr << "[SHADER ABORT] program build aborted at: " << filePath << std::endl;
        return 0;
    }
    const char* source = shaderSourceStr.c_str();

    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char info[512];
        glGetShaderInfoLog(shader, 512, NULL, info);
        // 调试：编译失败无条件打印 + 文件名，便于定位坏 shader
        std::cerr << "[COMPILE FAIL] " << filePath << ": " << info << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// 创建Program
GLuint Shader::createProgram(std::vector<GLuint> shaders) {
    programID = glCreateProgram();
    for (auto shader : shaders) {
        glAttachShader(programID, shader);
    }
    // 提示驱动保留可取回的二进制，否则部分驱动 glGetProgramBinary 会失败/返回 0 长度
    glProgramParameteri(programID, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glLinkProgram(programID);
    GLint success;
    glGetProgramiv(programID, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(programID, 512, NULL, info);
        // 调试：链接失败无条件打印（不再受 verboseShaderLoading 门控），便于定位坏 program
        std::cerr << "[LINK FAIL] program " << programID << ": " << info << std::endl;
        glDeleteProgram(programID);
        return 0;
    }
    for (auto shader : shaders) {
        glDetachShader(programID, shader);
        glDeleteShader(shader);
    }
	if (RuntimeConfig::get().verboseShaderLoading)
		std::cout << "Shader Program " << programID << " created successfully." << std::endl;
    return programID;
}






// ============================================================================
// Program Binary 磁盘缓存
// ============================================================================

// 当前驱动/GPU 签名：program binary 是驱动+GPU 专属的，换驱动/换卡后旧缓存不可用。
// 把签名写进缓存文件头，加载时比对，不一致即视为失效并回退源码编译。
static std::string driverSignature() {
    auto q = [](GLenum e) -> std::string {
        const GLubyte* p = glGetString(e);
        return p ? std::string(reinterpret_cast<const char*>(p)) : std::string();
    };
    return q(GL_VENDOR) + "|" + q(GL_RENDERER) + "|" + q(GL_VERSION);
}

// 缓存文件名 = 各着色器路径拼接后的 FNV-1a 64 位哈希。
// 注意：只哈希“文件名”，不哈希文件内容 —— 改了着色器源码但不改名，缓存键不变，
// 需用 --rebuild-shaders 强制重编（见类注释）。
std::string Shader::cachePathFor(const std::initializer_list<
    std::pair<GLenum, const char*>>& shaders) const
{
    uint64_t h = 1469598103934665603ull;  // FNV offset basis
    for (const auto& p : shaders) {
        for (const char* s = p.second; s && *s; ++s) {
            h ^= static_cast<unsigned char>(*s);
            h *= 1099511628211ull;         // FNV prime
        }
        h ^= static_cast<unsigned char>('|'); // 分隔符，避免不同拼接产生同哈希
        h *= 1099511628211ull;
    }
    char name[24];
    std::snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(h));
    return std::string("cache/shaders/") + name + ".bin";
}

// 缓存文件格式：magic(4) | sigLen(4) | sig | format(4) | blobLen(4) | blob
static const uint32_t kShaderCacheMagic = 0x31435349u; // "ISC1"

bool Shader::loadProgramBinary(const std::string& path)
{
    // 驱动是否支持 program binary
    GLint numFormats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numFormats);
    if (numFormats == 0) return false;

    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return false;

    uint32_t magic = 0, sigLen = 0, format = 0, blobLen = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    if (!f.good() || magic != kShaderCacheMagic) return false;

    f.read(reinterpret_cast<char*>(&sigLen), 4);
    if (!f.good() || sigLen > 4096) return false;
    std::string sig(sigLen, '\0');
    f.read(sig.data(), sigLen);
    if (!f.good() || sig != driverSignature()) return false;  // 驱动/GPU 变更 → 失效

    f.read(reinterpret_cast<char*>(&format), 4);
    f.read(reinterpret_cast<char*>(&blobLen), 4);
    if (!f.good() || blobLen == 0) return false;

    std::vector<char> blob(blobLen);
    f.read(blob.data(), blobLen);
    if (!f.good()) return false;

    GLuint prog = glCreateProgram();
    glProgramBinary(prog, static_cast<GLenum>(format), blob.data(),
                    static_cast<GLsizei>(blobLen));
    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        // 二进制被驱动拒绝（版本/GPU 微差异）→ 回退源码编译
        glDeleteProgram(prog);
        return false;
    }
    programID = prog;
    if (RuntimeConfig::get().verboseShaderLoading)
        std::cout << "Shader loaded from cache: " << path << std::endl;
    return true;
}

void Shader::saveProgramBinary(const std::string& path) const
{
    GLint numFormats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &numFormats);
    if (numFormats == 0) return;  // 驱动不支持则不缓存

    GLint length = 0;
    glGetProgramiv(programID, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0) return;

    std::vector<char> blob(length);
    GLenum format = 0;
    GLsizei written = 0;
    glGetProgramBinary(programID, length, &written, &format, blob.data());
    if (written <= 0) return;

    std::error_code ec;
    std::filesystem::create_directories("cache/shaders", ec);

    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.good()) return;

    std::string sig = driverSignature();
    uint32_t sigLen = static_cast<uint32_t>(sig.size());
    uint32_t fmt = static_cast<uint32_t>(format);
    uint32_t blen = static_cast<uint32_t>(written);
    f.write(reinterpret_cast<const char*>(&kShaderCacheMagic), 4);
    f.write(reinterpret_cast<const char*>(&sigLen), 4);
    f.write(sig.data(), sigLen);
    f.write(reinterpret_cast<const char*>(&fmt), 4);
    f.write(reinterpret_cast<const char*>(&blen), 4);
    f.write(blob.data(), written);
}

void Shader::setBool(const std::string& name, bool value) const
{
    glUniform1i(glGetUniformLocation(programID, name.c_str()), (int)value);
}

void Shader::setInt(const std::string& name, int value) const
{
    glUniform1i(glGetUniformLocation(programID, name.c_str()), value);
}

void Shader::setFloat(const std::string& name, float value) const
{
    glUniform1f(glGetUniformLocation(programID, name.c_str()), value);
}

void Shader::setVec2(const std::string& name, const glm::vec2& value) const
{
    glUniform2fv(glGetUniformLocation(programID, name.c_str()), 1, &value[0]);
}
void Shader::setVec2(const std::string& name, float x, float y) const
{
    glUniform2f(glGetUniformLocation(programID, name.c_str()), x, y);
}

void Shader::setVec3(const std::string& name, const glm::vec3& value) const
{
    glUniform3fv(glGetUniformLocation(programID, name.c_str()), 1, &value[0]);
}
void Shader::setVec3(const std::string& name, float x, float y, float z) const
{
    glUniform3f(glGetUniformLocation(programID, name.c_str()), x, y, z);
}

void Shader::setVec4(const std::string& name, const glm::vec4& value) const
{
    glUniform4fv(glGetUniformLocation(programID, name.c_str()), 1, &value[0]);
}
void Shader::setVec4(const std::string& name, float x, float y, float z, float w) const
{
    glUniform4f(glGetUniformLocation(programID, name.c_str()), x, y, z, w);
}

void Shader::setMat2(const std::string& name, const glm::mat2& mat) const
{
    glUniformMatrix2fv(glGetUniformLocation(programID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}

void Shader::setMat3(const std::string& name, const glm::mat3& mat) const
{
    glUniformMatrix3fv(glGetUniformLocation(programID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}

void Shader::setMat4(const std::string& name, const glm::mat4& mat) const
{
    glUniformMatrix4fv(glGetUniformLocation(programID, name.c_str()), 1, GL_FALSE, &mat[0][0]);
}
