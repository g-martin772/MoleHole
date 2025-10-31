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
    // Simple hash function (djb2) using fixed-size type
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
    // Convert to time_t for consistent timestamp
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
    );
    return std::chrono::system_clock::to_time_t(sctp);
}

std::string Shader::GetCachePath(const std::string& key) {
    return GetCacheDir() + "/" + key + ".bin";
}

bool Shader::IsCacheValid(const std::string& cacheKey, const char* path1, const char* path2) {
    std::string cachePath = GetCachePath(cacheKey);
    std::string metaPath = cachePath + ".meta";
    
    // Check if cache files exist
    std::ifstream cacheFile(cachePath, std::ios::binary);
    std::ifstream metaFile(metaPath);
    if (!cacheFile.good() || !metaFile.good()) {
        return false;
    }
    
    // Read stored timestamps from meta file
    int64_t storedTime1 = 0, storedTime2 = 0;
    metaFile >> storedTime1;
    if (path2 != nullptr) {
        metaFile >> storedTime2;
    }
    metaFile.close();
    
    // Get current file timestamps
    int64_t currentTime1 = GetFileModTime(path1);
    int64_t currentTime2 = path2 ? GetFileModTime(path2) : 0;
    
    // Cache is valid if timestamps match
    bool valid = (currentTime1 == storedTime1);
    if (path2 != nullptr) {
        valid = valid && (currentTime2 == storedTime2);
    }
    
    return valid;
}

bool Shader::LoadCachedProgram(unsigned int& program, const std::string& cacheKey) {
    std::string cachePath = GetCachePath(cacheKey);
    
    std::ifstream file(cachePath, std::ios::binary);
    if (!file.good()) {
        return false;
    }
    
    // Read binary format and size
    GLenum binaryFormat;
    GLsizei binaryLength;
    file.read(reinterpret_cast<char*>(&binaryFormat), sizeof(GLenum));
    file.read(reinterpret_cast<char*>(&binaryLength), sizeof(GLsizei));
    
    if (!file.good() || binaryLength <= 0 || binaryLength > 50 * 1024 * 1024) { // 50MB max
        spdlog::warn("Invalid cached shader binary metadata: {}", cachePath);
        return false;
    }
    
    // Read binary data
    std::vector<char> binary(binaryLength);
    file.read(binary.data(), binaryLength);
    file.close();
    
    if (!file.good()) {
        spdlog::warn("Failed to read cached shader binary: {}", cachePath);
        return false;
    }
    
    // Create program and load binary
    program = glCreateProgram();
    glProgramBinary(program, binaryFormat, binary.data(), binaryLength);
    
    // Check if program loaded successfully
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
    
    // Get program binary
    GLsizei binaryLength;
    glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    
    std::vector<char> binary(binaryLength);
    GLenum binaryFormat;
    glGetProgramBinary(program, binaryLength, nullptr, &binaryFormat, binary.data());
    
    // Save binary to file
    std::string cachePath = GetCachePath(cacheKey);
    std::ofstream file(cachePath, std::ios::binary);
    if (file.good()) {
        file.write(reinterpret_cast<const char*>(&binaryFormat), sizeof(GLenum));
        file.write(reinterpret_cast<const char*>(&binaryLength), sizeof(GLsizei));
        file.write(binary.data(), binaryLength);
        file.close();
    }
}

unsigned int Shader::CompileWithCache(const char* vertexPath, const char* fragmentPath) {
    // Generate cache key from file paths and timestamps
    std::string cacheKeyData = std::string(vertexPath) + "|" + std::string(fragmentPath);
    std::string cacheKey = ComputeHash(cacheKeyData);
    
    // Try to load from cache
    if (IsCacheValid(cacheKey, vertexPath, fragmentPath)) {
        unsigned int program;
        if (LoadCachedProgram(program, cacheKey)) {
            spdlog::debug("Loaded shader from cache: {} + {}", vertexPath, fragmentPath);
            return program;
        }
    }
    
    // Cache miss or invalid - compile from source
    spdlog::debug("Compiling shader from source: {} + {}", vertexPath, fragmentPath);
    std::string vert = ReadFile(vertexPath);
    std::string frag = ReadFile(fragmentPath);
    unsigned int program = Compile(vert, frag);
    
    // Save to cache with timestamps
    SaveCachedProgram(program, cacheKey);
    
    // Save metadata (timestamps)
    std::string metaPath = GetCachePath(cacheKey) + ".meta";
    std::ofstream metaFile(metaPath);
    if (metaFile.good()) {
        metaFile << GetFileModTime(vertexPath) << "\n";
        metaFile << GetFileModTime(fragmentPath) << "\n";
        metaFile.close();
    }
    
    return program;
}

unsigned int Shader::CompileComputeWithCache(const char* computePath) {
    // Generate cache key from file path and timestamp
    std::string cacheKeyData = std::string(computePath);
    std::string cacheKey = ComputeHash(cacheKeyData);
    
    // Try to load from cache
    if (IsCacheValid(cacheKey, computePath)) {
        unsigned int program;
        if (LoadCachedProgram(program, cacheKey)) {
            spdlog::debug("Loaded compute shader from cache: {}", computePath);
            return program;
        }
    }
    
    // Cache miss or invalid - compile from source
    spdlog::debug("Compiling compute shader from source: {}", computePath);
    std::string comp = ReadFile(computePath);
    unsigned int program = CompileCompute(comp);
    
    // Save to cache with timestamp
    SaveCachedProgram(program, cacheKey);
    
    // Save metadata (timestamp)
    std::string metaPath = GetCachePath(cacheKey) + ".meta";
    std::ofstream metaFile(metaPath);
    if (metaFile.good()) {
        metaFile << GetFileModTime(computePath) << "\n";
        metaFile.close();
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
    // Direct source compilation (no caching for source strings)
    ID = Compile(vertexSrc, fragmentSrc);
}

Shader::Shader(const char* vertexPath, const char* fragmentPath) {
    // Use caching for file-based shaders
    ID = CompileWithCache(vertexPath, fragmentPath);
}

Shader::Shader(const std::string& computeSrc, bool isCompute) {
    // Direct source compilation (no caching for source strings)
    if (isCompute)
        ID = CompileCompute(computeSrc);
    else
        ID = 0;
}

Shader::Shader(const char* computePath, bool isCompute) {
    // Use caching for file-based compute shaders
    if (isCompute) {
        ID = CompileComputeWithCache(computePath);
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
