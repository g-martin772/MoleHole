//
// Created by leo on 11/28/25.
//

#include "SettingsPopUp.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <nfd.h>
#include <filesystem>
#include <algorithm>

namespace SettingsPopUp {

void Render(UI* ui, bool* showSettingsWindow) {
    // Open the popup when the window flag is set
    if (*showSettingsWindow && !ImGui::IsPopupOpen("Settings")) {
        ImGui::OpenPopup("Settings");
    }

    // Center the popup
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_Appearing);

    // Popup styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

    if (ImGui::BeginPopupModal("Settings", showSettingsWindow, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextWrapped("Configure application settings");
        ImGui::Separator();
        ImGui::Spacing();

        // Display Settings Section
        if (ImGui::CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            bool vsync = Application::State().window.vsync;
            if (ImGui::Checkbox("Enable VSync", &vsync)) {
                Application::State().window.vsync = vsync;
                ui->MarkConfigDirty();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Synchronize frame rate with display refresh rate\nReduces screen tearing but may limit FPS");
            }

            ImGui::Spacing();
            ImGui::TextDisabled("Current state: %s", Application::State().window.vsync ? "Enabled" : "Disabled");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Font Settings Section
        static std::vector<std::string> availableFonts;
        static int selectedFontIndex = -1;
        static bool fontsLoaded = false;
        
        if (ImGui::CollapsingHeader("Font Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            std::string currentFont = Application::State().GetProperty<std::string>("mainFont", "Roboto-Regular.ttf");
            float fontSize = Application::State().GetProperty<float>("fontSize", 16.0f);
            
            ImGui::Text("Current Font: %s", currentFont.c_str());
            ImGui::Spacing();
            
            if (!fontsLoaded) {
                availableFonts = ui->GetAvailableFonts();
                // Find current font index
                for (size_t i = 0; i < availableFonts.size(); ++i) {
                    if (availableFonts[i] == currentFont) {
                        selectedFontIndex = static_cast<int>(i);
                        break;
                    }
                }
                fontsLoaded = true;
            }
            
            // Font dropdown
            if (ImGui::BeginCombo("Select Font", currentFont.c_str())) {
                for (size_t i = 0; i < availableFonts.size(); ++i) {
                    bool isSelected = (selectedFontIndex == static_cast<int>(i));
                    if (ImGui::Selectable(availableFonts[i].c_str(), isSelected)) {
                        selectedFontIndex = static_cast<int>(i);
                        Application::State().SetProperty("mainFont", availableFonts[i]);
                        ui->MarkConfigDirty();
                        ui->ReloadFonts();
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            
            ImGui::Spacing();
            
            // Font size slider
            if (ImGui::SliderFloat("Font Size", &fontSize, 10.0f, 32.0f, "%.0f")) {
                Application::State().SetProperty("fontSize", fontSize);
                ImGui::GetIO().FontGlobalScale = fontSize / 16.0f; // For 16.0f base size
                ui->MarkConfigDirty();
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            // Add custom font button
            if (ImGui::Button("Add Custom Font (.ttf)...", ImVec2(-1, 0))) {
                nfdchar_t* outPath = nullptr;
                nfdfilteritem_t filterItems[] = {
                    { "TrueType Font", "ttf" }
                };

                nfdresult_t result = NFD_OpenDialog(&outPath, filterItems, 1, nullptr);

                if (result == NFD_OKAY && outPath) {
                    std::filesystem::path sourcePath(outPath);
                    std::filesystem::path destPath = "../font/" + sourcePath.filename().string();
                    
                    try {
                        // Copy the font file to the font directory
                        std::filesystem::copy_file(sourcePath, destPath, 
                                                   std::filesystem::copy_options::overwrite_existing);
                        
                        // Set as current font
                        Application::State().SetProperty("mainFont", sourcePath.filename().string());
                        ui->MarkConfigDirty();
                        
                        // Reload available fonts and reload font atlas
                        fontsLoaded = false;
                        availableFonts = ui->GetAvailableFonts();
                        ui->ReloadFonts();
                        
                        spdlog::info("Custom font added successfully: {}", sourcePath.filename().string());
                    } catch (const std::exception& e) {
                        spdlog::error("Failed to copy font file: {}", e.what());
                    }

                    free(outPath);
                }
            }
            
            ImGui::TextDisabled("Choose a .ttf file to add to your font collection");
        }

        // Spacer
        ImGui::Spacing();
        ImGui::Spacing();

        // Bottom buttons
        ImGui::Separator();
        ImGui::Spacing();
        
        float buttonWidth = 120.0f;
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((availWidth - buttonWidth) * 0.5f);
        
        if (ImGui::Button("Close", ImVec2(buttonWidth, 0))) {
            *showSettingsWindow = false;
            fontsLoaded = false; // Reset for next time
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}

} // namespace SettingsPopUp
