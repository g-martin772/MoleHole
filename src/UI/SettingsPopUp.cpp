//
// Created by leo on 11/28/25.
//

#include "SettingsPopUp.h"
#include "ParameterWidgets.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
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
    ImGui::SetNextWindowSize(ImVec2(800, 700), ImGuiCond_Appearing);

    // Popup styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

    // ------------------------------------------------------------------------------------------
    // Main Settings PopUp
    // ------------------------------------------------------------------------------------------
    if (ImGui::BeginPopupModal("Settings", showSettingsWindow, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        ImGui::TextWrapped("Configure application settings and parameters");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // ------------------------------------------------------------------------------------------
        // Main Settings Tab Bar (for easier navigation)
        // ------------------------------------------------------------------------------------------
        if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_None)) {

            ImGui::Separator();

            // Display Tab
            if (ImGui::BeginTabItem("Display")) {

                ImGui::Spacing();
                if (ImGui::BeginChild("Display Settings", ImVec2(0, -30), false))
                {
                    ParameterWidgets::RenderParameterGroup(
                        ParameterGroup::Window,
                        ui,
                        ParameterWidgets::WidgetStyle::Detailed, true
                    );
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            // Camera Tab
            if (ImGui::BeginTabItem("Camera")) {

                ImGui::Spacing();
                if (ImGui::BeginChild("Camera Settings", ImVec2(0, -30), false))
                {
                    ParameterWidgets::RenderParameterGroupWithFilter(
                        ParameterGroup::Camera, ui,
                        [](const ParameterMetadata& meta) {
                            return meta.name != "Camera.Position" &&
                                   meta.name != "Camera.Front" &&
                                   meta.name != "Camera.Up" &&
                                   meta.name != "Camera.Pitch" &&
                                   meta.name != "Camera.Yaw";
                        },
                        ParameterWidgets::WidgetStyle::Detailed, true
                    );
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            // Rendering Tab
            if (ImGui::BeginTabItem("Rendering")) {
                ImGui::Spacing();

                if (ImGui::BeginChild("Rendering Settings", ImVec2(0, -30), false)) {
                    ParameterWidgets::RenderParameterGroupWithFilter(
                        ParameterGroup::Rendering, ui,
                        [](const ParameterMetadata& meta) {
                            return meta.name.find("Bloom") == std::string::npos &&
                                   meta.name.find("Acc") == std::string::npos &&
                                   meta.name.find("Doppler") == std::string::npos;
                        },
                        ParameterWidgets::WidgetStyle::Detailed, true
                    );
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            // Application Tab
            if (ImGui::BeginTabItem("Application")) {

                ImGui::Spacing();
                if (ImGui::BeginChild("Application Settings", ImVec2(0, -30), false))
                {
                    static std::vector<std::string> availableFonts;
                    static int selectedFontIndex = -1;
                    static bool fontsLoaded = false;

                    if (ImGui::CollapsingHeader("Font Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                        ImGui::TextWrapped("Customize the application's font and size");
                        ImGui::PopStyleColor();
                        ImGui::Separator();
                        ImGui::Spacing();

                        const std::string defaultFont = "Roboto-Regular.ttf";
                        std::string currentFont = Application::Params().Get(Params::UIMainFont, defaultFont);

                        ImGui::Text("Current Font: %s", currentFont.c_str());
                        ImGui::Spacing();

                        if (!fontsLoaded) {
                            availableFonts = ui->GetAvailableFonts();
                            for (size_t i = 0; i < availableFonts.size(); ++i) {
                                if (availableFonts[i] == currentFont) {
                                    selectedFontIndex = static_cast<int>(i);
                                    break;
                                }
                            }
                            fontsLoaded = true;
                        }

                        if (ImGui::BeginCombo("Select Font", currentFont.c_str())) {
                            for (size_t i = 0; i < availableFonts.size(); ++i) {
                                bool isSelected = (selectedFontIndex == static_cast<int>(i));
                                if (ImGui::Selectable(availableFonts[i].c_str(), isSelected)) {
                                    selectedFontIndex = static_cast<int>(i);
                                    Application::Params().Set(Params::UIMainFont, availableFonts[i]);
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
                        ParameterWidgets::RenderParameter(Params::UIFontSize, ui, ParameterWidgets::WidgetStyle::Detailed);

                        ImGui::Spacing();
                        ImGui::Separator();
                        ImGui::Spacing();

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
                                    std::filesystem::copy_file(sourcePath, destPath,
                                                               std::filesystem::copy_options::overwrite_existing);

                                    Application::Params().Set(Params::UIMainFont, sourcePath.filename().string());
                                    ui->MarkConfigDirty();

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
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                        ImGui::TextWrapped("Choose a .ttf file to add to your font collection");
                        ImGui::PopStyleColor();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // Media Export Settings Section
                    if (ImGui::CollapsingHeader("Media Export Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
                        ImGui::TextWrapped("Configure default export location for screenshots and videos");
                        ImGui::PopStyleColor();
                        ImGui::Separator();
                        ImGui::Spacing();

                        const std::string defaultPath = ".";
                        std::string exportPath = Application::Params().Get(Params::UIDefaultExportPath, defaultPath);

                        ImGui::Text("Default Export Path:");
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 0.7f, 1.0f));
                        ImGui::TextWrapped("%s", exportPath.c_str());
                        ImGui::PopStyleColor();
                        ImGui::Spacing();

                        if (ImGui::Button("Change Export Path...", ImVec2(-1, 0))) {
                            nfdchar_t* outPath = nullptr;
                            nfdresult_t result = NFD_PickFolder(&outPath, exportPath.c_str());

                            if (result == NFD_OKAY && outPath) {
                                Application::Params().Set(Params::UIDefaultExportPath, std::string(outPath));
                                ui->MarkConfigDirty();
                                spdlog::info("Default export path set to: {}", outPath);
                                free(outPath);
                            }
                        }

                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
                        ImGui::TextWrapped("Screenshots and videos will be saved here by default");
                        ImGui::PopStyleColor();

                        ImGui::Spacing();

                        if (ImGui::Button("Reset to Current Directory", ImVec2(-1, 0))) {
                            Application::Params().Set(Params::UIDefaultExportPath, std::string("."));
                            ui->MarkConfigDirty();
                        }
                    }

                    ImGui::Spacing();

                    // Other Application Settings
                    ParameterWidgets::RenderParameter(Params::AppShowDemoWindow, ui, ParameterWidgets::WidgetStyle::Detailed);
                    ParameterWidgets::RenderParameter(Params::AppUseKerrDistortion, ui, ParameterWidgets::WidgetStyle::Detailed);
                    ParameterWidgets::RenderParameter(Params::AppIntroAnimationEnabled, ui, ParameterWidgets::WidgetStyle::Detailed);
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            // About Tab
            if (ImGui::BeginTabItem("About")) {

                ImGui::Spacing();
                if (ImGui::BeginChild("Display Settings", ImVec2(0, -30), false))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 1.0f, 1.0f));
                    ImGui::TextWrapped("MoleHole - Black Hole Simulation");
                    ImGui::PopStyleColor();
                    ImGui::Spacing();
                    ImGui::Text("Version: 1.0.0");
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::TextWrapped("Developed using:");
                    ImGui::BulletText("OpenGL 4.6");
                    ImGui::BulletText("GLFW - Window and input");
                    ImGui::BulletText("ImGui - User interface");
                    ImGui::BulletText("GLM - Mathematics");
                    ImGui::BulletText("spdlog - Logging");
                    ImGui::BulletText("yaml-cpp - Configuration");
                    ImGui::BulletText("stb_image - Image loading");
                    ImGui::BulletText("PhysX - Physics simulation");
                    ImGui::BulletText("FFmpeg - Video export (Linux)");

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    ImGui::TextWrapped("Special thanks to all open-source contributors!");

                    ImGui::Spacing();

                    if (ImGui::Button("View License (MIT)", ImVec2(-1, 0))) {
                        spdlog::info("Opening license file...");
#ifdef __linux__
                        system("xdg-open ../LICENSE &");
#endif
                    }
                }
                ImGui::EndChild();

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        // Bottom buttons
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
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
