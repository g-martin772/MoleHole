//
// Created by leo on 11/28/25.
//

#include "SettingsPopUp.h"
#include "ParameterWidgets.h"
#include "UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include <nfd.h>
#include <filesystem>
#include <algorithm>
#include <cstring>

namespace SettingsPopUp {

static void RenderSectionHeader(const char* title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

void Render(UI* ui, Scene* scene, bool* showSettingsWindow) {
    if (*showSettingsWindow && !ImGui::IsPopupOpen("Settings")) {
        ImGui::OpenPopup("Settings");
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(800, 700), ImGuiCond_Appearing);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);

    if (ImGui::BeginPopupModal("Settings", showSettingsWindow, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
        ImGui::Text("Settings");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("SettingsTabs", ImGuiTabBarFlags_None)) {

            if (ImGui::BeginTabItem("Application")) {
                ImGui::Spacing();
                if (ImGui::BeginChild("AppSettingsScroll", ImVec2(0, -60), false)) {

                    RenderSectionHeader("BACKGROUND");

                    if (scene) {
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Background Image (.hdr)");
                        const std::string currentBackground = Application::Params().Get(Params::AppBackgroundImage, std::string("space.hdr"));
                        const std::vector<std::string> backgroundPaths = ui->GetAvailableBackgrounds();

                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##BackgroundImage", currentBackground.c_str())) {
                            for (size_t i = 0; i < backgroundPaths.size(); ++i) {
                                const bool isSelected = (backgroundPaths[i] == currentBackground);
                                if (ImGui::Selectable(backgroundPaths[i].c_str(), isSelected)) {
                                    Application::Params().Set(Params::AppBackgroundImage, backgroundPaths[i]);
                                    ui->MarkConfigDirty();
                                    scene->reloadSkybox = true;
                                }
                                if (isSelected) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }

                            if (ImGui::Selectable("Custom Import...")) {
                                nfdchar_t* outPath = nullptr;
                                nfdfilteritem_t filterItems[] = { { "High Dynamic Range", "hdr" } };
                                if (nfdresult_t result = NFD_OpenDialog(&outPath, filterItems, 1, nullptr); result == NFD_OKAY && outPath) {
                                    std::filesystem::path sourcePath(outPath);
                                    std::filesystem::path destPath = "../assets/backgrounds/" + sourcePath.filename().string();
                                    try {
                                        std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing);
                                        Application::Params().Set(Params::AppBackgroundImage, sourcePath.filename().string());
                                        ui->MarkConfigDirty();
                                        scene->reloadSkybox = true;
                                    } catch (const std::exception& e) {
                                        spdlog::error("Failed to copy background image file: {}", e.what());
                                    }
                                    free(outPath);
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "HDR image for environment lighting and reflections");
                    }

                    RenderSectionHeader("FONT");

                    static std::vector<std::string> availableFonts;
                    static int selectedFontIndex = -1;
                    static bool fontsLoaded = false;

                    const std::string defaultFont = "Roboto-Regular.ttf";
                    std::string currentFont = Application::Params().Get(Params::UIMainFont, defaultFont);

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

                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Font Family");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::BeginCombo("##FontFamily", currentFont.c_str())) {
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

                        if (ImGui::Selectable("Custom Import...")) {
                            nfdchar_t* outPath = nullptr;
                            nfdfilteritem_t filterItems[] = { { "TrueType Font", "ttf" } };
                            nfdresult_t result = NFD_OpenDialog(&outPath, filterItems, 1, nullptr);
                            if (result == NFD_OKAY && outPath) {
                                std::filesystem::path sourcePath(outPath);
                                std::filesystem::path destPath = "../assets/font/" + sourcePath.filename().string();
                                try {
                                    std::filesystem::copy_file(sourcePath, destPath, std::filesystem::copy_options::overwrite_existing);
                                    Application::Params().Set(Params::UIMainFont, sourcePath.filename().string());
                                    ui->MarkConfigDirty();
                                    fontsLoaded = false;
                                    availableFonts = ui->GetAvailableFonts();
                                    ui->ReloadFonts();
                                } catch (const std::exception& e) {
                                    spdlog::error("Failed to copy font file: {}", e.what());
                                }
                                free(outPath);
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Font Size");
                    ParameterWidgets::RenderParameter(Params::UIFontSize, ui, ParameterWidgets::WidgetStyle::Compact);

                    RenderSectionHeader("EXPORT");

                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Default Export Path");
                    const std::string defaultPath = ".";
                    std::string exportPath = Application::Params().Get(Params::UIDefaultExportPath, defaultPath);

                    if (ImGui::BeginTable("ExportPathGrid", 2, ImGuiTableFlags_None)) {
                        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Browse", ImGuiTableColumnFlags_WidthFixed, 80.0f);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        char pathBuf[512];
                        std::strncpy(pathBuf, exportPath.c_str(), sizeof(pathBuf));
                        pathBuf[sizeof(pathBuf) - 1] = '\0';
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::InputText("##ExportPath", pathBuf, sizeof(pathBuf))) {
                            Application::Params().Set(Params::UIDefaultExportPath, std::string(pathBuf));
                            ui->MarkConfigDirty();
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
                        if (ImGui::Button("Browse", ImVec2(-1, 0))) {
                            nfdchar_t* outPath = nullptr;
                            nfdresult_t result = NFD_PickFolder(&outPath, exportPath.c_str());
                            if (result == NFD_OKAY && outPath) {
                                Application::Params().Set(Params::UIDefaultExportPath, std::string(outPath));
                                ui->MarkConfigDirty();
                                free(outPath);
                            }
                        }
                        ImGui::PopStyleColor(3);

                        ImGui::EndTable();
                    }

                    RenderSectionHeader("USER INTERFACE");

                    bool introEnabled = Application::Params().Get(Params::AppIntroAnimationEnabled, true);
                    if (ImGui::BeginTable("UIToggles", 2, ImGuiTableFlags_None)) {
                        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Toggle", ImGuiTableColumnFlags_WidthFixed, 60.0f);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("Intro Animation");
                        ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "Show animated intro when app starts");
                        ImGui::TableNextColumn();
                        if (ImGui::Checkbox("##IntroAnim", &introEnabled)) {
                            Application::Params().Set(Params::AppIntroAnimationEnabled, introEnabled);
                            ui->MarkConfigDirty();
                        }

                        ImGui::EndTable();
                    }

                    RenderSectionHeader("PERFORMANCE");

                    ParameterWidgets::RenderParameterGroup(
                        ParameterGroup::Window, ui,
                        ParameterWidgets::WidgetStyle::Standard, true
                    );

                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("About")) {
                ImGui::Spacing();
                if (ImGui::BeginChild("AboutScroll", ImVec2(0, -60), false)) {

                    ImGui::Spacing();

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 1.0f, 1.0f));
                    ImGui::Text("MoleHole - Black Hole Simulation");
                    ImGui::PopStyleColor();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Version 1.0.0");

                    RenderSectionHeader("ABOUT");

                    ImGui::TextWrapped(
                        "An advanced black hole and gravitational physics simulator for researchers, "
                        "educators, and space enthusiasts. Explore the fascinating phenomena of "
                        "gravitational lensing, accretion disks, and spacetime curvature in real-time."
                    );

                    RenderSectionHeader("FEATURES");

                    ImGui::TextColored(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f), " * ");
                    ImGui::SameLine();
                    ImGui::Text("Real-time gravitational lensing simulation");

                    ImGui::TextColored(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f), " * ");
                    ImGui::SameLine();
                    ImGui::Text("Accurate black hole physics and accretion disk rendering");

                    ImGui::TextColored(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f), " * ");
                    ImGui::SameLine();
                    ImGui::Text("Customizable animation graphs for complex simulations");

                    ImGui::TextColored(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f), " * ");
                    ImGui::SameLine();
                    ImGui::Text("Advanced camera controls and third-person perspectives");

                    ImGui::TextColored(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f), " * ");
                    ImGui::SameLine();
                    ImGui::Text("Physics-based object interactions and collisions");

                    RenderSectionHeader("CREDITS");

                    ImGui::TextWrapped("Developed with passion for astrophysics and simulation.");
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                        "Built with C++23, OpenGL 4.6, and advanced compute shader rendering.");

                    RenderSectionHeader("LICENSE");

                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "MIT License");
                    ImGui::Spacing();

                    if (ImGui::Button("View License", ImVec2(-1, 0))) {
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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float totalButtonWidth = 250.0f;
        float availWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX((availWidth - totalButtonWidth) * 0.5f + ImGui::GetCursorPosX());

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            *showSettingsWindow = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        if (ImGui::Button("Save Settings", ImVec2(120, 0))) {
            Application::Instance().SaveState();
            ui->MarkConfigDirty();
        }

        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}

} // namespace SettingsPopUp
