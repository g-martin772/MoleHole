//
// Created by leo on 11/28/25.
//

#include "SceneWindow.h"
#include "UI.h"
#include "../Application/Application.h"
#include "../Application/Parameters.h"
#include "../Simulation/Scene.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <filesystem>
#include <algorithm>
#include <set>
#include <vector>
#include <cstring>
#include <nfd.h>

#include "ParameterWidgets.h"

namespace SceneWindow {

static void RenderSectionHeader(const char* title) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f));
    ImGui::Text("%s", title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

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

        Application::Params().Set(Params::AppLastOpenScene, path);
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

    RenderSectionHeader("CURRENT SCENE");

    if (scene) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Scene Name");
        char nameBuffer[128];
        std::strncpy(nameBuffer, scene->name.c_str(), sizeof(nameBuffer));
        nameBuffer[sizeof(nameBuffer) - 1] = '\0';
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##SceneName", nameBuffer, sizeof(nameBuffer))) {
            scene->name = nameBuffer;
            if (!scene->currentPath.empty()) {
                scene->Serialize(scene->currentPath);
            }
        }

        ImGui::Spacing();

        if (ImGui::BeginTable("SaveGrid", 2, ImGuiTableFlags_None)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            bool canSave = !scene->currentPath.empty();
            if (!canSave) ImGui::BeginDisabled();
            if (ImGui::Button("Save Scene", ImVec2(-1, 0))) {
                scene->Serialize(scene->currentPath);
                spdlog::info("Scene saved to: {}", scene->currentPath.string());
            }
            if (!canSave) ImGui::EndDisabled();

            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Save As...", ImVec2(-1, 0))) {
                auto path = Scene::ShowFileDialog(true);
                if (!path.empty()) {
                    scene->Serialize(path);
                    Application::Params().Set(Params::AppLastOpenScene, path.string());
                    AddToRecentScenes(ui, path.string());
                }
            }
            ImGui::PopStyleColor(3);

            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("No scene loaded");
    }

    RenderSectionHeader("RECENT SCENES");

    auto recentScenes = Application::Params().Get(Params::AppRecentScenes, std::vector<std::string>());
    std::vector<size_t> indicesToRemove;
    std::set<std::string> uniquePaths;
    std::string currentScenePath = scene ? scene->currentPath.string() : "";

    ImGui::BeginChild("RecentScenesList", ImVec2(0, 200), false);

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
        std::string dateStr;

        try {
            path = std::filesystem::path(scenePath);
            displayName = path.filename().string();
            if (displayName.empty()) {
                displayName = path.string();
            }
            validPath = std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
            if (validPath) {
                auto ftime = std::filesystem::last_write_time(path);
                auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                auto tt = std::chrono::system_clock::to_time_t(sctp);
                std::tm tm_buf{};
                localtime_r(&tt, &tm_buf);
                char dateBuf[32];
                std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", &tm_buf);
                dateStr = dateBuf;
            }
        } catch (const std::exception&) {
            validPath = false;
            displayName = "Invalid Path";
        }

        if (!validPath && !isCurrentScene) {
            indicesToRemove.push_back(i);
            continue;
        }

        ImGui::PushID(static_cast<int>(i));

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.1f, 0.1f, 0.1f, 0.5f));
        ImGui::BeginChild(("RecentItem" + std::to_string(i)).c_str(), ImVec2(-1, 48), true);

        if (isCurrentScene) {
            ImGui::TextColored(ImVec4(180.0f/255.0f, 100.0f/255.0f, 40.0f/255.0f, 1.0f), "%s", displayName.c_str());
        } else {
            ImGui::Text("%s", displayName.c_str());
        }
        if (!dateStr.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%s", dateStr.c_str());
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !isCurrentScene) {
            LoadScene(scene, scenePath);
            AddToRecentScenes(ui, scenePath);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", scenePath.c_str());
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

    ImGui::EndChild();

    ImGui::Spacing();

    if (ImGui::BeginTable("BottomGrid", 2, ImGuiTableFlags_None)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        if (ImGui::Button("Load Scene", ImVec2(-1, 0))) {
            if (scene) {
                auto path = Scene::ShowFileDialog(false);
                if (!path.empty()) {
                    LoadScene(scene, path.string());
                    AddToRecentScenes(ui, path.string());
                }
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.15f, 0.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Clear Scene", ImVec2(-1, 0))) {
            if (scene) {
                Application::Instance().NewScene();
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::EndTable();
    }

    ImGui::End();
}

} // namespace SceneWindow
