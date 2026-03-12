//
// Created by leo on 11/28/25.
//

#include "TopBar.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "../Simulation/Scene.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/Screenshot.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <nfd.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <algorithm>

namespace TopBar {

static void AddToRecentScenes(UI* ui, const std::string& path) {
    if (path.empty()) {
        return;
    }

    try {
        if (!std::filesystem::exists(path) || !std::filesystem::is_regular_file(path)) {
            return;
        }
    } catch (const std::exception&) {
        return;
    }

    auto recentScenes = Application::Params().Get(Params::AppRecentScenes, std::vector<std::string>());

    auto it = std::find(recentScenes.begin(), recentScenes.end(), path);
    if (it != recentScenes.end()) {
        recentScenes.erase(it);
    }

    recentScenes.insert(recentScenes.begin(), path);

    static constexpr size_t MAX_RECENT_SCENES = 10;
    if (recentScenes.size() > MAX_RECENT_SCENES) {
        recentScenes.resize(MAX_RECENT_SCENES);
    }

    Application::Params().Set(Params::AppRecentScenes, recentScenes);
    ui->MarkConfigDirty();
}

static void RemoveFromRecentScenes(UI* ui, const std::string& path) {
    auto recentScenes = Application::Params().Get(Params::AppRecentScenes, std::vector<std::string>());

    auto it = std::find(recentScenes.begin(), recentScenes.end(), path);
    if (it != recentScenes.end()) {
        recentScenes.erase(it);
        Application::Params().Set(Params::AppRecentScenes, recentScenes);
        ui->MarkConfigDirty();
    }
}

void LoadScene(UI* ui, Scene* scene, const std::string& path) {
    if (!scene || path.empty()) {
        return;
    }

    try {
        std::filesystem::path fsPath(path);
        if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
            RemoveFromRecentScenes(ui, path);
            return;
        }

        std::string currentScenePath = scene->currentPath.string();
        if (currentScenePath == path) {
            return;
        }

        spdlog::info("Loading scene: {}", path);

        scene->Deserialize(fsPath);

        Application::Params().Set(Params::AppLastOpenScene, path);
        AddToRecentScenes(ui, path);

        spdlog::info("Scene loaded successfully: {}", path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load scene '{}': {}", path, e.what());
        RemoveFromRecentScenes(ui, path);
    }
}

void RenderMainMenuBar(UI* ui, Scene* scene, bool& doSave, bool& doOpen,
                      bool& doTakeScreenshotDialog, bool& doTakeScreenshotViewportDialog,
                      bool& doTakeScreenshot, bool& doTakeScreenshotViewport) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) {
                Application::Instance().NewScene();
            }

            if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                doOpen = true;
            }
            bool canSave = scene && !scene->currentPath.empty();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, canSave)) {
                doSave = true;
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                if (scene) {
                    auto path = Scene::ShowFileDialog(true);
                    if (!path.empty()) {
                        scene->Serialize(path);
                        Application::Params().Set(Params::AppLastOpenScene, path.string());
                        AddToRecentScenes(ui, path.string());
                    }
                }
            }

            ImGui::Separator();

            if (ImGui::BeginMenu("From Template")) {
                namespace fs = std::filesystem;
                std::vector<fs::path> templates;
                fs::path templatesDir = "../templates";
                try {
                    if (fs::exists(templatesDir) && fs::is_directory(templatesDir)) {
                        for (const auto& entry : fs::directory_iterator(templatesDir)) {
                            if (!entry.is_regular_file()) continue;
                            auto ext = entry.path().extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                            if (ext == ".yaml" || ext == ".yml") {
                                templates.push_back(entry.path());
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to scan templates directory '{}': {}", templatesDir.string(), e.what());
                }

                std::sort(templates.begin(), templates.end(), [](const fs::path& a, const fs::path& b){ return a.filename().string() < b.filename().string(); });

                if (templates.empty()) {
                    ImGui::MenuItem("No templates found", nullptr, false, false);
                } else {
                    for (const auto& t : templates) {
                        std::string label = t.filename().string();
                        if (ImGui::MenuItem(label.c_str())) {
                            if (scene) {
                                try {
                                    scene->Deserialize(t, false);
                                    scene->currentPath.clear();
                                    Application::Params().Set(Params::AppLastOpenScene, std::string(""));
                                    spdlog::info("Loaded template: {}", t.string());
                                } catch (const std::exception& e) {
                                    spdlog::error("Failed to load template '{}': {}", t.string(), e.what());
                                }
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Export")) {
            if (ImGui::MenuItem("Export as Video")) {
                *ui->GetShowExportWindowPtr() = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export as Image (choose location)", "F12")) {
                doTakeScreenshotViewportDialog = true;
            }
            if (ImGui::MenuItem("Export as Image (default path)", "Ctrl+F12")) {
                doTakeScreenshotViewport = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export Full Window (choose location)", "F11")) {
                doTakeScreenshotDialog = true;
            }
            if (ImGui::MenuItem("Export Full Window (default path)", "Ctrl+F11")) {
                doTakeScreenshot = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export Scene Data")) {
                if (scene) {
                    auto path = Scene::ShowFileDialog(true);
                    if (!path.empty()) {
                        scene->Serialize(path);
                        spdlog::info("Scene data exported to: {}", path.string());
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Documentation", "F1")) {
                *ui->GetShowHelpWindowPtr() = true;
            }
            if (ImGui::MenuItem("Interactive Tutorial")) {
                ui->StartTutorial();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About")) {
                *ui->GetShowSettingsWindowPtr() = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void HandleFileOperations(UI* ui, Scene* scene, bool doSave, bool doOpen) {
    if (doOpen && scene) {
        auto path = Scene::ShowFileDialog(false);
        if (!path.empty()) {
            LoadScene(ui, scene, path.string());
        }
    }

    if (doSave && scene && !scene->currentPath.empty()) {
        spdlog::info("Saving scene: {}", scene->currentPath.string());
        scene->Serialize(scene->currentPath);
        spdlog::info("Scene saved");
    }
}

void HandleImageShortcuts(Scene* scene, bool takeViewportScreenshot, bool takeFullScreenshot,
                         bool takeViewportScreenshotWithDialog, bool takeFullScreenshotWithDialog) {
    if (takeViewportScreenshot && scene) {
        TakeViewportScreenshot();
    }

    if (takeFullScreenshot && scene) {
        TakeScreenshot();
    }

    if (takeViewportScreenshotWithDialog && scene) {
        TakeViewportScreenshotWithDialog();
    }

    if (takeFullScreenshotWithDialog && scene) {
        TakeScreenshotWithDialog();
    }
}

void TakeScreenshot() {
    const std::string defaultPath = ".";
    std::string exportPath = Application::Params().Get(Params::UIDefaultExportPath, defaultPath);
    std::string filename = exportPath + "/" + Screenshot::GenerateTimestampedFilename("molehole_screenshot");

    if (Screenshot::CaptureWindow(filename)) {
        spdlog::info("Screenshot saved: {}", filename);
    } else {
        spdlog::error("Failed to take screenshot");
    }
}

void TakeViewportScreenshot() {
    auto& renderer = Application::GetRenderer();

    // Get current viewport dimensions
    int x = static_cast<int>(renderer.m_viewportX);
    int y = static_cast<int>(renderer.m_viewportY);
    int width = static_cast<int>(renderer.m_viewportWidth);
    int height = static_cast<int>(renderer.m_viewportHeight);

    const std::string defaultPath = ".";
    std::string exportPath = Application::Params().Get(Params::UIDefaultExportPath, defaultPath);
    std::string filename = exportPath + "/" + Screenshot::GenerateTimestampedFilename("molehole_viewport");

    if (Screenshot::CaptureViewport(x, y, width, height, filename)) {
        spdlog::info("Viewport screenshot saved: {}", filename);
    } else {
        spdlog::error("Failed to take viewport screenshot");
    }
}

void TakeScreenshotWithDialog() {
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItems[] = {
        { "PNG Image", "png" }
    };

    // Generate a default filename with timestamp
    std::string defaultName = Screenshot::GenerateTimestampedFilename("molehole_screenshot", ".png");
    const std::string defaultPath = ".";
    std::string exportPath = Application::Params().Get(Params::UIDefaultExportPath, defaultPath);

    nfdresult_t result = NFD_SaveDialog(&outPath, filterItems, 1, exportPath.c_str(), defaultName.c_str());

    if (result == NFD_OKAY && outPath) {
        if (Screenshot::CaptureWindow(outPath)) {
            spdlog::info("Screenshot saved: {}", outPath);
        } else {
            spdlog::error("Failed to take screenshot");
        }
        free(outPath);
    }
}

void TakeViewportScreenshotWithDialog() {
    auto& renderer = Application::GetRenderer();

    // Get current viewport dimensions
    int x = static_cast<int>(renderer.m_viewportX);
    int y = static_cast<int>(renderer.m_viewportY);
    int width = static_cast<int>(renderer.m_viewportWidth);
    int height = static_cast<int>(renderer.m_viewportHeight);

    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItems[] = {
        { "PNG Image", "png" }
    };

    // Generate a default filename with timestamp
    std::string defaultName = Screenshot::GenerateTimestampedFilename("molehole_viewport", ".png");
    const std::string defaultPath = ".";
    std::string exportPath = Application::Params().Get(Params::UIDefaultExportPath, defaultPath);

    nfdresult_t result = NFD_SaveDialog(&outPath, filterItems, 1, exportPath.c_str(), defaultName.c_str());

    if (result == NFD_OKAY && outPath) {
        
        if (Screenshot::CaptureViewport(x, y, width, height, outPath)) {
            spdlog::info("Viewport screenshot saved: {}", outPath);
        } else {
            spdlog::error("Failed to take viewport screenshot");
        }
        free(outPath);
        
    }
}

} // namespace TopBar
