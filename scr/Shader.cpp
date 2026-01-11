#include "Shader.h"

Shader::Shader(const std::initializer_list<
    std::pair<GLenum, const char*>>& shaders)
{
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
        std::cerr << "Shader编译失败：" << info << std::endl;
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
    glLinkProgram(programID);
    GLint success;
    glGetProgramiv(programID, GL_LINK_STATUS, &success);
    if (!success) {
        char info[512];
        glGetProgramInfoLog(programID, 512, NULL, info);
        std::cerr << "Program链接失败：" << info << std::endl;
        glDeleteProgram(programID);
        return 0;
    }
    for (auto shader : shaders) {
        glDetachShader(programID, shader);
        glDeleteShader(shader);
    }
	std::cout << "Shader Program " << programID << " created successfully." << std::endl;
    return programID;
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
