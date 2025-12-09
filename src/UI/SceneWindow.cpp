//
// Created by leo on 11/28/25.
//

#include "SceneWindow.h"
#include "../Application/UI.h"
#include "../Application/Application.h"
#include "../Simulation/Scene.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <filesystem>
#include <algorithm>
#include <set>
#include <vector>
#include <cstring>

namespace SceneWindow {

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

    auto recentScenes = Application::State().GetStringVector(StateParameter::AppRecentScenes);

    auto it = std::find(recentScenes.begin(), recentScenes.end(), path);
    if (it != recentScenes.end()) {
        recentScenes.erase(it);
    }

    recentScenes.insert(recentScenes.begin(), path);

    static constexpr size_t MAX_RECENT_SCENES = 10;
    if (recentScenes.size() > MAX_RECENT_SCENES) {
        recentScenes.resize(MAX_RECENT_SCENES);
    }

    Application::State().SetStringVector(StateParameter::AppRecentScenes, recentScenes);
    ui->MarkConfigDirty();
}

static void RemoveFromRecentScenes(UI* ui, const std::string& path) {
    auto recentScenes = Application::State().GetStringVector(StateParameter::AppRecentScenes);

    auto it = std::find(recentScenes.begin(), recentScenes.end(), path);
    if (it != recentScenes.end()) {
        recentScenes.erase(it);
        Application::State().SetStringVector(StateParameter::AppRecentScenes, recentScenes);
        ui->MarkConfigDirty();
    }
}

void LoadScene(Scene* scene, const std::string& path) {
    if (!scene || path.empty()) {
        return;
    }

    try {
        std::filesystem::path fsPath(path);
        if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
            // Note: Can't call RemoveFromRecentScenes here without UI* parameter
            return;
        }

        std::string currentScenePath = scene->currentPath.string();
        if (currentScenePath == path) {
            return;
        }

        spdlog::info("Loading scene: {}", path);

        scene->Deserialize(fsPath);

        Application::Instance().GetState().SetString(StateParameter::AppLastOpenScene, path);
        // Note: Can't call AddToRecentScenes here without UI* parameter

        spdlog::info("Scene loaded successfully: {}", path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load scene '{}': {}", path, e.what());
    }
}

void Render(UI* ui, Scene* scene) {
    if (!ImGui::Begin("Scene")) {
        ImGui::End();
        return;
    }

    // Scene Properties Section
    if (ImGui::CollapsingHeader("Scene Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (scene) {
            char nameBuffer[128];
            std::strncpy(nameBuffer, scene->name.c_str(), sizeof(nameBuffer));
            nameBuffer[sizeof(nameBuffer) - 1] = '\0';
            if (ImGui::InputText("Scene Name", nameBuffer, sizeof(nameBuffer))) {
                scene->name = nameBuffer;
                if (!scene->currentPath.empty()) {
                    scene->Serialize(scene->currentPath);
                    spdlog::info("Scene name changed, auto-saved to: {}", scene->currentPath.string());
                }
            }

            if (!scene->currentPath.empty()) {
                std::string pathStr = scene->currentPath.string();
                ImGui::Text("Path: %s", pathStr.c_str());
            } else {
                ImGui::TextDisabled("Unsaved scene");
            }
        } else {
            ImGui::TextDisabled("No scene loaded");
        }
    }

    // Recent Scenes Section
    if (ImGui::CollapsingHeader("Recent Scenes", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto recentScenes = Application::State().GetStringVector(StateParameter::AppRecentScenes);

        std::vector<size_t> indicesToRemove;
        std::set<std::string> uniquePaths;
        std::string currentScenePath = scene ? scene->currentPath.string() : "";

        for (size_t i = 0; i < recentScenes.size(); ++i) {
            const std::string& scenePath = recentScenes[i];

            if (scenePath.empty() || uniquePaths.count(scenePath) > 0) {
                indicesToRemove.push_back(i);
                continue;
            }
            uniquePaths.insert(scenePath);

            std::filesystem::path path;
            bool validPath = false;
            bool isCurrentScene = (scenePath == currentScenePath);
            std::string displayName = "Invalid Path";

            try {
                path = std::filesystem::path(scenePath);
                displayName = path.filename().string();
                if (displayName.empty()) {
                    displayName = path.string();
                }
                validPath = std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
            } catch (const std::exception&) {
                validPath = false;
                displayName = "Invalid Path";
            }

            if (!validPath && !isCurrentScene) {
                indicesToRemove.push_back(i);
                continue;
            }

            ImGui::PushID(static_cast<int>(i));
            ImGui::BeginGroup();

            if (isCurrentScene) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.2f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.8f, 0.3f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
            }

            if (ImGui::Button(displayName.c_str(), ImVec2(-80, 0))) {
                if (!isCurrentScene) {
                    LoadScene(scene, scenePath);
                    AddToRecentScenes(ui, scenePath);
                }
            }

            if (isCurrentScene) {
                ImGui::PopStyleColor(3);
            }

            ImGui::SameLine();
            if (ImGui::Button("X", ImVec2(25, 0))) {
                indicesToRemove.push_back(i);
            }

            ImGui::EndGroup();

            if (ImGui::IsItemHovered()) {
                if (isCurrentScene) {
                    ImGui::SetTooltip("Current scene: %s", scenePath.c_str());
                } else if (validPath) {
                    ImGui::SetTooltip("%s", scenePath.c_str());
                } else {
                    ImGui::SetTooltip("File not found: %s", scenePath.c_str());
                }
            }

            ImGui::PopID();
        }

        for (auto it = indicesToRemove.rbegin(); it != indicesToRemove.rend(); ++it) {
            recentScenes.erase(recentScenes.begin() + static_cast<std::vector<std::string>::difference_type>(*it));
            ui->MarkConfigDirty();
        }

        if (recentScenes.empty()) {
            ImGui::TextDisabled("No recent scenes");
        }
    }

    ImGui::End();
}

} // namespace SceneWindow
