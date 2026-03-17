#include "VulkanImage.h"

#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include "VulkanCommandBuffer.h"
#include <spdlog/spdlog.h>

// ============================================================================
// VulkanImage Implementation
// ============================================================================

VulkanImage::~VulkanImage() {
    if (m_Device != nullptr) {
        Destroy();
    }
}

void VulkanImage::Create(const VulkanImageSpec &spec, VulkanDevice *device) {
    m_Device = device;
    m_Size = spec.Size;
    m_Format = spec.Format;
    m_Usage = spec.Usage;
    m_MipLevels = spec.MipLevels;
    m_ArrayLayers = spec.ArrayLayers;
    m_ImageType = spec.ImageType;

    // Calculate extent based on image type
    vk::Extent3D extent;
    extent.width = static_cast<uint32_t>(spec.Size.x);
    extent.height = static_cast<uint32_t>(spec.Size.y);
    extent.depth = static_cast<uint32_t>(spec.Size.z);

    // Create image
    vk::ImageCreateInfo imageInfo;
    imageInfo.imageType = spec.ImageType;
    imageInfo.extent = extent;
    imageInfo.mipLevels = spec.MipLevels;
    imageInfo.arrayLayers = spec.ArrayLayers;
    imageInfo.format = spec.Format;
    imageInfo.tiling = spec.Tiling;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;
    imageInfo.usage = spec.Usage;
    imageInfo.samples = spec.Samples;
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

    if (memoryIndex == static_cast<uint32_t>(-1)) {
        spdlog::error("Failed to find suitable memory type for image");
        return;
    }

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

    spdlog::trace("Vulkan Image successfully allocated and bound");

    // Create default image view if requested
    if (spec.CreateView) {
        VulkanImageViewSpec viewSpec;
        viewSpec.Type = (spec.ImageType == vk::ImageType::e2D) ? vk::ImageViewType::e2D : vk::ImageViewType::e3D;
        viewSpec.Format = spec.Format;
        viewSpec.AspectFlags = spec.ViewAspectFlags;
        viewSpec.MipLevelCount = spec.MipLevels;
        viewSpec.ArrayLayerCount = spec.ArrayLayers;
        CreateImageView(viewSpec);
    }
}

void VulkanImage::Destroy() {
    if (!m_Device) return;

    // Destroy sampler
    if (m_Sampler) {
        m_Device->GetDevice().destroySampler(m_Sampler);
        m_Sampler = nullptr;
    }

    // Destroy image views
    for (const auto &view : m_ImageViews) {
        m_Device->GetDevice().destroyImageView(view);
    }
    m_ImageViews.clear();

    // Free memory
    if (m_Memory) {
        m_Device->GetDevice().freeMemory(m_Memory);
        m_Memory = nullptr;
    }

    // Destroy image
    if (m_Image) {
        m_Device->GetDevice().destroyImage(m_Image);
        m_Image = nullptr;
    }

    spdlog::trace("Vulkan Image destroyed");
}

uint32_t VulkanImage::CreateImageView(const VulkanImageViewSpec &spec) {
    vk::ImageViewCreateInfo createInfo;
    createInfo.image = m_Image;
    createInfo.viewType = spec.Type;
    createInfo.format = spec.Format;
    createInfo.subresourceRange.aspectMask = spec.AspectFlags;
    createInfo.subresourceRange.baseMipLevel = spec.BaseMipLevel;
    createInfo.subresourceRange.levelCount = spec.MipLevelCount;
    createInfo.subresourceRange.baseArrayLayer = spec.BaseArrayLayer;
    createInfo.subresourceRange.layerCount = spec.ArrayLayerCount;

    try {
        m_ImageViews.push_back(m_Device->GetDevice().createImageView(createInfo));
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create vulkan image view: {}", err.what());
        return static_cast<uint32_t>(-1);
    }

    spdlog::trace("Created vulkan image view: successful");
    return static_cast<uint32_t>(m_ImageViews.size() - 1);
}

bool VulkanImage::CreateSampler(const VulkanSamplerSpec &spec) {
    if (!m_Device) {
        spdlog::error("Cannot create sampler: device is null");
        return false;
    }

    vk::SamplerCreateInfo samplerInfo;
    samplerInfo.magFilter = spec.MagFilter;
    samplerInfo.minFilter = spec.MinFilter;
    samplerInfo.mipmapMode = spec.MipmapMode;
    samplerInfo.addressModeU = spec.AddressU;
    samplerInfo.addressModeV = spec.AddressV;
    samplerInfo.addressModeW = spec.AddressW;
    samplerInfo.mipLodBias = spec.MipLodBias;
    samplerInfo.anisotropyEnable = spec.AnisotropyEnable;
    samplerInfo.maxAnisotropy = spec.MaxAnisotropy;
    samplerInfo.compareEnable = spec.CompareEnable;
    samplerInfo.compareOp = spec.CompareOp;
    samplerInfo.minLod = spec.MinLod;
    samplerInfo.maxLod = spec.MaxLod;
    samplerInfo.borderColor = spec.BorderColor;
    samplerInfo.unnormalizedCoordinates = spec.UnnormalizedCoordinates;

    try {
        m_Sampler = m_Device->GetDevice().createSampler(samplerInfo);
    } catch (const vk::SystemError &err) {
        spdlog::error("Failed to create sampler: {}", err.what());
        return false;
    }

    spdlog::trace("Created sampler: successful");
    return true;
}

void VulkanImage::TransitionLayout(
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::PipelineStageFlags srcStageMask,
    vk::PipelineStageFlags dstStageMask,
    vk::ImageAspectFlags aspectMask,
    uint32_t baseMipLevel,
    uint32_t mipLevelCount,
    uint32_t baseArrayLayer,
    uint32_t arrayLayerCount) {

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = mipLevelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = arrayLayerCount;

    // Set access masks based on layout transitions
    switch (oldLayout) {
        case vk::ImageLayout::eUndefined:
            barrier.srcAccessMask = {};
            break;
        case vk::ImageLayout::ePreinitialized:
            barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
            break;
        case vk::ImageLayout::eColorAttachmentOptimal:
            barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            break;
        case vk::ImageLayout::eTransferSrcOptimal:
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            break;
        case vk::ImageLayout::eTransferDstOptimal:
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            break;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            break;
        case vk::ImageLayout::eGeneral:
            barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
            break;
        default:
            barrier.srcAccessMask = {};
            break;
    }

    switch (newLayout) {
        case vk::ImageLayout::eTransferDstOptimal:
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            break;
        case vk::ImageLayout::eTransferSrcOptimal:
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            break;
        case vk::ImageLayout::eColorAttachmentOptimal:
            barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
            break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal:
            barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            break;
        case vk::ImageLayout::eShaderReadOnlyOptimal:
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            break;
        case vk::ImageLayout::ePresentSrcKHR:
            barrier.dstAccessMask = {};
            break;
        case vk::ImageLayout::eGeneral:
            barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite;
            break;
        default:
            barrier.dstAccessMask = {};
            break;
    }

    ExecuteLayoutTransition(barrier, srcStageMask, dstStageMask);
}

void VulkanImage::TransitionLayout(
    vk::CommandBuffer cmd,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::PipelineStageFlags srcStageMask,
    vk::PipelineStageFlags dstStageMask,
    vk::ImageAspectFlags aspectMask,
    uint32_t baseMipLevel,
    uint32_t mipLevelCount,
    uint32_t baseArrayLayer,
    uint32_t arrayLayerCount) {

    vk::ImageMemoryBarrier barrier;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_Image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = mipLevelCount;
    barrier.subresourceRange.baseArrayLayer = baseArrayLayer;
    barrier.subresourceRange.layerCount = arrayLayerCount;

    // Set access masks
    switch (oldLayout) {
        case vk::ImageLayout::eUndefined: barrier.srcAccessMask = {}; break;
        case vk::ImageLayout::ePreinitialized: barrier.srcAccessMask = vk::AccessFlagBits::eHostWrite; break;
        case vk::ImageLayout::eColorAttachmentOptimal: barrier.srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite; break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal: barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite; break;
        case vk::ImageLayout::eTransferSrcOptimal: barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead; break;
        case vk::ImageLayout::eTransferDstOptimal: barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite; break;
        case vk::ImageLayout::eShaderReadOnlyOptimal: barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead; break;
        case vk::ImageLayout::eGeneral: barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite; break;
        default: barrier.srcAccessMask = {}; break;
    }

    switch (newLayout) {
        case vk::ImageLayout::eTransferDstOptimal: barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite; break;
        case vk::ImageLayout::eTransferSrcOptimal: barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead; break;
        case vk::ImageLayout::eColorAttachmentOptimal: barrier.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite; break;
        case vk::ImageLayout::eDepthStencilAttachmentOptimal: barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite; break;
        case vk::ImageLayout::eShaderReadOnlyOptimal: barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead; break;
        case vk::ImageLayout::ePresentSrcKHR: barrier.dstAccessMask = {}; break;
        case vk::ImageLayout::eGeneral: barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite; break;
        default: barrier.dstAccessMask = {}; break;
    }

    cmd.pipelineBarrier(
        srcStageMask,
        dstStageMask,
        vk::DependencyFlags(),
        nullptr,
        nullptr,
        barrier
    );
}

void VulkanImage::CopyBufferToImage(
    vk::Buffer srcBuffer,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t baseMipLevel,
    uint32_t baseArrayLayer,
    vk::ImageAspectFlags aspectMask) {

    vk::BufferImageCopy region;
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = baseMipLevel;
    region.imageSubresource.baseArrayLayer = baseArrayLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D(0, 0, 0);
    region.imageExtent = vk::Extent3D(width, height, depth);

    ExecuteBufferToImageCopy(srcBuffer, region);
}

void VulkanImage::CopyImageToImage(
    vk::Image srcImage,
    vk::Image dstImage,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t srcMipLevel,
    uint32_t dstMipLevel,
    uint32_t srcArrayLayer,
    uint32_t dstArrayLayer,
    vk::ImageAspectFlags aspectMask) {

    // This is a static helper that would need a device context
    // For now, this would be called from a device's command buffer recording
    // In a real implementation, you might pass a device or command buffer
    spdlog::warn("CopyImageToImage requires device context - use from a recording command buffer");
}

vk::ImageView VulkanImage::GetImageView(uint32_t index) const {
    if (index >= m_ImageViews.size()) {
        spdlog::error("Image view index {} out of range (only {} views)", index, m_ImageViews.size());
        return nullptr;
    }
    return m_ImageViews[index];
}

void VulkanImage::ExecuteLayoutTransition(const vk::ImageMemoryBarrier &barrier,
                                         vk::PipelineStageFlags srcStage,
                                         vk::PipelineStageFlags dstStage) {
    if (!m_Device) {
        spdlog::error("Cannot execute layout transition: device is null");
        return;
    }

    // Use a single-use command buffer to execute the barrier
    VulkanCommandPool commandPool;
    commandPool.Init(m_Device, m_Device->GetQueueIndices().Graphics);

    auto commandBuffer = commandPool.AllocateSingleUseCommandBuffer();
    if (!commandBuffer) {
        spdlog::error("Failed to allocate command buffer for layout transition");
        commandPool.Destroy();
        return;
    }

    commandBuffer->Begin();
    commandBuffer->GetCommandBuffer().pipelineBarrier(
        srcStage,
        dstStage,
        vk::DependencyFlags(),
        nullptr,
        nullptr,
        vk::ArrayProxy<const vk::ImageMemoryBarrier>(1, &barrier)
    );
    commandBuffer->End();

    commandBuffer->Submit(m_Device->GetGraphicsQueue());

    commandPool.Destroy();
    spdlog::trace("Image layout transition executed");
}

void VulkanImage::ExecuteBufferToImageCopy(vk::Buffer srcBuffer, vk::BufferImageCopy &region) {
    if (!m_Device) {
        spdlog::error("Cannot execute buffer to image copy: device is null");
        return;
    }

    VulkanCommandPool commandPool;
    commandPool.Init(m_Device, m_Device->GetQueueIndices().Transfer);

    auto commandBuffer = commandPool.AllocateSingleUseCommandBuffer();
    if (!commandBuffer) {
        spdlog::error("Failed to allocate command buffer for buffer to image copy");
        commandPool.Destroy();
        return;
    }

    commandBuffer->Begin();
    commandBuffer->GetCommandBuffer().copyBufferToImage(
        srcBuffer,
        m_Image,
        vk::ImageLayout::eTransferDstOptimal,
        1,
        &region
    );
    commandBuffer->End();

    commandBuffer->Submit(m_Device->GetTransferQueue());

    commandPool.Destroy();
    spdlog::trace("Buffer to image copy executed");
}

// ============================================================================
// VulkanImage2D Implementation (Legacy)
// ============================================================================

void VulkanImage2D::Create(const VulkanImageSpec &spec, VulkanDevice *device) {
    m_Image.Create(spec, device);
}

void VulkanImage2D::Destroy() {
    m_Image.Destroy();
}

uint32_t VulkanImage2D::CreateImageView(const VulkanImageViewSpec &spec) {
    return m_Image.CreateImageView(spec);
}
