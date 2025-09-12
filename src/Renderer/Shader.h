#pragma once
#include <string>
#include <glm/glm.hpp>

class Shader {
public:
    unsigned int ID;
    Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();
    void Bind() const;
    void Unbind() const;
    void SetMat4(const std::string& name, const glm::mat4& matrix) const;
    void SetVec2(const std::string& name, const glm::vec2& vector) const;
    void SetVec3(const std::string& name, const glm::vec3& vector) const;
    void SetFloat(const std::string& name, float value) const;
    void SetInt(const std::string& name, int value) const;
    int GetUniformLocation(const std::string& name) const;

private:
    static std::string ReadFile(const char* path);
    unsigned int Compile(const std::string& vertexSrc, const std::string& fragmentSrc);
};
