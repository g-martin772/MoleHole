#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "VulkanImage.h"

class VulkanDevice;

class VulkanSwapChain {
public:
    void Init(VulkanDevice *device, glm::vec2 size, uint32_t framesInFlight = 2);
    void Destroy();

    void AcquireNextImage(vk::Semaphore semaphore, vk::Fence fence, uint64_t timeout = UINT64_MAX);
    void Present(vk::Queue graphicsQueue, vk::Queue presentQueue, vk::Semaphore waitSemaphore);
    void Update(glm::vec2 size);

    std::vector<vk::Image> GetImages() const { return m_Images; }
    std::vector<vk::ImageView> GetImageViews() const { return m_Views; }
    vk::ImageView GetDepthImageView() const { return m_DepthImage.GetImageView(0); }
    vk::Extent2D GetExtent() const { return m_Extent; }

    vk::Format GetImageFormat() const { return m_Format; }
    vk::Format GetDepthImageFormat() const { return m_DepthFormat; }

    uint32_t GetImageCount() const { return m_Images.size(); }
    uint32_t GetCurrentImageIndex() const { return m_CurrentFrame; }

    vk::SwapchainKHR GetSwapChain() const { return m_SwapChain; }
private:
    void CreateSwapChain();
    void DestroySwapChain();
private:
    glm::vec2 m_Size = {0.0f, 0.0f};
    vk::Extent2D m_Extent = {0, 0};
    uint32_t m_FramesInFlight = 3, m_CurrentFrame = 0;
    std::vector<vk::Image> m_Images;
    std::vector<vk::ImageView> m_Views;
    VulkanDevice *m_Device = nullptr;
    vk::SwapchainKHR m_SwapChain;
    vk::Format m_Format = vk::Format::eB8G8R8A8Unorm, m_DepthFormat = vk::Format::eD32Sfloat;
    vk::ColorSpaceKHR m_ColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    VulkanImage2D m_DepthImage;
};
