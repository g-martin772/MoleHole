#include "UI.h"
#include "Application.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <set>

UI::UI() {
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
}

void UI::RenderMainMenuBar(Scene* scene, bool& doSave, bool& doOpen) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
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
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show Demo Window", nullptr, &m_showDemoWindow);

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

            RenderDebugModeCombo();
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
            bool open = ImGui::TreeNode("Black Hole");
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
        "LUT Visualization"
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
        default:
            tooltip = "Unknown debug mode";
            break;
    }
    ImGui::SetTooltip("%s", tooltip);
}
