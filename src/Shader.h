#pragma once
#include <string>

class Shader {
public:
    unsigned int ID;
    Shader(const std::string& vertexSrc, const std::string& fragmentSrc);
    Shader(const char* vertexPath, const char* fragmentPath);
    ~Shader();
    void Bind() const;
    void Unbind() const;

private:
    static std::string ReadFile(const char* path);
    unsigned int Compile(const std::string& vertexSrc, const std::string& fragmentSrc);
};
