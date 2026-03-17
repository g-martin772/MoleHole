#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <vector>

class VulkanDevice;

struct VulkanImageViewSpec {
    vk::ImageViewType Type = vk::ImageViewType::e2D;
    vk::Format Format = vk::Format::eR8G8B8A8Unorm;
    vk::ImageAspectFlags AspectFlags = vk::ImageAspectFlagBits::eColor;
    uint32_t BaseMipLevel = 0;
    uint32_t MipLevelCount = 1;
    uint32_t BaseArrayLayer = 0;
    uint32_t ArrayLayerCount = 1;
};

struct VulkanImageSpec {
    glm::vec3 Size;
    vk::ImageTiling Tiling = vk::ImageTiling::eOptimal;
    vk::ImageUsageFlags Usage = vk::ImageUsageFlagBits::eSampled;
    vk::MemoryPropertyFlags MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
    vk::Format Format = vk::Format::eR8G8B8A8Unorm;
    bool CreateView = true;
    vk::ImageAspectFlags ViewAspectFlags = vk::ImageAspectFlagBits::eColor;
    uint32_t MipLevels = 1;
    uint32_t ArrayLayers = 1;
    vk::SampleCountFlagBits Samples = vk::SampleCountFlagBits::e1;
    vk::ImageType ImageType = vk::ImageType::e2D;
};

struct VulkanSamplerSpec {
    vk::Filter MagFilter = vk::Filter::eLinear;
    vk::Filter MinFilter = vk::Filter::eLinear;
    vk::SamplerMipmapMode MipmapMode = vk::SamplerMipmapMode::eLinear;
    vk::SamplerAddressMode AddressU = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode AddressV = vk::SamplerAddressMode::eRepeat;
    vk::SamplerAddressMode AddressW = vk::SamplerAddressMode::eRepeat;
    float MipLodBias = 0.0f;
    bool AnisotropyEnable = false;
    float MaxAnisotropy = 1.0f;
    bool CompareEnable = false;
    vk::CompareOp CompareOp = vk::CompareOp::eAlways;
    float MinLod = 0.0f;
    float MaxLod = 0.0f;
    vk::BorderColor BorderColor = vk::BorderColor::eIntOpaqueBlack;
    bool UnnormalizedCoordinates = false;
};

/**
 * VulkanImage: Comprehensive image wrapper for Vulkan 2D and 3D images
 *
 * Features:
 * - Creation of vk::Image and vk::DeviceMemory
 * - Multiple image view support with custom subresource ranges
 * - Sampler creation and management
 * - Image layout transitions using pipeline barriers
 * - Buffer-to-image copying for texture uploads
 * - Support for 2D/3D images with various formats
 * - Configurable usage flags and properties
 */
class VulkanImage {
public:
    VulkanImage() = default;
    ~VulkanImage();

    /**
     * Create and allocate a Vulkan image
     * @param spec Image specification
     * @param device The Vulkan device
     */
    void Create(const VulkanImageSpec &spec, VulkanDevice *device);

    /**
     * Destroy and deallocate the image, views, and sampler
     */
    void Destroy();

    /**
     * Create an image view for this image
     * @param spec Image view specification
     * @return Index of the created view (0-based)
     */
    uint32_t CreateImageView(const VulkanImageViewSpec &spec);

    /**
     * Create a sampler for this image (for texture sampling)
     * @param spec Sampler specification
     * @return true if successful, false otherwise
     */
    bool CreateSampler(const VulkanSamplerSpec &spec);

    /**
     * Transition the image layout using a pipeline barrier
     * @param oldLayout Current image layout
     * @param newLayout Target image layout
     * @param srcStageMask Source pipeline stage
     * @param dstStageMask Destination pipeline stage
     * @param aspectMask Image aspect (color, depth, etc.)
     * @param baseMipLevel Starting mip level
     * @param mipLevelCount Number of mip levels
     * @param baseArrayLayer Starting array layer
     * @param arrayLayerCount Number of array layers
     */
    void TransitionLayout(
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eTopOfPipe,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
        uint32_t baseMipLevel = 0,
        uint32_t mipLevelCount = 1,
        uint32_t baseArrayLayer = 0,
        uint32_t arrayLayerCount = 1);

    /**
     * Record a layout transition pipeline barrier into an existing command buffer
     */
    void TransitionLayout(
        vk::CommandBuffer cmd,
        vk::ImageLayout oldLayout,
        vk::ImageLayout newLayout,
        vk::PipelineStageFlags srcStageMask,
        vk::PipelineStageFlags dstStageMask,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
        uint32_t baseMipLevel = 0,
        uint32_t mipLevelCount = 1,
        uint32_t baseArrayLayer = 0,
        uint32_t arrayLayerCount = 1);

    /**
     * Copy buffer data to image (for texture uploads)
     * @param srcBuffer Source buffer handle
     * @param width Texture width
     * @param height Texture height
     * @param depth Texture depth (1 for 2D images)
     * @param baseMipLevel Target mip level
     * @param baseArrayLayer Target array layer
     * @param aspectMask Image aspect
     */
    void CopyBufferToImage(
        vk::Buffer srcBuffer,
        uint32_t width,
        uint32_t height,
        uint32_t depth = 1,
        uint32_t baseMipLevel = 0,
        uint32_t baseArrayLayer = 0,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor);

    /**
     * Copy image data to another image
     * @param srcImage Source image
     * @param dstImage Destination image
     * @param width Texture width
     * @param height Texture height
     * @param depth Texture depth (1 for 2D images)
     * @param srcMipLevel Source mip level
     * @param dstMipLevel Destination mip level
     * @param srcArrayLayer Source array layer
     * @param dstArrayLayer Destination array layer
     */
    static void CopyImageToImage(
        vk::Image srcImage,
        vk::Image dstImage,
        uint32_t width,
        uint32_t height,
        uint32_t depth = 1,
        uint32_t srcMipLevel = 0,
        uint32_t dstMipLevel = 0,
        uint32_t srcArrayLayer = 0,
        uint32_t dstArrayLayer = 0,
        vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor);

    /**
     * Get a specific image view
     * @param index View index (default 0 for first view)
     * @return vk::ImageView handle
     */
    vk::ImageView GetImageView(uint32_t index = 0) const;

    /**
     * Get the image sampler
     * @return vk::Sampler handle (nullptr if not created)
     */
    vk::Sampler GetSampler() const { return m_Sampler; }

    /**
     * Get the underlying Vulkan image handle
     */
    vk::Image GetImage() const { return m_Image; }

    /**
     * Get the image size
     */
    glm::vec3 GetSize() const { return m_Size; }

    /**
     * Get the image format
     */
    vk::Format GetFormat() const { return m_Format; }

    /**
     * Get the number of mip levels
     */
    uint32_t GetMipLevels() const { return m_MipLevels; }

    /**
     * Get the number of array layers
     */
    uint32_t GetArrayLayers() const { return m_ArrayLayers; }

    /**
     * Get the number of image views
     */
    uint32_t GetViewCount() const { return static_cast<uint32_t>(m_ImageViews.size()); }

    /**
     * Get image usage flags
     */
    vk::ImageUsageFlags GetUsageFlags() const { return m_Usage; }

    /**
     * Check if the image has a sampler
     */
    bool HasSampler() const { return m_Sampler; }

private:
    VulkanDevice *m_Device = nullptr;
    vk::Image m_Image;
    vk::DeviceMemory m_Memory;
    vk::Sampler m_Sampler;
    glm::vec3 m_Size = {0.0f, 0.0f, 1.0f};
    vk::Format m_Format = vk::Format::eR8G8B8A8Unorm;
    vk::ImageUsageFlags m_Usage;
    uint32_t m_MipLevels = 1;
    uint32_t m_ArrayLayers = 1;
    vk::ImageType m_ImageType = vk::ImageType::e2D;
    std::vector<vk::ImageView> m_ImageViews;

    /**
     * Execute a layout transition command immediately using a single-use command buffer
     */
    void ExecuteLayoutTransition(const vk::ImageMemoryBarrier &barrier,
                                vk::PipelineStageFlags srcStage,
                                vk::PipelineStageFlags dstStage);

    /**
     * Execute a buffer-to-image copy using a single-use command buffer
     */
    void ExecuteBufferToImageCopy(vk::Buffer srcBuffer,
                                 vk::BufferImageCopy &region);
};

/**
 * Legacy VulkanImage2D class for backward compatibility
 * Use VulkanImage instead for new code
 */
class VulkanImage2D {
public:
    void Create(const VulkanImageSpec &spec, VulkanDevice *device);
    void Destroy();

    uint32_t CreateImageView(const VulkanImageViewSpec &spec);
    vk::ImageView GetImageView(uint32_t index) const { return m_Image.GetImageView(index); }
    vk::Image GetImage() const { return m_Image.GetImage(); }
    glm::vec2 GetSize() const { return m_Image.GetSize(); }

private:
    VulkanImage m_Image;
};
