#include "Shader.h"
#include <fstream>
#include <sstream>
#include <glad/gl.h>
#include <spdlog/spdlog.h>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <iomanip>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <chrono>

std::string Shader::ReadFile(const char* path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::error("Failed to open shader file: {}", path);
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string Shader::GetCacheDir() {
    return ".shader_cache";
}

void Shader::EnsureCacheDirExists() {
    namespace fs = std::filesystem;
    fs::path cacheDir = GetCacheDir();
    
    if (!fs::exists(cacheDir)) {
        std::error_code ec;
        if (!fs::create_directories(cacheDir, ec)) {
            spdlog::warn("Failed to create shader cache directory: {}, error: {}", cacheDir.string(), ec.message());
        }
    } else if (!fs::is_directory(cacheDir)) {
        spdlog::error("Shader cache path exists but is not a directory: {}", cacheDir.string());
    }
}

std::string Shader::ComputeHash(const std::string& data) {
    uint64_t hash = 5381;
    for (char c : data) {
        hash = ((hash << 5) + hash) + static_cast<uint64_t>(c);
    }
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << hash;
    return ss.str();
}

int64_t Shader::GetFileModTime(const char* path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto ftime = fs::last_write_time(path, ec);
    if (ec) {
        spdlog::warn("Failed to get modification time for {}: {}", path, ec.message());
        return 0;
    }
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    return std::chrono::system_clock::to_time_t(sctp);
}

std::string Shader::GetCachePath(const std::string& key) {
    return GetCacheDir() + "/" + key + ".bin";
}

bool Shader::IsCacheValid(const std::string& cacheKey, const char* path1, const char* path2) {
    const std::string cachePath = GetCachePath(cacheKey);
    const std::string metaPath = cachePath + ".meta";
    
    std::ifstream cacheFile(cachePath, std::ios::binary);
    std::ifstream metaFile(metaPath);
    if (!cacheFile.good() || !metaFile.good()) {
        return false;
    }
    
    int64_t storedTime1 = 0, storedTime2 = 0;
    metaFile >> storedTime1;
    if (path2 != nullptr) {
        metaFile >> storedTime2;
    }
    
    const int64_t currentTime1 = GetFileModTime(path1);
    const int64_t currentTime2 = path2 ? GetFileModTime(path2) : 0;
    
    return (currentTime1 == storedTime1) && (!path2 || currentTime2 == storedTime2);
}

bool Shader::LoadCachedProgram(unsigned int& program, const std::string& cacheKey) {
    std::ifstream file(GetCachePath(cacheKey), std::ios::binary);
    if (!file.good()) {
        return false;
    }
    
    GLenum binaryFormat;
    GLsizei binaryLength;
    file.read(reinterpret_cast<char*>(&binaryFormat), sizeof(GLenum));
    file.read(reinterpret_cast<char*>(&binaryLength), sizeof(GLsizei));
    
    if (!file.good() || binaryLength <= 0 || binaryLength > 50 * 1024 * 1024) {
        spdlog::warn("Invalid cached shader binary metadata");
        return false;
    }
    
    std::vector<char> binary(binaryLength);
    file.read(binary.data(), binaryLength);
    
    if (!file.good()) {
        spdlog::warn("Failed to read cached shader binary");
        return false;
    }
    
    program = glCreateProgram();
    glProgramBinary(program, binaryFormat, binary.data(), binaryLength);
    
    GLint success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glDeleteProgram(program);
        return false;
    }
    
    return true;
}

void Shader::SaveCachedProgram(unsigned int program, const std::string& cacheKey) {
    EnsureCacheDirExists();
    
    GLsizei binaryLength;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    std::vector<char> binary(binaryLength);
    GLenum binaryFormat;
    glGetProgramBinary(program, binaryLength, nullptr, &binaryFormat, binary.data());
    
    std::ofstream file(GetCachePath(cacheKey), std::ios::binary);
    if (file.is_open()) {
        file.write(reinterpret_cast<const char*>(&binaryFormat), sizeof(GLenum));
        file.write(reinterpret_cast<const char*>(&binaryLength), sizeof(GLsizei));
        file.write(binary.data(), binaryLength);
    }
}

unsigned int Shader::CompileWithCache(const char* vertexPath, const char* fragmentPath) {
    const std::string cacheKey = ComputeHash(std::string(vertexPath) + "|" + fragmentPath);
    
    if (IsCacheValid(cacheKey, vertexPath, fragmentPath)) {
        const auto tStart = std::chrono::steady_clock::now();
        unsigned int program;
        if (LoadCachedProgram(program, cacheKey)) {
            const auto tEnd = std::chrono::steady_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
            spdlog::info("Loaded shader from cache in {} ms: {} + {}", ms, vertexPath, fragmentPath);
            return program;
        }
    }
    
    spdlog::debug("Compiling shader from source: {} + {}", vertexPath, fragmentPath);
    const auto tStart = std::chrono::steady_clock::now();
    const unsigned int program = Compile(ReadFile(vertexPath), ReadFile(fragmentPath));
    const auto tEnd = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
    spdlog::info("Compiled shader in {} ms: {} + {}", ms, vertexPath, fragmentPath);
    
    SaveCachedProgram(program, cacheKey);
    
    std::ofstream metaFile(GetCachePath(cacheKey) + ".meta");
    if (metaFile.is_open()) {
        metaFile << GetFileModTime(vertexPath) << "\n" << GetFileModTime(fragmentPath) << "\n";
    }
    
    return program;
}

unsigned int Shader::CompileComputeWithCache(const char* computePath) {
    const std::string cacheKey = ComputeHash(computePath);
    
    if (IsCacheValid(cacheKey, computePath)) {
        const auto tStart = std::chrono::steady_clock::now();
        unsigned int program;
        if (LoadCachedProgram(program, cacheKey)) {
            const auto tEnd = std::chrono::steady_clock::now();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
            spdlog::info("Loaded compute shader from cache in {} ms: {}", ms, computePath);
            return program;
        }
    }
    
    spdlog::debug("Compiling compute shader from source: {}", computePath);
    const auto tStart = std::chrono::steady_clock::now();
    const unsigned int program = CompileCompute(ReadFile(computePath));
    const auto tEnd = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();
    spdlog::info("Compiled compute shader in {} ms: {}", ms, computePath);
    
    SaveCachedProgram(program, cacheKey);
    
    std::ofstream metaFile(GetCachePath(cacheKey) + ".meta");
    if (metaFile.is_open()) {
        metaFile << GetFileModTime(computePath) << "\n";
    }
    
    return program;
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
    ID = CompileWithCache(vertexPath, fragmentPath);
}

Shader::Shader(const std::string& computeSrc, bool isCompute) {
    ID = isCompute ? CompileCompute(computeSrc) : 0;
}

Shader::Shader(const char* computePath, bool isCompute) {
    ID = isCompute ? CompileComputeWithCache(computePath) : 0;
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
