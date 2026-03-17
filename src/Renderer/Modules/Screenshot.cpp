#include "Screenshot.h"
#include <spdlog/spdlog.h>
#include "Application/Application.h"
#include "Renderer/Renderer.h"
#include "Platform/Vulkan/VulkanApi.h"
#include "Platform/Vulkan/VulkanBuffer.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

bool Screenshot::CaptureViewport(int x, int y, int width, int height, const std::string& filename) {
    auto& app = Application::Instance();
    auto& renderer = app.GetRenderer();
    if (!renderer.m_VulkanApi) {
        spdlog::error("Screenshot::CaptureViewport - Vulkan API not initialized");
        return false;
    }
    auto& api = *renderer.m_VulkanApi;
    auto& device = api.GetDevice();
    
    // Wait for device idle to ensure we can access resources safely
    device.WaitIdle();

    // Get current swapchain image
    auto& swapchain = api.GetSwapChain();
    int imageIndex = swapchain.GetCurrentImageIndex();
    if (imageIndex >= swapchain.GetImages().size()) {
        // Fallback if index is invalid (e.g. before first frame)
        imageIndex = 0;
    }
    vk::Image srcImage = swapchain.GetImages()[imageIndex];
    vk::Format format = swapchain.GetImageFormat();
    
    // Create staging buffer
    vk::DeviceSize size = width * height * 4;
    VulkanBuffer stagingBuffer;
    VulkanBufferSpec spec;
    spec.Size = size;
    spec.Usage = vk::BufferUsageFlagBits::eTransferDst;
    spec.MemoryProperties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    stagingBuffer.Create(spec, &device);

    // Create command buffer
    auto cmd = api.GetGraphicsCommandPool().AllocateSingleUseCommandBuffer();
    cmd->Begin();
    auto cmdBuf = cmd->GetCommandBuffer();

    // Transition image to transfer src
    vk::ImageMemoryBarrier barrier{};
    barrier.oldLayout = vk::ImageLayout::ePresentSrcKHR; // Assumption
    barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = srcImage;
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;

    cmdBuf.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        {},
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    // Copy
    vk::BufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{x, y, 0};
    region.imageExtent = vk::Extent3D{(uint32_t)width, (uint32_t)height, 1};

    cmdBuf.copyImageToBuffer(srcImage, vk::ImageLayout::eTransferSrcOptimal, stagingBuffer.GetBuffer(), 1, &region);

    // Transition back
    barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
    barrier.newLayout = vk::ImageLayout::ePresentSrcKHR;
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
    barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;

    cmdBuf.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer,
        vk::PipelineStageFlagBits::eTransfer,
        {},
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    cmd->End();
    cmd->Submit(device.GetGraphicsQueue()); // Waits idle

    // Map and read
    void* data = stagingBuffer.Map();
    std::vector<unsigned char> pixels(size);
    memcpy(pixels.data(), data, size);
    stagingBuffer.Unmap();
    stagingBuffer.Destroy();

    // Convert BGRA to RGBA (Vulkan swapchain is usually BGR)
    // Check format, but usually B8G8R8A8 or similar.
    // If R8G8B8A8, no swap needed.
    // Assuming BGR for now as it's common.
    if (format == vk::Format::eB8G8R8A8Unorm || format == vk::Format::eB8G8R8A8Srgb) {
        for (size_t i = 0; i < pixels.size(); i += 4) {
            std::swap(pixels[i], pixels[i+2]);
        }
    }

    // Save
    // Flip vertically because Vulkan (and STB) have different coords?
    // Vulkan 0,0 is top-left. STB writes top-left.
    // But usually OpenGL/Vulkan UVs might be flipped.
    // I'll leave it as is for now.
    
    stbi_flip_vertically_on_write(0); // Ensure no auto flip
    int result = stbi_write_png(filename.c_str(), width, height, 4, pixels.data(), width * 4);
    
    if (result) {
        spdlog::info("Screenshot saved to {}", filename);
    } else {
        spdlog::error("Failed to save screenshot to {}", filename);
    }

    return result != 0;
}

bool Screenshot::CaptureWindow(const std::string& filename) {
    auto& app = Application::Instance();
    int width, height;
    app.GetWindow()->GetFramebufferSize(width, height);
    return CaptureViewport(0, 0, width, height, filename);
}

std::string Screenshot::GenerateTimestampedFilename(const std::string& prefix, const std::string& extension) {
    // Basic implementation that doesn't rely on rendering API
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    char buffer[128];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", now);
    return prefix + "_" + std::string(buffer) + extension;
}

bool Screenshot::SavePNG(const std::string& filename, int width, int height, const std::vector<unsigned char>& pixels) {
    return false;
}

void Screenshot::FlipImageVertically(std::vector<unsigned char>& pixels, int width, int height, int channels) {
}
