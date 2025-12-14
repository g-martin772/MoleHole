//
// Created by leo on 11/28/25.
//

#include "TopBar.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
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

    auto& recentScenes = Application::State().app.recentScenes;

    auto it = std::find(recentScenes.begin(), recentScenes.end(), path);
    if (it != recentScenes.end()) {
        recentScenes.erase(it);
    }

    recentScenes.insert(recentScenes.begin(), path);

    static constexpr size_t MAX_RECENT_SCENES = 10;
    if (recentScenes.size() > MAX_RECENT_SCENES) {
        recentScenes.resize(MAX_RECENT_SCENES);
    }

    ui->MarkConfigDirty();
}

static void RemoveFromRecentScenes(UI* ui, const std::string& path) {
    auto& recentScenes = Application::State().app.recentScenes;

    auto it = std::find(recentScenes.begin(), recentScenes.end(), path);
    if (it != recentScenes.end()) {
        recentScenes.erase(it);
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

        Application::Instance().GetState().SetLastOpenScene(path);
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
            if (ImGui::MenuItem("New")) {
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
                        Application::Instance().GetState().SetLastOpenScene(path.string());
                        AddToRecentScenes(ui, path.string());
                    }
                }
            }

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
                                    Application::Instance().GetState().SetLastOpenScene("");
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

        if (ImGui::BeginMenu("Edit")) {
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Demo Window", nullptr, ui->GetShowDemoWindowPtr());
            ImGui::MenuItem("Show Animation Graph", nullptr, ui->GetShowAnimationGraphPtr());
            ImGui::MenuItem("Show Export Window", nullptr, ui->GetShowExportWindowPtr());

            ImGui::Separator();
            auto& renderer = Application::GetRenderer();
            auto currentMode = renderer.GetSelectedViewport();

            bool selectedDemo1 = (currentMode == Renderer::ViewportMode::Demo1);
            bool selectedRays2D = (currentMode == Renderer::ViewportMode::Rays2D);
            bool selectedSim3D = (currentMode == Renderer::ViewportMode::Simulation3D);

            if (ImGui::MenuItem("Demo1 Viewport", nullptr, selectedDemo1)) {
                renderer.SetSelectedViewport(Renderer::ViewportMode::Demo1);
            }
            if (ImGui::MenuItem("2D Rays Viewport", nullptr, selectedRays2D)) {
                renderer.SetSelectedViewport(Renderer::ViewportMode::Rays2D);
            }
            if (ImGui::MenuItem("3D Simulation Viewport", nullptr, selectedSim3D)) {
                renderer.SetSelectedViewport(Renderer::ViewportMode::Simulation3D);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Help", "F1")) {
                *ui->GetShowHelpWindowPtr() = true;
            }
            if (ImGui::MenuItem("About")) {

            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Image")) {
            if (ImGui::MenuItem("Take Screenshot (choose location)", "F12")) {
                doTakeScreenshotViewportDialog = true;
            }
            if (ImGui::MenuItem("Take Screenshot from whole screen (choose location)", "F11")) {
                doTakeScreenshotDialog = true;
            }
            if (ImGui::MenuItem("Take Screenshot (default path)", "F12 + S")) {
                doTakeScreenshotViewport = true;
            }
            if (ImGui::MenuItem("Take Screenshot from whole screen (default path)", "F11 + S")) {
                doTakeScreenshot = true;
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
    std::string exportPath = Application::State().GetProperty<std::string>("defaultExportPath", ".");
    std::string filename = exportPath + "/" + Screenshot::GenerateTimestampedFilename("molehole_screenshot");

    if (Screenshot::CaptureWindow(filename)) {
        spdlog::info("Screenshot saved: {}", filename);
    } else {
        spdlog::error("Failed to take screenshot");
    }
}

void TakeViewportScreenshot() {
    auto& renderer = Application::GetRenderer();
    auto& ui = Application::Instance().GetUI();

    // Get current viewport dimensions
    int x = static_cast<int>(renderer.m_viewportX);
    int y = static_cast<int>(renderer.m_viewportY);
    int width = static_cast<int>(renderer.m_viewportWidth);
    int height = static_cast<int>(renderer.m_viewportHeight);

    std::string exportPath = Application::State().GetProperty<std::string>("defaultExportPath", ".");
    std::string filename = exportPath + "/" + Screenshot::GenerateTimestampedFilename("molehole_viewport");

    // Set flag to hide UI controls, render one frame, then capture
    ui.SetTakingScreenshot(true);
    glfwPollEvents(); // Process events
    Application::Instance().Render(); // Render without controls
    glfwSwapBuffers(renderer.GetWindow()); // Present
    
    if (Screenshot::CaptureViewport(x, y, width, height, filename)) {
        spdlog::info("Viewport screenshot saved: {}", filename);
    } else {
        spdlog::error("Failed to take viewport screenshot");
    }
    
    ui.SetTakingScreenshot(false);
}

void TakeScreenshotWithDialog() {
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItems[] = {
        { "PNG Image", "png" }
    };

    // Generate a default filename with timestamp
    std::string defaultName = Screenshot::GenerateTimestampedFilename("molehole_screenshot", ".png");
    std::string exportPath = Application::State().GetProperty<std::string>("defaultExportPath", ".");

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
    auto& ui = Application::Instance().GetUI();

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
    std::string exportPath = Application::State().GetProperty<std::string>("defaultExportPath", ".");

    nfdresult_t result = NFD_SaveDialog(&outPath, filterItems, 1, exportPath.c_str(), defaultName.c_str());

    if (result == NFD_OKAY && outPath) {
        // Set flag to hide UI controls, render one frame, then capture
        ui.SetTakingScreenshot(true);
        glfwPollEvents(); // Process events
        Application::Instance().Render(); // Render without controls
        glfwSwapBuffers(renderer.GetWindow()); // Present
        
        if (Screenshot::CaptureViewport(x, y, width, height, outPath)) {
            spdlog::info("Viewport screenshot saved: {}", outPath);
        } else {
            spdlog::error("Failed to take viewport screenshot");
        }
        
        ui.SetTakingScreenshot(false);
        free(outPath);
    }
}

} // namespace TopBar
