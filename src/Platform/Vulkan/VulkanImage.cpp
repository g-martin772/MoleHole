#include "VulkanImage.h"

#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include <spdlog/spdlog.h>

void VulkanImage2D::Create(const VulkanImageSpec &spec, VulkanDevice *device) {
    m_Size = spec.Size;
    m_Device = device;

    vk::ImageCreateInfo imageInfo = {};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.extent.width = static_cast<uint32_t>(spec.Size.x);
    imageInfo.extent.height = static_cast<uint32_t>(spec.Size.y);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = spec.MipLevels;
    imageInfo.arrayLayers = spec.ArrayLayers;
    imageInfo.format = spec.Format;
    imageInfo.tiling = spec.Tiling;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = spec.Usage;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;

    try {
        m_Image = m_Device->GetDevice().createImage(imageInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create Vulkan Image: {}", err.what());
        return;
    }
    spdlog::trace("Created Vulkan Image: successful");

    // Get memory requirements
    vk::MemoryRequirements requirements = m_Device->GetDevice().getImageMemoryRequirements(m_Image);
    const uint32_t memoryIndex = VulkanMemory::FindMemoryIndex(m_Device, requirements.memoryTypeBits,
                                                               spec.MemoryProperties);

    // Allocate Image
    vk::MemoryAllocateInfo allocInfo;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = memoryIndex;

    try {
        m_Memory = m_Device->GetDevice().allocateMemory(allocInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to allocate vulkan image memory: {}", err.what());
        return;
    }

    try {
        m_Device->GetDevice().bindImageMemory(m_Image, m_Memory, 0);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to bind vulkan image memory: {}", err.what());
        return;
    }

    if (spec.CreateView) {
        VulkanImageViewSpec viewSpec;
        viewSpec.Type = vk::ImageViewType::e2D;
        viewSpec.Format = spec.Format;
        viewSpec.AspectFlags = spec.ViewAspectFlags;
        CreateImageView(viewSpec);
    }
}

void VulkanImage2D::Destroy() {
    for (const auto &view: m_ImageViews)
        m_Device->GetDevice().destroyImageView(view);
    m_Device->GetDevice().freeMemory(m_Memory);
    m_Device->GetDevice().destroyImage(m_Image);
}

uint32_t VulkanImage2D::CreateImageView(const VulkanImageViewSpec &spec) {
    vk::ImageViewCreateInfo createInfo = {};
    createInfo.image = m_Image;
    createInfo.viewType = spec.Type;
    createInfo.format = spec.Format;
    createInfo.subresourceRange.aspectMask = spec.AspectFlags;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    try {
        m_ImageViews.push_back(m_Device->GetDevice().createImageView(createInfo));
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create vulkan image view: {}", err.what());
        return 0;
    }

    return m_ImageViews.size() - 1;
}
