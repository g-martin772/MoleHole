//
// Created by leo on 11/28/25.
//

#include "RendererWindow.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "imgui.h"

namespace RendererWindow {

void Render(UI* ui, float fps) {
    if (!ImGui::Begin("System")) {
        ImGui::End();
        return;
    }

    // System Info Section
    if (ImGui::CollapsingHeader("System Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame time: %.3f ms", 1000.0f / fps);
    }

    // Display Settings Section
    if (ImGui::CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool vsync = Application::State().window.vsync;
        if (ImGui::Checkbox("VSync", &vsync)) {
            Application::State().window.vsync = vsync;
            ui->MarkConfigDirty();
        }
        ImGui::Text("VSync State: %s", Application::State().window.vsync ? "Enabled" : "Disabled");

        float beamSpacing = Application::State().GetProperty<float>("beamSpacing", 1.0f);
        if (ImGui::DragFloat("Ray spacing", &beamSpacing, 0.01f, 0.0f, 1e10f)) {
            Application::State().SetProperty("beamSpacing", beamSpacing);
            ui->MarkConfigDirty();
        }
    }

    ImGui::End();
}

} // namespace RendererWindow
