#include "Shader.h"
#include <fstream>
#include <sstream>
#include <glad/gl.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>

std::string Shader::ReadFile(const char* path) {
    std::ifstream file(path);
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

unsigned int Shader::Compile(const std::string& vertexSrc, const std::string& fragmentSrc) {
    unsigned int vertex = glCreateShader(GL_VERTEX_SHADER);
    const char* vsrc = vertexSrc.c_str();
    glShaderSource(vertex, 1, &vsrc, nullptr);
    glCompileShader(vertex);
    int success;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(vertex, 1024, nullptr, infoLog);
        spdlog::error("Vertex shader compilation failed: {}", infoLog);
    }
    unsigned int fragment = glCreateShader(GL_FRAGMENT_SHADER);
    const char* fsrc = fragmentSrc.c_str();
    glShaderSource(fragment, 1, &fsrc, nullptr);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(fragment, 1024, nullptr, infoLog);
        spdlog::error("Fragment shader compilation failed: {}", infoLog);
    }
    unsigned int program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
        spdlog::error("Shader program linking failed: {}", infoLog);
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

unsigned int Shader::CompileCompute(const std::string& computeSrc) {
    unsigned int compute = glCreateShader(GL_COMPUTE_SHADER);
    const char* csrc = computeSrc.c_str();
    glShaderSource(compute, 1, &csrc, nullptr);
    glCompileShader(compute);
    int success;
    glGetShaderiv(compute, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetShaderInfoLog(compute, 1024, nullptr, infoLog);
        spdlog::error("Compute shader compilation failed: {}", infoLog);
    }
    unsigned int program = glCreateProgram();
    glAttachShader(program, compute);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024];
        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
        spdlog::error("Compute shader program linking failed: {}", infoLog);
    }
    glDeleteShader(compute);
    return program;
}

Shader::Shader(const std::string& vertexSrc, const std::string& fragmentSrc) {
    ID = Compile(vertexSrc, fragmentSrc);
}

Shader::Shader(const char* vertexPath, const char* fragmentPath) {
    std::string vert = ReadFile(vertexPath);
    std::string frag = ReadFile(fragmentPath);
    ID = Compile(vert, frag);
}

Shader::Shader(const std::string& computeSrc, bool isCompute) {
    if (isCompute)
        ID = CompileCompute(computeSrc);
    else
        ID = 0;
}

Shader::Shader(const char* computePath, bool isCompute) {
    if (isCompute) {
        std::string comp = ReadFile(computePath);
        ID = CompileCompute(comp);
    } else {
        ID = 0;
    }
}

Shader::~Shader() {
    glDeleteProgram(ID);
}
void Shader::Bind() const {
    glUseProgram(ID);
}
void Shader::Unbind() const {
    glUseProgram(0);
}

void Shader::SetMat4(const std::string& name, const glm::mat4& matrix) const {
    glUniformMatrix4fv(GetUniformLocation(name), 1, GL_FALSE, glm::value_ptr(matrix));
}

void Shader::SetVec2(const std::string& name, const glm::vec2& vector) const {
    glUniform2fv(GetUniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::SetVec3(const std::string& name, const glm::vec3& vector) const {
    glUniform3fv(GetUniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::SetVec4(const std::string& name, const glm::vec4& vector) const {
    glUniform4fv(GetUniformLocation(name), 1, glm::value_ptr(vector));
}

void Shader::SetFloat(const std::string& name, float value) const {
    glUniform1f(GetUniformLocation(name), value);
}

void Shader::SetInt(const std::string& name, int value) const {
    glUniform1i(GetUniformLocation(name), value);
}

void Shader::Dispatch(uint32_t x, uint32_t y, uint32_t z) const {
    glUseProgram(ID);
    glDispatchCompute(x, y, z);
    glUseProgram(0);
}

int Shader::GetUniformLocation(const std::string& name) const {
    return glGetUniformLocation(ID, name.c_str());
}
