#pragma once
// #include "../Interface/Shader.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanImage.h"

namespace tinygltf {
    class Model;
    class Node;
}

struct GLTFPrimitive {
    std::unique_ptr<VulkanBuffer> m_VertexBuffer;
    std::unique_ptr<VulkanBuffer> m_IndexBuffer;
    
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
    std::shared_ptr<VulkanImage> m_baseColorTexture;
    bool m_hasBaseColorTexture;
    bool m_hasTransparency;
    vk::DescriptorSet m_descriptorSet;

    GLTFMaterial();
};

class GLTFMesh {
public:
    GLTFMesh();
    ~GLTFMesh();

    bool Load(const std::string& path, VulkanDevice* device, vk::DescriptorSetLayout textureLayout);
    void Render(vk::CommandBuffer cmd, vk::PipelineLayout layout);
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

    struct PhysicsGeometry {
        std::vector<glm::vec3> vertices;
        std::vector<unsigned int> indices;
    };
    PhysicsGeometry GetPhysicsGeometry() const;

    const std::vector<GLTFPrimitive>& GetPrimitives() const { return m_primitives; }

private:
    void ProcessNode(const tinygltf::Model& model, const tinygltf::Node& node, const glm::mat4& parentTransform);
    void ProcessMesh(const tinygltf::Model& model, int meshIndex, const glm::mat4& transform);
    void LoadMaterials(const tinygltf::Model& model);
    std::shared_ptr<VulkanImage> LoadTextureFromModel(const tinygltf::Model& model, int textureIndex);

    std::vector<GLTFPrimitive> m_primitives;
    std::vector<GLTFMaterial> m_materials;
    // Shader is managed externally or via pipeline
    // std::unique_ptr<Shader> m_shader; 

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
    
    VulkanDevice* m_Device = nullptr;
    vk::DescriptorPool m_DescriptorPool;
    vk::DescriptorSetLayout m_TextureLayout;
};
