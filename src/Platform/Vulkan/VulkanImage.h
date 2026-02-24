#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

class VulkanDevice;

struct VulkanImageViewSpec {
    vk::ImageViewType Type = vk::ImageViewType::e2D;
    vk::Format Format = vk::Format::eR8G8B8A8Unorm;
    vk::ImageAspectFlags AspectFlags = vk::ImageAspectFlagBits::eColor;
};

struct VulkanImageSpec {
    glm::vec2 Size;
    vk::ImageTiling Tiling;
    vk::ImageUsageFlagBits Usage;
    vk::MemoryPropertyFlags MemoryProperties;
    vk::Format Format;
    bool CreateView;
    vk::ImageAspectFlags ViewAspectFlags = vk::ImageAspectFlagBits::eColor;
    uint32_t MipLevels = 1, ArrayLayers = 1;
    vk::SampleCountFlags Samples;
};

class VulkanImage2D {
public:
    void Create(const VulkanImageSpec &spec, VulkanDevice *device);
    void Destroy();

    uint32_t CreateImageView(const VulkanImageViewSpec &spec);
    vk::ImageView GetImageView(uint32_t index) const { return m_ImageViews[index]; }
    glm::vec2 GetSize() const { return m_Size; }
private:
    VulkanDevice *m_Device = nullptr;
    vk::DeviceMemory m_Memory;
    glm::vec2 m_Size = {0.0f, 0.0f};
    vk::Image m_Image;
    std::vector<vk::ImageView> m_ImageViews;
};
