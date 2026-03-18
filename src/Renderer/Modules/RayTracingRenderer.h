#pragma once

#include "Platform/Vulkan/VulkanDevice.h"
#include "Platform/Vulkan/VulkanRayTracingPipeline.h"
#include "Platform/Vulkan/VulkanAccelerationStructure.h"
#include "Platform/Vulkan/VulkanImage.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include <vulkan/vulkan.hpp>
#include <memory>

#include <unordered_map>
#include <string>

class Scene;
class Camera;
class GLTFMesh;

class RayTracingRenderer {
public:
    RayTracingRenderer();
    ~RayTracingRenderer();

    void Init(VulkanDevice* device, int width, int height);
    void Render(const Scene& scene, const Camera& camera, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache, vk::CommandBuffer cmd);
    void Resize(int width, int height);
    void Shutdown();

    // For displaying the result
    std::shared_ptr<VulkanImage> GetOutputImage() const { return m_outputImage; }
    vk::DescriptorSet GetImGuiDescriptorSet();

    void RenderUI();

    void CreateAccelerationStructures(const Scene& scene, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache, vk::CommandBuffer cmd); // Need cmd for building

private:
    void CreateOutputImage();
    void CreatePipeline();
    void CreateDescriptorSets();
    void UpdateUniformBuffer(const Camera& camera, const Scene& scene);
    
    // Helper for matrix conversion
    vk::TransformMatrixKHR ToVkTransform(const glm::mat4& matrix);

    bool m_initialized = false;
    VulkanDevice* m_device = nullptr;
    int m_width = 0;
    int m_height = 0;

    // Ray Tracing Components
    std::unique_ptr<VulkanAccelerationStructure> m_tlas;
    std::unordered_map<GLTFMesh*, std::unique_ptr<VulkanAccelerationStructure>> m_blasCache;
    std::unique_ptr<VulkanBuffer> m_instanceBuffer;

    std::unique_ptr<VulkanRayTracingPipeline> m_pipeline;

    // Resources
    std::shared_ptr<VulkanImage> m_outputImage;
    std::unique_ptr<VulkanBuffer> m_cameraUBO;

    // Descriptors
    vk::DescriptorPool m_descriptorPool;
    vk::DescriptorSetLayout m_descriptorSetLayout;
    vk::DescriptorSet m_descriptorSet;
    vk::DescriptorSet m_imGuiDescriptorSet;

    struct CameraProperties {
        glm::mat4 viewInverse;
        glm::mat4 projInverse;
        
        // Black Hole Data
        glm::vec4 blackHoles[8]; // xyz = pos, w = mass (in Solar Masses)
        glm::vec4 blackHoleSpins[8]; // xyz = spin axis, w = spin magnitude (0..1)
        
        // Sphere Data
        glm::vec4 spheres[8]; // xyz = pos, w = radius
        glm::vec4 sphereColors[8]; // rgb = color, w = unused
        
        int numBlackHoles;
        int numSpheres;
        int mode; // 0 = Euclidean, 1 = Kerr
        int maxSteps;
        float stepSize;
        float padding[3];
    };

    struct PendingResource {
        std::shared_ptr<VulkanImage> image;
        vk::DescriptorSet descriptorSet;
        bool isImGuiDescriptor = false;
        std::unique_ptr<VulkanBuffer> buffer;
        std::unique_ptr<VulkanAccelerationStructure> accelerationStructure;
        uint64_t frameIndex;
    };
    std::vector<PendingResource> m_pendingResources;
    uint64_t m_frameCount = 0;

    void ProcessPendingResources();
};
