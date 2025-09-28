#pragma once

#include <memory>
#include <vector>
#include <string>
#include "Simulation/Scene.h"

class UI {
public:
    UI();
    ~UI();

    void Initialize();
    void Shutdown();

    void BeginFrame();
    void EndFrame();

    void RenderDockspace(Scene* scene);
    void RenderMainUI(float fps, Scene* scene);

    void Update(float deltaTime);

private:
    void RenderMainMenuBar(Scene* scene, bool& doSave, bool& doOpen);
    void RenderSystemWindow(float fps);
    void RenderSceneWindow(Scene* scene);
    void RenderSimulationWindow(Scene* scene);

    void RenderSystemInfoSection(float fps);
    void RenderDisplaySettingsSection();
    void RenderCameraControlsSection();
    void RenderRenderingFlagsSection();

    void RenderScenePropertiesSection(Scene* scene);
    void RenderRecentScenesSection(Scene* scene);

    void RenderSimulationGeneralSection();
    void RenderBlackHolesSection(Scene* scene);

    void HandleFileOperations(Scene* scene, bool doSave, bool doOpen);
    void RenderDebugModeCombo();
    void RenderDebugModeTooltip(int debugMode);
    void LoadScene(Scene* scene, const std::string& path);
    void AddToRecentScenes(const std::string& path);
    void RemoveFromRecentScenes(const std::string& path);

    bool m_showDemoWindow = false;
    bool m_initialized = false;
    bool m_configDirty = false;
    float m_saveTimer = 0.0f;
    static constexpr float SAVE_INTERVAL = 5.0f;
};
