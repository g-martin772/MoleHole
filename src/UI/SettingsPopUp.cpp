//
// Created by leo on 11/28/25.
//

#include "SettingsPopUp.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "imgui.h"

namespace SettingsPopUp {

void Render(UI* ui, bool* showSettingsWindow) {
    // Open the popup when the window flag is set
    if (*showSettingsWindow && !ImGui::IsPopupOpen("Settings")) {
        ImGui::OpenPopup("Settings");
    }

    // Center the popup
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 250), ImGuiCond_Appearing);

    // Popup styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

    if (ImGui::BeginPopupModal("Settings", showSettingsWindow, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextWrapped("Configure application settings");
        ImGui::Separator();
        ImGui::Spacing();

        // Display Settings Section
        ImGui::Text("Display Settings");
        ImGui::Spacing();

        bool vsync = Application::Params().Get(Params::WindowVSync, true);
        if (ImGui::Checkbox("Enable VSync", &vsync)) {
            Application::Params().Set(Params::WindowVSync, vsync);
            ui->MarkConfigDirty();
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Synchronize frame rate with display refresh rate\nReduces screen tearing but may limit FPS");
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Current state: %s", Application::Params().Get(Params::WindowVSync, true) ? "Enabled" : "Disabled");

        // Spacer
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Info text
        ImGui::TextWrapped("More settings will be added in future updates.");

        // Bottom buttons
        ImGui::Spacing();
        ImGui::Separator();
        
        float buttonWidth = 120.0f;
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((availWidth - buttonWidth) * 0.5f);
        
        if (ImGui::Button("Close", ImVec2(buttonWidth, 0))) {
            *showSettingsWindow = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}

} // namespace SettingsPopUp
