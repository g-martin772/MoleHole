#include "ViewportWindow.h"
#include "../Renderer/Renderer.h"
#include "../Application/Application.h"
#include "imgui.h"

void ViewportWindow::Render(UI* ui, Scene* scene) {
    if (!ui) return;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin("Viewport - 3D Simulation", nullptr, ImGuiWindowFlags_NoCollapse)) {
        
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        
        // Handle resizing
        auto& renderer = Application::Instance().GetRenderer();
        auto* blackHoleRenderer = renderer.GetBlackHoleRenderer();

        if (blackHoleRenderer) {
            static int lastWidth = 0;
            static int lastHeight = 0;
            
            if (viewportSize.x > 0 && viewportSize.y > 0) {
                int width = (int)viewportSize.x;
                int height = (int)viewportSize.y;

                if (lastWidth != width || lastHeight != height) {
                    // Check if change is significant (to avoid potential thrashing)
                    if (width > 0 && height > 0) {
                        blackHoleRenderer->Resize(width, height);
                        lastWidth = width;
                        lastHeight = height;
                    }
                }

                // Get Texture
                vk::DescriptorSet descSet = blackHoleRenderer->GetImGuiDescriptorSet();
                if (descSet) {
                    // ImGui expects ImTextureID which is void*
                    // With ImGui Vulkan backend, we cast VkDescriptorSet to ImTextureID
                    ImGui::Image((ImTextureID)(VkDescriptorSet)descSet, viewportSize);
                } else {
                    ImGui::Text("No texture available");
                }
            }
        } else {
             ImGui::Text("BlackHoleRenderer not available");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}
