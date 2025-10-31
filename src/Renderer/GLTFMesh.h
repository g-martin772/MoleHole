#pragma once
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace tinygltf {
    class Model;
    class Node;
}

class Shader;

struct GLTFPrimitive {
    unsigned int m_VAO;
    unsigned int m_VBO;
    unsigned int m_EBO;
    unsigned int m_indexCount;
    unsigned int m_indexType; // GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT, or GL_UNSIGNED_INT
    unsigned int m_indexOffsetBytes;
    int m_baseVertex;
    int m_materialIndex;

    GLTFPrimitive();
    ~GLTFPrimitive();

    GLTFPrimitive(const GLTFPrimitive&) = delete;
    GLTFPrimitive& operator=(const GLTFPrimitive&) = delete;

    GLTFPrimitive(GLTFPrimitive&& other) noexcept;
    GLTFPrimitive& operator=(GLTFPrimitive&& other) noexcept;

    void Cleanup();
};

struct GLTFMaterial {
    glm::vec4 m_baseColorFactor;
    float m_metallicFactor;
    float m_roughnessFactor;
    unsigned int m_baseColorTexture;
    bool m_hasBaseColorTexture;
    bool m_hasTransparency;

    GLTFMaterial();
};

class GLTFMesh {
public:
    GLTFMesh();
    ~GLTFMesh();

    bool Load(const std::string& path);
    void Render(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& cameraPos);
    void Cleanup();

    void SetPosition(const glm::vec3& position) { m_position = position; }
    void SetRotation(const glm::quat& rotation) { m_rotation = rotation; }
    void SetScale(const glm::vec3& scale) { m_scale = scale; }

    glm::vec3 GetPosition() const { return m_position; }
    glm::quat GetRotation() const { return m_rotation; }
    glm::vec3 GetScale() const { return m_scale; }

    glm::mat4 GetTransform() const;

    bool IsLoaded() const { return m_loaded; }
    std::string GetPath() const { return m_path; }

private:
    void ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const glm::mat4& parentTransform);
    void ProcessMesh(const tinygltf::Model& model, int meshIndex, const glm::mat4& transform);
    void LoadMaterials(const tinygltf::Model& model);
    unsigned int LoadTextureFromModel(const tinygltf::Model& model, int textureIndex);

    std::vector<GLTFPrimitive> m_primitives;
    std::vector<GLTFMaterial> m_materials;
    std::unique_ptr<Shader> m_shader;

    glm::vec3 m_position;
    glm::quat m_rotation;
    glm::vec3 m_scale;

    std::string m_path;
    bool m_loaded;

    struct TempCachePrim {
        int materialIndex;
        unsigned int indexType;
        unsigned int indexCount;
        std::vector<float> vertex;
        std::vector<unsigned char> indices;
    };
    std::vector<TempCachePrim> m_tempCache;

    unsigned int m_sharedVAO = 0;
    unsigned int m_sharedVBO = 0;
    unsigned int m_sharedEBO = 0;
    bool m_useSharedBuffers = false;
};
