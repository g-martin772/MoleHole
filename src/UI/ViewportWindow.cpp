#include "ViewportWindow.h"
#include "../Renderer/Renderer.h"
#include "../Application/Application.h"
#include "imgui.h"

void ViewportWindow::Render(UI* ui, Scene* scene) {
    if (!ui) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Viewport - 3D Simulation", nullptr)) {
        
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        
        // Handle resizing
        auto& renderer = Application::Instance().GetRenderer();
        auto* blackHoleRenderer = renderer.GetBlackHoleRenderer();
        auto* rtRenderer = renderer.GetRayTracingRenderer();
        auto viewportMode = renderer.GetSelectedViewport();

        if (viewportSize.x > 0 && viewportSize.y > 0) {
            int width = (int)viewportSize.x;
            int height = (int)viewportSize.y;

            // Update Renderer's viewport size, but defer actual resizing to the start of the next frame's render
            // This prevents invalidating the image that was just rendered and is about to be displayed
            renderer.SetViewportBounds(0, 0, width, height);

            // Get Texture
            vk::DescriptorSet descSet = VK_NULL_HANDLE;
            if (viewportMode == Renderer::ViewportMode::HardwareRayTracing && rtRenderer) {
                descSet = rtRenderer->GetImGuiDescriptorSet();
            } else if (blackHoleRenderer) {
                // Legacy renderer might still need explicit resize here if it doesn't handle it in RenderScene
                if (renderer.GetSelectedViewport() != Renderer::ViewportMode::HardwareRayTracing)
                    blackHoleRenderer->Resize(width, height);
                descSet = blackHoleRenderer->GetImGuiDescriptorSet();
            }

            if (descSet) {
                // ImGui expects ImTextureID which is void*
                // With ImGui Vulkan backend, we cast VkDescriptorSet to ImTextureID
                ImGui::Image((ImTextureID)(VkDescriptorSet)descSet, viewportSize);
            } else {
                ImGui::Text("No texture available");
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
