#include "VulkanApi.h"

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

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

    m_MainRenderPass.Init(&m_Device, &m_SwapChain, glm::vec4(0.0f, 0.0f, size.x, size.y), {1.0f, 0.0f, 0.0f, 1.0f});
    m_MainFrameBuffer.CreateFromSwapchain(&m_Device, &m_SwapChain, &m_MainRenderPass);
    m_RenderCommandBuffers.clear();
    for (uint32_t i = 0; i < m_SwapChain.GetImageCount(); i++) {
        m_RenderCommandBuffers.push_back(m_GraphicsCommandPool.AllocateCommandBuffer());
        m_InFlightFences.emplace_back(&m_Device);
        m_RenderFinishedSemaphores.emplace_back(&m_Device);
        m_ImageAvailableSemaphores.emplace_back(&m_Device);
    }
}

void VulkanApi::Shutdown() {
    m_Device.WaitIdle();
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
    // If swapchain is resizing, return false

    const uint32_t frameIndex = m_SwapChain.GetCurrentImageIndex();

    m_InFlightFences[frameIndex].WaitAndReset();
    m_RenderCommandBuffers[frameIndex]->GetCommandBuffer().reset();

    m_SwapChain.AcquireNextImage(m_ImageAvailableSemaphores[frameIndex].GetSemaphore(),
                                 m_InFlightFences[frameIndex].GetFence());
    Ref<VulkanCommandBuffer> commandBuffer = m_RenderCommandBuffers[frameIndex];
    commandBuffer->Begin();
    m_MainRenderPass.Begin(m_MainFrameBuffer.GetFrameBuffer(frameIndex), Application::Instance().GetWindow()->GetSize(),
                           commandBuffer->GetCommandBuffer());

    return true;
}

void VulkanApi::EndFrame() {
    const uint32_t frameIndex = m_SwapChain.GetCurrentImageIndex();
    Ref<VulkanCommandBuffer> commandBuffer = m_RenderCommandBuffers[frameIndex];


    m_MainRenderPass.End(commandBuffer->GetCommandBuffer());
    commandBuffer->End();
    commandBuffer->Submit(m_Device.GetGraphicsQueue());
    m_SwapChain.Present(m_Device.GetGraphicsQueue(), m_Device.GetPresentQueue(),
                        m_RenderFinishedSemaphores[frameIndex].GetSemaphore());
}

void VulkanApi::OnResize(glm::vec2 newSize) {
}

void VulkanApi::SetClearColor(glm::vec4 color) {
}
