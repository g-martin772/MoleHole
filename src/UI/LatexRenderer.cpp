#include "LatexRenderer.h"

#include "../Platform/Vulkan/VulkanImage.h"
#include "../Platform/Vulkan/VulkanApi.h"
#include "../Platform/Vulkan/VulkanDevice.h"
#include "../Application/Application.h"
#include "../Renderer/Renderer.h"
#include "imgui_impl_vulkan.h"

#include <spdlog/spdlog.h>
#include <stb_image.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>

namespace fs = std::filesystem;

// Helper functions for Vulkan operations
static uint32_t FindMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

static void CreateBuffer(VulkanDevice* device, vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::Buffer& buffer, vk::DeviceMemory& bufferMemory) {
    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer = device->GetDevice().createBuffer(bufferInfo);

    vk::MemoryRequirements memRequirements = device->GetDevice().getBufferMemoryRequirements(buffer);

    vk::MemoryAllocateInfo allocInfo{};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(device->GetPhysicalDevice(), memRequirements.memoryTypeBits, properties);

    bufferMemory = device->GetDevice().allocateMemory(allocInfo);

    device->GetDevice().bindBufferMemory(buffer, bufferMemory, 0);
}

static vk::CommandBuffer BeginSingleTimeCommands(VulkanDevice* device, vk::CommandPool pool) {
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    vk::CommandBuffer commandBuffer;
    if (device->GetDevice().allocateCommandBuffers(&allocInfo, &commandBuffer) != vk::Result::eSuccess) {
        throw std::runtime_error("failed to allocate command buffer!");
    }

    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    commandBuffer.begin(beginInfo);
    return commandBuffer;
}

static void EndSingleTimeCommands(VulkanDevice* device, vk::CommandPool pool, vk::CommandBuffer commandBuffer) {
    commandBuffer.end();

    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    if (device->GetGraphicsQueue().submit(1, &submitInfo, nullptr) != vk::Result::eSuccess) {
        spdlog::error("LatexRenderer: failed to submit queue");
    }
    device->GetGraphicsQueue().waitIdle();

    device->GetDevice().freeCommandBuffers(pool, 1, &commandBuffer);
}

LatexRenderer& LatexRenderer::Instance() {
    static LatexRenderer instance;
    return instance;
}

LatexRenderer::~LatexRenderer() {
    Shutdown();
}

void LatexRenderer::Init() {
    if (m_initialized) return;

    m_tempDir = (fs::temp_directory_path() / "molehole_latex").string();
    fs::create_directories(m_tempDir);

    auto result = std::system("latex --version > /dev/null 2>&1");
    bool hasLatex = (result == 0);

    result = std::system("dvipng --version > /dev/null 2>&1");
    bool hasDvipng = (result == 0);

    m_latexAvailable = hasLatex && hasDvipng;

    if (m_latexAvailable) {
        spdlog::info("LatexRenderer: latex + dvipng found, rendering enabled");
    } else {
        spdlog::warn("LatexRenderer: latex or dvipng not found, rendering disabled");
    }

    m_initialized = true;
}

void LatexRenderer::Shutdown() {
    if (!m_initialized) return;

    Invalidate();

    try {
        if (!m_tempDir.empty() && fs::exists(m_tempDir)) {
            fs::remove_all(m_tempDir);
        }
    } catch (...) {}

    m_initialized = false;
}

std::string LatexRenderer::HashLatex(const std::string& latex, int dpi) {
    std::size_t h = std::hash<std::string>{}(latex);
    h ^= std::hash<int>{}(dpi) + 0x9e3779b9 + (h << 6) + (h >> 2);
    std::ostringstream oss;
    oss << std::hex << h;
    return oss.str();
}

bool LatexRenderer::RenderToPNG(const std::string& latex, const std::string& pngPath, int dpi) {
    std::string hash = HashLatex(latex, dpi);
    std::string texPath = m_tempDir + "/" + hash + ".tex";
    std::string dviPath = m_tempDir + "/" + hash + ".dvi";

    {
        std::ofstream ofs(texPath);
        if (!ofs) {
            spdlog::error("LatexRenderer: failed to write tex file {}", texPath);
            return false;
        }
        ofs << "\\documentclass[preview,border=2pt]{standalone}\n"
            << "\\usepackage{amsmath}\n"
            << "\\usepackage{xcolor}\n"
            << "\\begin{document}\n"
            << "\\color{white}\n"
            << latex << "\n"
            << "\\end{document}\n";
    }

    std::string latexCmd = "cd " + m_tempDir +
                           " && latex -interaction=nonstopmode " + hash + ".tex"
                           " > /dev/null 2>&1";
    if (std::system(latexCmd.c_str()) != 0) {
        spdlog::error("LatexRenderer: latex compilation failed for: {}", latex);
        return false;
    }

    std::ostringstream dvipngCmd;
    dvipngCmd << "dvipng"
              << " -D " << dpi
              << " --truecolor"
              << " -bg Transparent"
              << " -fg 'rgb 1.0 1.0 1.0'"
              << " -o " << pngPath
              << " " << dviPath
              << " > /dev/null 2>&1";
    if (std::system(dvipngCmd.str().c_str()) != 0) {
        spdlog::error("LatexRenderer: dvipng conversion failed for: {}", latex);
        return false;
    }

    return fs::exists(pngPath);
}

LatexTexture LatexRenderer::LoadPNGToTexture(const std::string& pngPath) {
    LatexTexture result;
    
    // Check if renderer/device is available
    auto& renderer = Application::GetRenderer();
    if (!renderer.m_VulkanApi) {
         spdlog::error("LatexRenderer: Vulkan API not initialized");
         return result;
    }
    
    auto* device = &renderer.m_VulkanApi->GetDevice();
    auto& vulkanApi = *renderer.m_VulkanApi;

    int channels = 0;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* data = stbi_load(pngPath.c_str(), &result.width, &result.height, &channels, 4);
    if (!data) {
        spdlog::error("LatexRenderer: failed to load PNG {}", pngPath);
        return result;
    }

    try {
        // Create Vulkan Image
        result.image = std::make_shared<VulkanImage>();
        
        VulkanImageSpec spec;
        spec.Size = glm::vec3(result.width, result.height, 1.0f);
        spec.Format = vk::Format::eR8G8B8A8Unorm;
        spec.Tiling = vk::ImageTiling::eOptimal;
        spec.Usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
        spec.MemoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        spec.CreateView = true;
        spec.ViewAspectFlags = vk::ImageAspectFlagBits::eColor;
        
        result.image->Create(spec, device);
        
        // Create Staging Buffer
        vk::DeviceSize imageSize = result.width * result.height * 4;
        
        vk::Buffer stagingBuffer;
        vk::DeviceMemory stagingBufferMemory;
        
        CreateBuffer(device, imageSize, vk::BufferUsageFlagBits::eTransferSrc,
                     vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
                     stagingBuffer, stagingBufferMemory);
        
        // Copy data to staging buffer
        void* mappedData;
        if (device->GetDevice().mapMemory(stagingBufferMemory, 0, imageSize, {}, &mappedData) == vk::Result::eSuccess) {
            std::memcpy(mappedData, data, static_cast<size_t>(imageSize));
            device->GetDevice().unmapMemory(stagingBufferMemory);
        }
        
        // Copy buffer to image
        vk::CommandBuffer cmd = BeginSingleTimeCommands(device, vulkanApi.GetGraphicsCommandPool().GetPool());
        
        result.image->TransitionLayout(
            cmd,
            vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal,
            vk::PipelineStageFlagBits::eTopOfPipe,
            vk::PipelineStageFlagBits::eTransfer
        );
        
        vk::BufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D{0, 0, 0};
        region.imageExtent = vk::Extent3D{static_cast<uint32_t>(result.width), static_cast<uint32_t>(result.height), 1};
        
        cmd.copyBufferToImage(stagingBuffer, result.image->GetImage(), vk::ImageLayout::eTransferDstOptimal, 1, &region);
        
        // Transition to ShaderReadOnly
        result.image->TransitionLayout(
            cmd,
            vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader
        );
        
        EndSingleTimeCommands(device, vulkanApi.GetGraphicsCommandPool().GetPool(), cmd);
        
        // Cleanup staging buffer
        device->GetDevice().destroyBuffer(stagingBuffer);
        device->GetDevice().freeMemory(stagingBufferMemory);
        
        // Create Sampler
        VulkanSamplerSpec samplerSpec;
        samplerSpec.MagFilter = vk::Filter::eLinear;
        samplerSpec.MinFilter = vk::Filter::eLinear;
        samplerSpec.AddressU = vk::SamplerAddressMode::eClampToEdge;
        samplerSpec.AddressV = vk::SamplerAddressMode::eClampToEdge;
        result.image->CreateSampler(samplerSpec);
        
        // Create Descriptor Set for ImGui
        result.descriptorSet = ImGui_ImplVulkan_AddTexture(
            result.image->GetSampler(),
            result.image->GetImageView(),
            static_cast<VkImageLayout>(vk::ImageLayout::eShaderReadOnlyOptimal)
        );
        
        result.valid = true;
        spdlog::debug("LatexRenderer: loaded texture {}x{} from {}", result.width, result.height, pngPath);
        
    } catch (const std::exception& e) {
        spdlog::error("LatexRenderer: Vulkan error loading texture: {}", e.what());
        if (result.image) {
             result.image->Destroy();
             result.image.reset();
        }
        result.valid = false;
    }

    stbi_image_free(data);
    return result;
}

const LatexTexture& LatexRenderer::RenderLatex(const std::string& latex, int dpi) {
    if (!m_initialized) Init();

    if (!m_latexAvailable) return m_invalid;

    std::string key = HashLatex(latex, dpi);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    std::string pngPath = m_tempDir + "/" + key + ".png";

    if (!RenderToPNG(latex, pngPath, dpi)) {
        m_cache[key] = m_invalid;
        return m_cache[key];
    }

    m_cache[key] = LoadPNGToTexture(pngPath);
    return m_cache[key];
}

void LatexRenderer::Invalidate() {
    for (auto& [key, tex] : m_cache) {
        if (tex.descriptorSet) {
            ImGui_ImplVulkan_RemoveTexture((VkDescriptorSet)tex.descriptorSet);
            tex.descriptorSet = nullptr;
        }
        if (tex.image) {
            tex.image->Destroy();
            tex.image.reset();
        }
    }
    m_cache.clear();
}
