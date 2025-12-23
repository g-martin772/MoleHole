//
// Created by leo on 11/28/25.
//

#include "RendererWindow.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
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
        bool vsync = Application::Params().Get(Params::WindowVSync, true);
        if (ImGui::Checkbox("VSync", &vsync)) {
            Application::Params().Set(Params::WindowVSync, vsync);
            ui->MarkConfigDirty();
        }
        ImGui::Text("VSync State: %s", Application::Params().Get(Params::WindowVSync, true) ? "Enabled" : "Disabled");
    }

    ImGui::End();
}

} // namespace RendererWindow
