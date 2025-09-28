#pragma once

#include <memory>
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

private:
    void RenderMainMenuBar(Scene* scene, bool& doSave, bool& doOpen);
    void RenderMainWindow(float fps, Scene* scene);

    void RenderSceneInfoSection(Scene* scene);
    void RenderSystemInfoSection(float fps);
    void RenderDisplaySettingsSection();
    void RenderKerrDistortionSection(Scene* scene);
    void RenderBlackHolesSection(Scene* scene);

    void HandleFileOperations(Scene* scene, bool doSave, bool doOpen);
    void RenderDebugModeCombo();
    void RenderDebugModeTooltip(int debugMode);

    bool m_showDemoWindow = false;
    bool m_initialized = false;
};
