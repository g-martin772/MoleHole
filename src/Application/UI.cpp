#include "UI.h"
#include "Application.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include "spdlog/spdlog.h"
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <set>
#include <imgui_node_editor.h>
#include <vector>
#include <memory>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

namespace ed = ax::NodeEditor;

UI::UI() {
    m_AnimationGraph = std::make_unique<AnimationGraph>();
}

UI::~UI() {
    if (m_initialized) {
        Shutdown();
    }
}

void UI::Initialize() {
    if (m_initialized) {
        spdlog::warn("UI already initialized");
        return;
    }

    m_initialized = true;
    spdlog::info("UI initialized successfully");
}

void UI::Shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_configDirty) {
        Application::Instance().SaveState();
        m_configDirty = false;
    }

    m_initialized = false;
}

void UI::Update(float deltaTime) {
    if (!m_initialized) return;

    if (m_configDirty) {
        m_saveTimer += deltaTime;
        if (m_saveTimer >= SAVE_INTERVAL) {
            Application::Instance().SaveState();
            m_configDirty = false;
            m_saveTimer = 0.0f;
            spdlog::debug("Periodic config save completed");
        }
    }
}

void UI::RenderDockspace(Scene* scene) {
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl;
    static bool prevCtrlS = false, prevCtrlO = false;
    bool ctrlS = ctrl && ImGui::IsKeyPressed(ImGuiKey_S);
    bool ctrlO = ctrl && ImGui::IsKeyPressed(ImGuiKey_O);
    bool doSave = false, doOpen = false;

    if (ctrlS && !prevCtrlS) doSave = true;
    if (ctrlO && !prevCtrlO) doOpen = true;
    prevCtrlS = ctrlS;
    prevCtrlO = ctrlO;

    RenderMainMenuBar(scene, doSave, doOpen);
    HandleFileOperations(scene, doSave, doOpen);

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - ImGui::GetFrameHeight()));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    ImGuiWindowFlags dockspace_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##DockSpace", nullptr, dockspace_flags);
    ImGui::DockSpace(ImGui::GetID("DockSpace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(2);
}

void UI::RenderMainUI(float fps, Scene* scene) {
    RenderSystemWindow(fps);
    RenderSceneWindow(scene);
    RenderSimulationWindow(scene);

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }

    if (m_showHelpWindow) {
        RenderHelpWindow();
    }

    if (m_ShowAnimationGraph) {
        RenderAnimationGraphWindow(scene);
    }
}

void UI::RenderMainMenuBar(Scene* scene, bool& doSave, bool& doOpen) {
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
                        AddToRecentScenes(path.string());
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
            ImGui::MenuItem("Show Demo Window", nullptr, &m_showDemoWindow);
            ImGui::MenuItem("Show Animation Graph", nullptr, &m_ShowAnimationGraph);

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
                m_showHelpWindow = true;
            }
            if (ImGui::MenuItem("About")) {

            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void UI::RenderSystemWindow(float fps) {
    ImGui::Begin("System");

    RenderSystemInfoSection(fps);
    RenderDisplaySettingsSection();
    RenderCameraControlsSection();
    RenderRenderingFlagsSection();
    RenderDebugSection();

    ImGui::End();
}

void UI::RenderSceneWindow(Scene* scene) {
    ImGui::Begin("Scene");

    RenderScenePropertiesSection(scene);
    RenderRecentScenesSection(scene);

    ImGui::End();
}

void UI::RenderSimulationWindow(Scene* scene) {
    ImGui::Begin("Simulation");

    RenderSimulationGeneralSection();
    RenderBlackHolesSection(scene);

    ImGui::End();
}

void UI::RenderSystemInfoSection(float fps) {
    if (ImGui::CollapsingHeader("System Info", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("FPS: %.1f", fps);
        ImGui::Text("Frame time: %.3f ms", 1000.0f / fps);
    }
}

void UI::RenderDisplaySettingsSection() {
    if (ImGui::CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool vsync = Application::State().window.vsync;
        if (ImGui::Checkbox("VSync", &vsync)) {
            Application::State().window.vsync = vsync;
            m_configDirty = true;
        }
        ImGui::Text("VSync State: %s", Application::State().window.vsync ? "Enabled" : "Disabled");

        float beamSpacing = Application::State().GetProperty<float>("beamSpacing", 1.0f);
        if (ImGui::DragFloat("Ray spacing", &beamSpacing, 0.01f, 0.0f, 1e10f)) {
            Application::State().SetProperty("beamSpacing", beamSpacing);
            m_configDirty = true;
        }
    }
}

void UI::RenderCameraControlsSection() {
    if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& renderer = Application::GetRenderer();
        auto& camera = renderer.camera;

        float cameraSpeed = Application::State().app.cameraSpeed;
        if (ImGui::DragFloat("Movement Speed", &cameraSpeed, 0.1f, 0.1f, 50.0f)) {
            Application::State().app.cameraSpeed = cameraSpeed;
            m_configDirty = true;
        }

        float mouseSensitivity = Application::State().app.mouseSensitivity;
        if (ImGui::DragFloat("Mouse Sensitivity", &mouseSensitivity, 0.01f, 0.01f, 5.0f)) {
            Application::State().app.mouseSensitivity = mouseSensitivity;
            m_configDirty = true;
        }

        ImGui::Separator();

        if (camera) {
            glm::vec3 position = camera->GetPosition();
            if (ImGui::DragFloat3("Camera Position", &position[0], 0.1f)) {
                camera->SetPosition(position);
                Application::State().UpdateCameraState(position, camera->GetFront(), camera->GetUp(), camera->GetPitch(), camera->GetYaw());
                m_configDirty = true;
            }

            float yaw = camera->GetYaw();
            float pitch = camera->GetPitch();
            bool angleChanged = false;

            if (ImGui::DragFloat("Yaw", &yaw, 0.5f, -180.0f, 180.0f)) {
                angleChanged = true;
            }

            if (ImGui::DragFloat("Pitch", &pitch, 0.5f, -89.0f, 89.0f)) {
                angleChanged = true;
            }

            if (angleChanged) {
                camera->SetYawPitch(yaw, pitch);
                Application::State().UpdateCameraState(camera->GetPosition(), camera->GetFront(), camera->GetUp(), pitch, yaw);
                m_configDirty = true;
            }

            float fov = Application::State().rendering.fov;
            if (ImGui::DragFloat("Field of View", &fov, 0.5f, 10.0f, 120.0f)) {
                Application::State().rendering.fov = fov;
                camera->SetFov(fov);
                m_configDirty = true;
            }

            if (ImGui::Button("Reset Camera Position")) {
                glm::vec3 resetPos(0.0f, 20.0f, 100.0f);
                camera->SetPosition(resetPos);
                camera->SetYawPitch(-90.0f, 0.0f);
                Application::State().UpdateCameraState(resetPos, camera->GetFront(), camera->GetUp(), 0.0f, -90.0f);
                m_configDirty = true;
            }
        }

        ImGui::Separator();
        ImGui::Text("Controls:");
        ImGui::BulletText("WASD - Move");
        ImGui::BulletText("QE - Up/Down");
        ImGui::BulletText("Right Mouse - Look around");
    }
}

void UI::RenderRenderingFlagsSection() {
    if (ImGui::CollapsingHeader("Rendering Flags", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool kerrDistortionEnabled = Application::State().rendering.enableDistortion;
        if (ImGui::Checkbox("Enable Kerr Distortion", &kerrDistortionEnabled)) {
            Application::State().rendering.enableDistortion = kerrDistortionEnabled;
            m_configDirty = true;
        }

        if (Application::State().rendering.enableDistortion) {
            int kerrLutResolution = Application::State().rendering.kerrLutResolution;
            if (ImGui::SliderInt("LUT Resolution", &kerrLutResolution, 32, 256)) {
                Application::State().rendering.kerrLutResolution = kerrLutResolution;
                m_configDirty = true;
            }

            float kerrMaxDistance = Application::State().rendering.kerrMaxDistance;
            if (ImGui::DragFloat("Max Distance", &kerrMaxDistance, 1.0f, 10.0f, 1000.0f)) {
                Application::State().rendering.kerrMaxDistance = kerrMaxDistance;
                m_configDirty = true;
            }

            bool kerrDebugLut = Application::State().GetProperty<bool>("kerrDebugLut", false);
            if (ImGui::Checkbox("Debug Kerr LUT", &kerrDebugLut)) {
                Application::State().SetProperty("kerrDebugLut", kerrDebugLut);
                m_configDirty = true;
            }

            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Kerr LUT overlay: a 2D slice of the 3D table.\n"
                    "Horizontal = polar angle θ (0..π), Vertical = azimuth φ (0..2π).\n"
                    "Slice: fixed distance (X axis of the LUT) at mid-range.\n"
                    "Color: two hues mix encode deflection (R=θ defl, G=φ defl).\n"
                    "Brightness (value) = distance factor (B channel).\n"
                    "Magenta tint = invalid/overflow entries (A == 0).\n"
                    "Faint grid helps gauge indices.");
            }
        }
    }
}

void UI::RenderDebugSection() {
    if (ImGui::CollapsingHeader("Debug Visualization", ImGuiTreeNodeFlags_DefaultOpen)) {
        RenderDebugModeCombo();

        if (Application::State().rendering.debugMode == DebugMode::GravityGrid) {
            auto& renderer = Application::GetRenderer();
            if (auto* grid = renderer.GetGravityGridRenderer()) {
                ImGui::Separator();
                ImGui::TextDisabled("Gravity Grid (Plane) Settings");

                float planeY = grid->GetPlaneY();
                if (ImGui::DragFloat("Plane Y", &planeY, 0.5f, -10000.0f, 10000.0f)) {
                    grid->SetPlaneY(planeY);
                }
                float size = grid->GetPlaneSize();
                if (ImGui::DragFloat("Plane Size", &size, 1.0f, 2.0f, 10000.0f)) {
                    grid->SetPlaneSize(size);
                }
                int res = grid->GetResolution();
                if (ImGui::SliderInt("Resolution", &res, 8, 512)) {
                    grid->SetResolution(res);
                }
                float cellSize = grid->GetCellSize();
                if (ImGui::DragFloat("Grid Cell Size", &cellSize, 0.05f, 0.01f, 100.0f)) {
                    grid->SetCellSize(cellSize);
                }
                float lineThickness = grid->GetLineThickness();
                if (ImGui::DragFloat("Line Thickness (cells)", &lineThickness, 0.005f, 0.001f, 0.5f)) {
                    grid->SetLineThickness(lineThickness);
                }
                float opacity = grid->GetOpacity();
                if (ImGui::SliderFloat("Opacity", &opacity, 0.05f, 1.0f)) {
                    grid->SetOpacity(opacity);
                }

                glm::vec3 color = grid->GetColor();
                if (ImGui::ColorEdit3("Grid Color", &color[0])) {
                    grid->SetColor(color);
                }
            }
        }
    }
}

void UI::RenderScenePropertiesSection(Scene* scene) {
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
}

void UI::RenderRecentScenesSection(Scene* scene) {
    if (ImGui::CollapsingHeader("Recent Scenes", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& recentScenes = Application::State().app.recentScenes;

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
            recentScenes.erase(recentScenes.begin() + *it);
            m_configDirty = true;
        }

        if (recentScenes.empty()) {
            ImGui::TextDisabled("No recent scenes");
        }
    }
}

void UI::RenderSimulationGeneralSection() {
    if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextDisabled("Simulation controls maybe...");
        ImGui::Separator();
    }
}

void UI::RenderBlackHolesSection(Scene* scene) {
    if (ImGui::CollapsingHeader("Black Holes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!scene) {
            ImGui::TextDisabled("No scene loaded");
            return;
        }

        if (scene->HasSelection()) {
            ImGui::Text("Transform Controls:");
            ImGui::SameLine();

            if (ImGui::RadioButton("Translate", m_currentGizmoOperation == GizmoOperation::Translate)) {
                m_currentGizmoOperation = GizmoOperation::Translate;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate", m_currentGizmoOperation == GizmoOperation::Rotate)) {
                m_currentGizmoOperation = GizmoOperation::Rotate;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale", m_currentGizmoOperation == GizmoOperation::Scale)) {
                m_currentGizmoOperation = GizmoOperation::Scale;
            }

            ImGui::Checkbox("Use Snap", &m_useSnap);
            if (m_useSnap) {
                switch (m_currentGizmoOperation) {
                    case GizmoOperation::Translate:
                        ImGui::DragFloat3("Snap", m_snapTranslate, 0.1f);
                        break;
                    case GizmoOperation::Rotate:
                        ImGui::DragFloat("Snap", &m_snapRotate, 1.0f);
                        break;
                    case GizmoOperation::Scale:
                        ImGui::DragFloat("Snap", &m_snapScale, 0.01f);
                        break;
                }
            }

            if (ImGui::Button("Deselect")) {
                scene->ClearSelection();
            }

            ImGui::Separator();
        }

        if (ImGui::Button("Add Black Hole")) {
            BlackHole newHole;
            newHole.mass = 10.0f;
            newHole.position = glm::vec3(0.0f, 0.0f, -5.0f);
            newHole.showAccretionDisk = true;
            newHole.accretionDiskDensity = 1.0f;
            newHole.accretionDiskSize = 15.0f;
            newHole.accretionDiskColor = glm::vec3(1.0f, 0.5f, 0.0f);
            newHole.spinAxis = glm::vec3(0.0f, 1.0f, 0.0f);
            newHole.spin = 0.5f;
            scene->blackHoles.push_back(newHole);
        }

        ImGui::Text("Black Holes: %zu", scene->blackHoles.size());

        int idx = 0;
        for (auto it = scene->blackHoles.begin(); it != scene->blackHoles.end();) {
            ImGui::PushID(idx);

            bool isSelected = scene->HasSelection() &&
                             scene->selectedObject->type == Scene::ObjectType::BlackHole &&
                             scene->selectedObject->index == static_cast<size_t>(idx);

            if (isSelected) {
                ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.3f, 0.7f, 1.0f, 0.6f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.4f, 0.8f, 1.0f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.5f, 0.9f, 1.0f, 1.0f));
            }

            std::string label = "Black Hole #" + std::to_string(idx + 1);
            if (isSelected) {
                label += " (Selected)";
            }

            bool open = ImGui::TreeNode(label.c_str());

            if (isSelected) {
                ImGui::PopStyleColor(3);
            }

            ImGui::SameLine();
            if (ImGui::Button("Select")) {
                scene->SelectObject(Scene::ObjectType::BlackHole, idx);
            }
            ImGui::SameLine();
            bool remove = ImGui::Button("Remove");

            if (open) {
                BlackHole& bh = *it;
                bool bhChanged = false;

                if (ImGui::DragFloat("Mass", &bh.mass, 0.02f, 0.0f, 1e10f)) bhChanged = true;
                if (ImGui::DragFloat("Spin", &bh.spin, 0.01f, 0.0f, 1.0f)) bhChanged = true;
                if (ImGui::DragFloat3("Position", &bh.position[0], 0.05f)) bhChanged = true;
                if (ImGui::DragFloat3("Spin Axis", &bh.spinAxis[0], 0.01f, -1.0f, 1.0f)) bhChanged = true;

                ImGui::Separator();
                ImGui::Text("Accretion Disk");
                ImGui::Checkbox("Show Accretion Disk", &bh.showAccretionDisk);
                ImGui::DragFloat("Density", &bh.accretionDiskDensity, 0.01f, 0.0f, 1e6f);
                ImGui::DragFloat("Size", &bh.accretionDiskSize, 0.01f, 0.0f, 1e6f);
                ImGui::ColorEdit3("Color", &bh.accretionDiskColor[0]);

                if (bhChanged) {
                    if (!scene->currentPath.empty()) {
                        scene->Serialize(scene->currentPath);
                    }
                }

                ImGui::TreePop();
            }

            ImGui::PopID();

            if (remove) {
                if (isSelected) {
                    scene->ClearSelection();
                }
                it = scene->blackHoles.erase(it);
                if (!scene->currentPath.empty()) {
                    scene->Serialize(scene->currentPath);
                }
            } else {
                ++it;
            }
            idx++;
        }
    }
}

void UI::RenderAnimationGraphWindow(Scene *scene) {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Animation Graph");

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float closeButtonSize = ImGui::CalcTextSize("Close").x + style.FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(avail.x - closeButtonSize);
    if (ImGui::Button("Close")) {
        m_ShowAnimationGraph = false;
    }

    ImGui::SetCursorPos({0,20});

    if (m_AnimationGraph)
        m_AnimationGraph->Render();

    ImGui::End();
}

void UI::RenderImGuizmo(Scene* scene) {
    if (!scene || !scene->HasSelection()) {
        return;
    }

    auto& renderer = Application::GetRenderer();
    if (!renderer.camera) {
        return;
    }

    glm::vec3* position = scene->GetSelectedObjectPosition();
    if (!position) {
        return;
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();

    // Use the viewport bounds from the renderer
    ImGuizmo::SetRect(renderer.m_viewportX, renderer.m_viewportY,
                     renderer.m_viewportWidth, renderer.m_viewportHeight);

    glm::mat4 view = renderer.camera->GetViewMatrix();
    glm::mat4 projection = renderer.camera->GetProjectionMatrix();

    glm::mat4 transform = glm::translate(glm::mat4(1.0f), *position);

    ImGuizmo::OPERATION operation;
    switch (m_currentGizmoOperation) {
        case GizmoOperation::Translate:
            operation = ImGuizmo::TRANSLATE;
            break;
        case GizmoOperation::Rotate:
            operation = ImGuizmo::ROTATE;
            break;
        case GizmoOperation::Scale:
            operation = ImGuizmo::SCALE;
            break;
    }

    float* snap = nullptr;
    if (m_useSnap) {
        switch (m_currentGizmoOperation) {
            case GizmoOperation::Translate:
                snap = m_snapTranslate;
                break;
            case GizmoOperation::Rotate:
                snap = &m_snapRotate;
                break;
            case GizmoOperation::Scale:
                snap = &m_snapScale;
                break;
        }
    }

    ImGuizmo::Enable(true);

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                             operation, ImGuizmo::LOCAL, glm::value_ptr(transform),
                             nullptr, snap)) {

        glm::vec3 translation, scale;
        glm::quat rotation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(transform, scale, rotation, translation, skew, perspective);

        *position = translation;

        spdlog::debug("Object moved to: ({:.2f}, {:.2f}, {:.2f})",
                     position->x, position->y, position->z);
    }
}

void UI::HandleFileOperations(Scene* scene, bool doSave, bool doOpen) {
    if (doOpen && scene) {
        auto path = Scene::ShowFileDialog(false);
        if (!path.empty()) {
            LoadScene(scene, path.string());
        }
    }

    if (doSave && scene && !scene->currentPath.empty()) {
        spdlog::info("Saving scene: {}", scene->currentPath.string());
        scene->Serialize(scene->currentPath);
        spdlog::info("Scene saved");
    }
}

void UI::LoadScene(Scene* scene, const std::string& path) {
    if (!scene || path.empty()) {
        return;
    }

    try {
        std::filesystem::path fsPath(path);
        if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
            RemoveFromRecentScenes(path);
            return;
        }

        std::string currentScenePath = scene->currentPath.string();
        if (currentScenePath == path) {
            return;
        }

        spdlog::info("Loading scene: {}", path);

        Scene tempScene = *scene;
        tempScene.Deserialize(fsPath);
        *scene = tempScene;

        Application::Instance().GetState().SetLastOpenScene(path);
        AddToRecentScenes(path);

        spdlog::info("Scene loaded successfully: {}", path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load scene '{}': {}", path, e.what());
        RemoveFromRecentScenes(path);
    }
}

void UI::AddToRecentScenes(const std::string& path) {
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

    m_configDirty = true;
}

void UI::RemoveFromRecentScenes(const std::string& path) {
    auto& recentScenes = Application::State().app.recentScenes;

    auto it = std::find(recentScenes.begin(), recentScenes.end(), path);
    if (it != recentScenes.end()) {
        recentScenes.erase(it);
        m_configDirty = true;
    }
}

void UI::RenderDebugModeCombo() {
    const char* debugModeItems[] = {
        "Normal Rendering",
        "Influence Zones",
        "Deflection Magnitude",
        "Gravitational Field",
        "Spherical Shape",
        "LUT Visualization",
        "Gravity Grid"
    };

    int debugMode = static_cast<int>(Application::State().rendering.debugMode);
    if (ImGui::Combo("Debug Mode", &debugMode, debugModeItems, IM_ARRAYSIZE(debugModeItems))) {
        Application::State().rendering.debugMode = static_cast<DebugMode>(debugMode);
        m_configDirty = true;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        RenderDebugModeTooltip(debugMode);
    }
}

void UI::RenderDebugModeTooltip(int debugMode) {
    const char* tooltip = "";
    switch (debugMode) {
        case 0:
            tooltip = "Normal rendering with no debug visualization";
            break;
        case 1:
            tooltip = "Red zones showing gravitational influence areas\nBrighter red = closer to black hole\nOnly shows outside event horizon safety zone";
            break;
        case 2:
            tooltip = "Yellow/orange visualization of deflection strength\nBrightness indicates how much light rays are bent\nHelps visualize Kerr distortion effects";
            break;
        case 3:
            tooltip = "Green visualization of gravitational field strength\nBrighter green = stronger gravitational effects\nShows field within 10x Schwarzschild radius";
            break;
        case 4:
            tooltip = "Blue gradient showing black hole's spherical shape\nBlack interior = event horizon (no escape)\nBlue gradient = distance from event horizon\nHelps verify proper sphere geometry";
            break;
        case 5:
            tooltip = "Visualize the distortion lookup table (LUT)\n2D slice of the 3D LUT used for ray deflection\nHue encodes deflection direction, brightness encodes distance\nMagenta tint indicates invalid/overflow entries";
            break;
        case 6:
            tooltip = "Gravity Grid overlay on ground plane\nColor shows dominant black hole per cell (by mass/distance^2)\nGrid helps visualize regions of influence";
            break;
        default:
            tooltip = "Unknown debug mode";
            break;
    }
    ImGui::SetTooltip("%s", tooltip);
}

void UI::RenderHelpWindow() {
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("MoleHole - Help", &m_showHelpWindow)) {
        ImGui::End();
        return;
    }

    ImGui::BeginChild("HelpContent", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));

    if (ImGui::BeginTabBar("HelpTabs")) {
        if (ImGui::BeginTabItem("Getting Started")) {
            ImGui::TextWrapped("Welcome to MoleHole - Black Hole Physics Simulation");
            ImGui::Separator();

            ImGui::Text("Quick Start:");
            ImGui::BulletText("Create a new scene or load an existing one from File menu");
            ImGui::BulletText("Add black holes using the Simulation panel");
            ImGui::BulletText("Adjust rendering settings in the System panel");
            ImGui::BulletText("Navigate using mouse and keyboard controls");

            ImGui::Spacing();
            ImGui::Text("Basic Workflow:");
            ImGui::BulletText("1. Set up your scene with black holes");
            ImGui::BulletText("2. Configure physics and rendering parameters");
            ImGui::BulletText("3. Use debug modes to visualize effects");
            ImGui::BulletText("4. Save your scene for later use");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Controls")) {
            ImGui::Text("Camera Movement:");
            ImGui::BulletText("W/A/S/D - Move forward/left/backward/right");
            ImGui::BulletText("Q/E - Move up/down");
            ImGui::BulletText("Right Mouse + Drag - Look around");
            ImGui::BulletText("Mouse Wheel - Zoom in/out");

            ImGui::Separator();
            ImGui::Text("Keyboard Shortcuts:");
            ImGui::BulletText("Ctrl+O - Open scene file");
            ImGui::BulletText("Ctrl+S - Save current scene");
            ImGui::BulletText("F1 - Toggle this help window");
            ImGui::BulletText("ESC - Close dialogs/windows");

            ImGui::Separator();
            ImGui::Text("Camera Settings:");
            ImGui::BulletText("Movement Speed - Controls how fast you move through the scene");
            ImGui::BulletText("Mouse Sensitivity - Controls how responsive camera rotation is");
            ImGui::BulletText("Adjust these in System > Camera Controls");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Physics")) {
            ImGui::Text("Black Hole Properties:");
            ImGui::BulletText("Mass - Determines gravitational strength and event horizon size");
            ImGui::BulletText("Spin - Kerr rotation parameter (0=Schwarzschild, 1=maximal)");
            ImGui::BulletText("Position - 3D location of the black hole center");
            ImGui::BulletText("Spin Axis - Direction of rotation axis");

            ImGui::Separator();
            ImGui::Text("Accretion Disk:");
            ImGui::BulletText("Density - How thick/bright the disk appears");
            ImGui::BulletText("Size - Outer radius of the accretion disk");
            ImGui::BulletText("Color - RGB color tint for the disk material");

            ImGui::Separator();
            ImGui::Text("Kerr Physics:");
            ImGui::BulletText("Kerr metric describes rotating black holes");
            ImGui::BulletText("Spin affects light ray deflection and spacetime curvature");
            ImGui::BulletText("Frame dragging causes space itself to rotate near the black hole");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Rendering")) {
            ImGui::Text("Kerr Distortion:");
            ImGui::BulletText("Enable to simulate gravitational lensing effects");
            ImGui::BulletText("LUT Resolution controls accuracy vs performance");
            ImGui::BulletText("Max Distance sets the simulation boundary");

            ImGui::Separator();
            ImGui::Text("Debug Modes:");
            ImGui::BulletText("Normal - Standard rendering with physics effects");
            ImGui::BulletText("Influence Zones - Red areas show gravitational influence");
            ImGui::BulletText("Deflection Magnitude - Yellow shows light ray bending");
            ImGui::BulletText("Gravitational Field - Green shows field strength");
            ImGui::BulletText("Spherical Shape - Blue shows black hole geometry");
            ImGui::BulletText("LUT Visualization - Shows the distortion lookup table");
            ImGui::BulletText("Gravity Grid - Grid overlay showing gravitational influence regions");

            ImGui::Separator();
            ImGui::Text("Performance Tips:");
            ImGui::BulletText("Lower LUT Resolution for better performance");
            ImGui::BulletText("Reduce Max Distance if experiencing slowdowns");
            ImGui::BulletText("Disable Kerr Distortion for faster rendering");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Scene Management")) {
            ImGui::Text("File Operations:");
            ImGui::BulletText("Open Scene - Load a previously saved scene file");
            ImGui::BulletText("Save Scene - Save changes to current scene file");
            ImGui::BulletText("Save Scene As - Save with a new filename");

            ImGui::Separator();
            ImGui::Text("Recent Scenes:");
            ImGui::BulletText("Recently opened scenes appear in the Scene panel");
            ImGui::BulletText("Click any recent scene to quickly switch to it");
            ImGui::BulletText("Green highlight indicates the currently active scene");
            ImGui::BulletText("X button removes scenes from recent list");

            ImGui::Separator();
            ImGui::Text("Scene Properties:");
            ImGui::BulletText("Scene Name - Descriptive name for your scene");
            ImGui::BulletText("Path - Shows where the scene file is saved");
            ImGui::BulletText("Changes auto-save when you modify scene properties");

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Troubleshooting")) {
            ImGui::Text("Common Issues:");

            ImGui::Text("Poor Performance:");
            ImGui::BulletText("Lower Kerr LUT Resolution (32-64 for low-end systems)");
            ImGui::BulletText("Reduce Max Distance setting");
            ImGui::BulletText("Disable Kerr Distortion temporarily");
            ImGui::BulletText("Close unnecessary debug visualizations");

            ImGui::Separator();
            ImGui::Text("Scene Loading Problems:");
            ImGui::BulletText("Check file permissions and disk space");
            ImGui::BulletText("Verify scene file isn't corrupted");
            ImGui::BulletText("Remove invalid entries from recent scenes");

            ImGui::Separator();
            ImGui::Text("Visual Issues:");
            ImGui::BulletText("Try different debug modes to isolate problems");
            ImGui::BulletText("Check black hole positions aren't overlapping");
            ImGui::BulletText("Verify spin values are between 0 and 1");
            ImGui::BulletText("Reset camera position if view seems stuck");

            ImGui::Separator();
            ImGui::Text("System Requirements:");
            ImGui::BulletText("Modern GPU with OpenGL 4.6 support");
            ImGui::BulletText("At least 4GB RAM recommended");
            ImGui::BulletText("Updated graphics drivers");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndChild();

    ImGui::Separator();
    if (ImGui::Button("Close Help")) {
        m_showHelpWindow = false;
    }

    ImGui::End();
}
