#include "VulkanSwapchain.h"

#include "VulkanApi.h"
#include <spdlog/spdlog.h>

void VulkanSwapChain::Init(VulkanDevice *device, const glm::vec2 size, uint32_t framesInFlight) {
    m_Extent = vk::Extent2D(size.x, size.y);
    m_Device = device;
    m_FramesInFlight = framesInFlight;

    CreateSwapChain();
}

void VulkanSwapChain::Destroy() {
    DestroySwapChain();
}

void VulkanSwapChain::AcquireNextImage(vk::Semaphore semaphore, vk::Fence fence, uint64_t timeout) {
    const vk::Result result = m_Device->GetDevice().
            acquireNextImageKHR(m_SwapChain, timeout, semaphore, fence, &m_CurrentFrame);

    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
        Update(m_Size);
    else if (result != vk::Result::eSuccess)
        spdlog::error("Failed to acquire next image from vulkan swapchain: {}", vk::to_string(result));
}

void VulkanSwapChain::AdvanceSemaphoreIndex() {
    m_SemaphoreIndex = (m_SemaphoreIndex + 1) % static_cast<uint32_t>(m_Images.size());
}

void VulkanSwapChain::SetVSync(bool enabled) {
    if (m_VSync == enabled)
        return;
    m_VSync = enabled;
    Update(m_Size);
}

void VulkanSwapChain::Present(vk::Queue graphicsQueue, vk::Queue presentQueue, vk::Semaphore waitSemaphore) {
    vk::PresentInfoKHR presentInfo = {};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &waitSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_SwapChain;
    presentInfo.pImageIndices = &m_CurrentFrame;
    presentInfo.pResults = nullptr;

    const vk::Result result = presentQueue.presentKHR(presentInfo);
    if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
        Update(m_Size); // TODO: Get current framebuffer size!
    else if (result != vk::Result::eSuccess)
        spdlog::error("Failed to present swapchain image: {}", vk::to_string(result));
}

void VulkanSwapChain::Update(glm::vec2 size) {
    DestroySwapChain();
    m_Extent = vk::Extent2D(size.x, size.y);
    CreateSwapChain();
}

void VulkanSwapChain::CreateSwapChain() {
    bool found = false;
    for (const auto &format: m_Device->GetSurfaceFormats()) {
        if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            m_Format = format.format;
            m_ColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
            found = true;
            break;
        }
    }

    if (!found)
        m_Format = m_Device->GetSurfaceFormats()[0].format;

    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo; // always supported, vsync on
    if (!m_VSync) {
        for (const auto &mode: m_Device->GetSurfacePresentModes()) {
            if (mode == vk::PresentModeKHR::eImmediate) {
                presentMode = mode;
                break;
            }
        }
    } else {
        for (const auto &mode: m_Device->GetSurfacePresentModes()) {
            if (mode == vk::PresentModeKHR::eMailbox) {
                presentMode = mode;
                break;
            }
        }
    }

    if (m_Device->GetSurfaceCapabilities().currentExtent.width != UINT32_MAX)
        m_Extent = m_Device->GetSurfaceCapabilities().currentExtent;

    if (m_FramesInFlight > m_Device->GetSurfaceCapabilities().maxImageCount)
        m_FramesInFlight = m_Device->GetSurfaceCapabilities().maxImageCount;

    vk::SwapchainCreateInfoKHR createInfo = {};
    createInfo.surface = m_Device->GetRequirements().Surface;
    createInfo.minImageCount = m_FramesInFlight;
    createInfo.imageFormat = m_Format;
    createInfo.imageColorSpace = m_ColorSpace;
    createInfo.imageExtent = m_Extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

    if (m_Device->GetQueueIndices().Graphics != m_Device->GetQueueIndices().Present) {
        createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
        const uint32_t queueIndices[] = {m_Device->GetQueueIndices().Graphics, m_Device->GetQueueIndices().Present};
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueIndices;
    } else {
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    createInfo.preTransform = m_Device->GetSurfaceCapabilities().currentTransform;
    createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = nullptr; // Is this worth being checked out?

    try {
        m_SwapChain = m_Device->GetDevice().createSwapchainKHR(createInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create vulkan swapchain: {}", err.what());
        return;
    }
    spdlog::trace("Created vulkan swapchain: succesful");

    m_CurrentFrame = 0;
    m_SemaphoreIndex = 0;
    m_Images = m_Device->GetDevice().getSwapchainImagesKHR(m_SwapChain);
    m_Views.clear();
    for (const auto &image: m_Images) {
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = m_Format;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        try {
            m_Views.push_back(m_Device->GetDevice().createImageView(viewInfo));
        } catch (const vk::SystemError &err) {
            spdlog::error("Failed to create image view for vulkan swapchain image: {}", err.what());
            return;
        }
    }

    m_DepthFormat = m_Device->GetDepthFormat();

    VulkanImageSpec spec;
    spec.Size = {m_Extent.width, m_Extent.height};
    spec.Tiling = vk::ImageTiling::eOptimal;
    spec.Usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
    spec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    spec.Format = m_DepthFormat;
    spec.CreateView = true;
    spec.ViewAspectFlags = vk::ImageAspectFlagBits::eDepth;
    spec.MipLevels = 1;
    spec.ArrayLayers = 1;
    spec.Samples = vk::SampleCountFlagBits::e1;
    m_DepthImage.Create(spec, m_Device);
}

void VulkanSwapChain::DestroySwapChain() {
    m_DepthImage.Destroy();
    for (const auto &view: m_Views)
        m_Device->GetDevice().destroyImageView(view);
    m_Views.clear();
    m_Images.clear();
    m_Device->GetDevice().destroySwapchainKHR(m_SwapChain);
}
