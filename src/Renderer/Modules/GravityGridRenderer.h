#pragma once
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanPipeline.h"
#include "Platform/Vulkan/VulkanBuffer.h"
#include <glm/glm.hpp>

class Scene;
class Camera;

#include "Platform/Vulkan/VulkanRenderPass.h"

class GravityGridRenderer {
public:
    void Init(VulkanDevice* device, VulkanRenderPass* renderPass);
    void Render(const Scene& scene, const Camera& camera, float time, vk::CommandBuffer cmd);
    void Shutdown();

    void SetPlaneY(float y) { m_planeY = y; }
    void SetPlaneSize(float size);
    void SetCellSize(float size) { m_cellSize = size; }
    void SetOpacity(float a) { m_opacity = a; }
    void SetResolution(int r);
    void SetLineThickness(float t) { m_lineThickness = t; }
    void SetColor(const glm::vec3& c) { m_color = c; }

    float GetPlaneY() const { return m_planeY; }
    float GetPlaneSize() const { return m_planeSize; }
    float GetCellSize() const { return m_cellSize; }
    float GetOpacity() const { return m_opacity; }
    int GetResolution() const { return m_resolution; }
    float GetLineThickness() const { return m_lineThickness; }
    glm::vec3 GetColor() const { return m_color; }

private:
    void CreatePlane();

    VulkanDevice* m_Device = nullptr;
    std::unique_ptr<VulkanShader> m_shader;
    std::unique_ptr<VulkanPipeline> m_pipeline;
    std::unique_ptr<VulkanBuffer> m_vbo;
    std::unique_ptr<VulkanBuffer> m_ebo;
    std::unique_ptr<VulkanBuffer> m_UBO;
    int m_indexCount = 0;
    
    // Descriptor set and layout for UBO
    vk::DescriptorSetLayout m_DescriptorSetLayout;
    vk::DescriptorPool m_DescriptorPool;
    vk::DescriptorSet m_DescriptorSet;

    float m_planeY = -5.0f;
    float m_planeSize = 200.0f;
    float m_cellSize = 2.0f;
    float m_opacity = 0.7f;

    int m_resolution = 1024;
    float m_lineThickness = 0.03f;
    glm::vec3 m_color = glm::vec3(0.1f, 0.1f, 0.8f);
};
