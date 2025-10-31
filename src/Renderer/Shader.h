#pragma once
#include <string>
#include <glm/glm.hpp>
#include <cstdint>

class Shader {
public:
    unsigned int ID;
    Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
    Shader(const char* vertexPath, const char* fragmentPath);
    Shader(const std::string& computeSrc, bool isCompute);
    Shader(const char* computePath, bool isCompute);
    ~Shader();
    void Bind() const;
    void Unbind() const;
    void Dispatch(uint32_t x, uint32_t y = 1, uint32_t z = 1) const;
    void SetMat4(const std::string& name, const glm::mat4& matrix) const;
    void SetVec2(const std::string& name, const glm::vec2& vector) const;
    void SetVec3(const std::string& name, const glm::vec3& vector) const;
    void SetVec4(const std::string& name, const glm::vec4& vector) const;
    void SetFloat(const std::string& name, float value) const;
    void SetInt(const std::string& name, int value) const;
    int GetUniformLocation(const std::string& name) const;

private:
    static std::string ReadFile(const char* path);
    unsigned int Compile(const std::string& vertexSrc, const std::string& fragmentSrc);
    unsigned int CompileCompute(const std::string& computeSrc);
    
    // Shader caching functions
    static std::string GetCacheDir();
    static void EnsureCacheDirExists();
    static std::string ComputeHash(const std::string& data);
    static int64_t GetFileModTime(const char* path);
    static std::string GetCachePath(const std::string& key);
    static bool LoadCachedProgram(unsigned int& program, const std::string& cacheKey);
    static void SaveCachedProgram(unsigned int program, const std::string& cacheKey);
    static bool IsCacheValid(const std::string& cacheKey, const char* path1, const char* path2 = nullptr);
    
    unsigned int CompileWithCache(const char* vertexPath, const char* fragmentPath);
    unsigned int CompileComputeWithCache(const char* computePath);
};
