#include "VulkanApi.h"

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "Application/Application.h"
#include "spdlog/spdlog.h"


void VulkanApi::Init() {
    m_Instance.Init();

    GLFWwindow *window = Application::Instance().GetWindow()->GetNativeWindow();

    VkSurfaceKHR surface;
    if (glfwCreateWindowSurface(m_Instance.GetInstance(), window, nullptr, &surface) != VK_SUCCESS)
        spdlog::error("Failed to create window surface!");
    else
        spdlog::trace("Creating window surface: successful");

    m_Surface = surface;

    DeviceRequirements deviceRequirements{};
    deviceRequirements.Compute = true;
    deviceRequirements.Graphics = true;
    deviceRequirements.Transfer = true;
    deviceRequirements.Present = true;
    deviceRequirements.Surface = m_Surface;
    m_Device.Init(deviceRequirements, &m_Instance);

    m_GraphicsCommandPool.Init(&m_Device, m_Device.GetQueueIndices().Graphics);
    m_MainCommandBuffer = m_GraphicsCommandPool.AllocateCommandBuffer();

    glm::ivec2 size;
    Application::Instance().GetWindow()->GetFramebufferSize(size.x, size.y);
    m_SwapChain.Init(&m_Device, size, 3);

    m_MainRenderPass.Init(&m_Device, &m_SwapChain, glm::vec4(0.0f, 0.0f, size.x, size.y), {0.0f, 0.0f, 0.0f, 1.0f});
    m_MainFrameBuffer.CreateFromSwapchain(&m_Device, &m_SwapChain, &m_MainRenderPass);
    m_RenderCommandBuffers.clear();
    for (uint32_t i = 0; i < m_SwapChain.GetImageCount(); i++) {
        m_RenderCommandBuffers.push_back(m_GraphicsCommandPool.AllocateCommandBuffer());
        m_InFlightFences.emplace_back(&m_Device);
        m_RenderFinishedSemaphores.emplace_back(&m_Device);
        m_ImageAvailableSemaphores.emplace_back(&m_Device);
    }


    // ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();

    vk::DescriptorPoolSize poolSizes[] = {
        { vk::DescriptorType::eCombinedImageSampler, 1000 },
        { vk::DescriptorType::eUniformBuffer, 1000 },
        { vk::DescriptorType::eStorageImage, 1000 },
        { vk::DescriptorType::eSampledImage, 1000 },
        { vk::DescriptorType::eInputAttachment, 1000 }
    };
    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 5000;
    poolInfo.poolSizeCount = 5;
    poolInfo.pPoolSizes = poolSizes;
    m_ImGuiDescriptorPool = m_Device.GetDevice().createDescriptorPool(poolInfo);

    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance       = m_Instance.GetInstance();
    init_info.PhysicalDevice = m_Device.GetPhysicalDevice();
    init_info.Device         = m_Device.GetDevice();
    init_info.QueueFamily    = m_Device.GetQueueIndices().Graphics;
    init_info.Queue          = m_Device.GetGraphicsQueue();
    init_info.DescriptorPool = m_ImGuiDescriptorPool;
    init_info.RenderPass     = m_MainRenderPass.GetRenderPass();
    init_info.MinImageCount  = 2;
    init_info.ImageCount     = m_SwapChain.GetImageCount();
    init_info.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&init_info);
}

void VulkanApi::Shutdown() {
    m_Device.WaitIdle();
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    m_Device.GetDevice().destroyDescriptorPool(m_ImGuiDescriptorPool);
    m_MainFrameBuffer.Destroy();
    m_MainRenderPass.Destroy();
    m_SwapChain.Destroy();
    m_MainCommandBuffer->Free();
    m_RenderCommandBuffers.clear();
    m_GraphicsCommandPool.Destroy();
    m_ImageAvailableSemaphores.clear();
    m_RenderFinishedSemaphores.clear();
    m_InFlightFences.clear();
    m_Device.Shutdown();
    m_Instance.GetInstance().destroySurfaceKHR(m_Surface);
    m_Instance.Shutdown();
}

bool VulkanApi::BeginFrame() {
    const uint32_t syncIndex = m_SwapChain.GetSemaphoreIndex();
    m_InFlightFences[syncIndex].WaitAndReset();
    m_SwapChain.AcquireNextImage(m_ImageAvailableSemaphores[syncIndex].GetSemaphore(), VK_NULL_HANDLE);

    const uint32_t imageIndex = m_SwapChain.GetCurrentImageIndex();
    Ref<VulkanCommandBuffer> commandBuffer = m_RenderCommandBuffers[imageIndex];
    commandBuffer->GetCommandBuffer().reset();
    commandBuffer->Begin();

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    return true;
}

void VulkanApi::BeginRenderPass() {
    const uint32_t imageIndex = m_SwapChain.GetCurrentImageIndex();
    Ref<VulkanCommandBuffer> commandBuffer = m_RenderCommandBuffers[imageIndex];

    m_MainRenderPass.Begin(m_MainFrameBuffer.GetFrameBuffer(imageIndex),
                           Application::Instance().GetWindow()->GetSize(),
                           commandBuffer->GetCommandBuffer());
}

void VulkanApi::EndFrame() {
    const uint32_t imageIndex = m_SwapChain.GetCurrentImageIndex();
    const uint32_t syncIndex  = m_SwapChain.GetSemaphoreIndex();
    Ref<VulkanCommandBuffer> commandBuffer = m_RenderCommandBuffers[imageIndex];

    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer->GetCommandBuffer());

    m_MainRenderPass.End(commandBuffer->GetCommandBuffer());
    commandBuffer->End();
    commandBuffer->Submit(
        m_Device.GetGraphicsQueue(),
        m_ImageAvailableSemaphores[syncIndex].GetSemaphore(),
        m_RenderFinishedSemaphores[syncIndex].GetSemaphore(),
        m_InFlightFences[syncIndex].GetFence());
    m_SwapChain.Present(m_Device.GetGraphicsQueue(), m_Device.GetPresentQueue(),
                        m_RenderFinishedSemaphores[syncIndex].GetSemaphore());
    m_SwapChain.AdvanceSemaphoreIndex();
}

void VulkanApi::OnResize(glm::vec2 newSize) {
    m_Device.WaitIdle();
    m_SwapChain.Update(newSize);
    m_MainFrameBuffer.Rebuild();
}

void VulkanApi::SetClearColor(glm::vec4 color) {
}

void VulkanApi::SetVSync(bool enabled) {
    m_Device.WaitIdle();
    m_SwapChain.SetVSync(enabled);
    m_MainFrameBuffer.Rebuild();
}

