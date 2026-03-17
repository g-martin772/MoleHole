#pragma once
#include "BlackbodyLUTGenerator.h"
#include "AccelerationLUTGenerator.h"
#include "HRDiagramLUTGenerator.h"
#include "KerrGeodesicLUTGenerator.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanImage.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include "Platform/Vulkan/VulkanRenderPass.h"

class GLTFMesh;
class Scene;
class Camera;

#include <vulkan/vulkan.hpp>
#include <memory>

class BlackHoleRenderer {
public:
    BlackHoleRenderer();
    ~BlackHoleRenderer();

    void Init(VulkanDevice* device, VulkanRenderPass* renderPass, int width, int height);
    void Render(const Scene& scene, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache, const Camera& camera, float time, vk::CommandBuffer cmd);
    void PrepareDisplay(vk::CommandBuffer cmd);
    void Resize(int width, int height);
    void RenderToScreen(vk::CommandBuffer cmd); // Changed from void RenderToScreen()
    void Shutdown();
    vk::DescriptorSet GetImGuiDescriptorSet();
    
    // Getter methods for LUT textures (Returns Vulkan Image View or Descriptor?)
    // For now, let's return shared_ptr to VulkanImage
    std::shared_ptr<VulkanImage> GetBlackbodyLUT() const { return m_blackbodyLUT; }
    std::shared_ptr<VulkanImage> GetHRDiagramLUT() const { return m_hrDiagramLUT; }
    std::shared_ptr<VulkanImage> GetAccelerationLUT() const { return m_accelerationLUT; }
    std::shared_ptr<VulkanImage> GetOutputImage() const { return m_computeTexture; }

    // Export mode control
    void SetExportMode(bool isExporting) { m_isExportMode = isExporting; }
    bool IsExportMode() const { return m_isExportMode; }

    void LoadSkybox(const std::string& path);
    
private:
    void CreateComputeTexture();
    void CreateBloomTextures();
    void ApplyBloom(vk::CommandBuffer cmd);
    void ApplyLensFlare(vk::CommandBuffer cmd);
    void CreateFullscreenQuad();
    void GenerateBlackbodyLUT();
    void GenerateAccelerationLUT();
    void GenerateHRDiagramLUT();
    void GenerateKerrGeodesicLUTs();
    void UpdateUniforms(const Scene& scene, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache, const Camera& camera, float time, vk::CommandBuffer cmd);
    void UpdateMeshBuffers(const Scene& scene, const std::unordered_map<std::string, std::shared_ptr<GLTFMesh>>& meshCache);
    void CreateMeshBuffers();

    VulkanDevice* m_Device = nullptr;

    std::unique_ptr<VulkanPipeline> m_computePipeline;
    std::unique_ptr<VulkanShader> m_computeShader;
    
    // Compute pipeline descriptors
    vk::DescriptorPool m_ComputeDescriptorPool;
    vk::DescriptorSetLayout m_ComputeDescriptorSetLayout;
    vk::DescriptorSet m_ComputeDescriptorSet;
    
    std::unique_ptr<VulkanPipeline> m_displayPipeline;
    std::unique_ptr<VulkanShader> m_displayShader;
    
    // Display pipeline descriptors
    vk::DescriptorSetLayout m_DisplayDescriptorSetLayout;
    vk::DescriptorSet m_DisplayDescriptorSet;

    // Bloom/Lens flare shaders/pipelines (Stubbed for now or to be implemented)
    std::unique_ptr<VulkanShader> m_bloomExtractShader;
    std::unique_ptr<VulkanShader> m_bloomBlurShader;
    std::unique_ptr<VulkanShader> m_lensFlareShader;
    
    std::shared_ptr<VulkanImage> m_skyboxTexture;
    std::unique_ptr<MoleHole::BlackbodyLUTGenerator> m_blackbodyLUTGenerator;
    std::unique_ptr<MoleHole::AccelerationLUTGenerator> m_accelerationLUTGenerator;
    std::unique_ptr<MoleHole::HRDiagramLUTGenerator> m_hrDiagramLUTGenerator;
    std::unique_ptr<MoleHole::KerrGeodesicLUTGenerator> m_kerrGeodesicLUTGenerator;

    std::shared_ptr<VulkanImage> m_computeTexture;
    std::shared_ptr<VulkanImage> m_bloomBrightTexture;
    std::shared_ptr<VulkanImage> m_bloomBlurTexture[2]; 
    int m_bloomFinalTextureIndex; 
    std::shared_ptr<VulkanImage> m_lensFlareTexture;
    
    std::shared_ptr<VulkanImage> m_blackbodyLUT;
    std::shared_ptr<VulkanImage> m_accelerationLUT;
    std::shared_ptr<VulkanImage> m_hrDiagramLUT;
    std::shared_ptr<VulkanImage> m_kerrDeflectionLUT;    
    std::shared_ptr<VulkanImage> m_kerrRedshiftLUT;      
    std::shared_ptr<VulkanImage> m_kerrPhotonSphereLUT;  
    std::shared_ptr<VulkanImage> m_kerrISCOLUT;          

    // Uniform Buffers
    std::unique_ptr<VulkanBuffer> m_CommonUBO;
    std::unique_ptr<VulkanBuffer> m_DisplayUBO;

    void UpdateDisplayDescriptorSet();
    void UpdateComputeDescriptorSet();
    void CreateFallbackSkybox();

    // Mesh geometry SSBOs
    std::unique_ptr<VulkanBuffer> m_meshDataSSBO;
    std::unique_ptr<VulkanBuffer> m_triangleSSBO;
    vk::DescriptorSet m_ImGuiDescriptorSet = VK_NULL_HANDLE;

    int m_width, m_height;

    // Export mode flag - when true, uses custom ray marching settings from AppState
    bool m_isExportMode = false;

    static constexpr float G = 6.67430e-11f;
    static constexpr float c = 299792458.0f;
};
