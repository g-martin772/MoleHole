#pragma once

#include <string>
#include "Simulation/Scene.h"
#include "AnimationGraph.h"

class UI {
public:
    UI();
    ~UI();

    void Initialize();
    void Shutdown();

    void RenderDockspace(Scene* scene);
    void RenderMainUI(float fps, Scene* scene);
    void RenderSimulationControls();

    void Update(float deltaTime);

    AnimationGraph* GetAnimationGraph() const { return m_AnimationGraph.get(); }

    enum class GizmoOperation { Translate, Rotate, Scale };
    GizmoOperation GetCurrentGizmoOperation() const { return m_currentGizmoOperation; }
    bool IsUsingSnap() const { return m_useSnap; }
    const float* GetSnapTranslate() const { return m_snapTranslate; }
    float GetSnapRotate() const { return m_snapRotate; }
    float GetSnapScale() const { return m_snapScale; }

    void TakeScreenshot();
    void TakeViewportScreenshot();
    void TakeScreenshotWithDialog();
    void TakeViewportScreenshotWithDialog();

    void ShowExportWindow(Scene* scene);

private:
    void RenderMainMenuBar(Scene* scene, bool& doSave, bool& doOpen);
    void RenderSystemWindow(float fps);
    void RenderSceneWindow(Scene* scene);
    void RenderSimulationWindow(Scene* scene);
    void RenderHelpWindow();

    void RenderSystemInfoSection(float fps);
    void RenderDisplaySettingsSection();
    void RenderCameraControlsSection();
    void RenderRenderingFlagsSection();
    void RenderDebugSection();
    void RenderScreenshotSection();

    void RenderExportSection();
    void RenderImageExportSettings();
    void RenderVideoExportSettings();
    void RenderExportProgress();

    void RenderScenePropertiesSection(Scene* scene);
    void RenderRecentScenesSection(Scene* scene);

    void RenderSimulationGeneralSection();
    void RenderBlackHolesSection(Scene* scene);
    void RenderMeshesSection(Scene* scene);
    void RenderSpheresSection(Scene *scene);

    void RenderAnimationGraphWindow(Scene* scene);

    void HandleFileOperations(Scene* scene, bool doSave, bool doOpen);
    void RenderDebugModeCombo();
    void RenderDebugModeTooltip(int debugMode);
    void LoadScene(Scene* scene, const std::string& path);
    void AddToRecentScenes(const std::string& path);
    void RemoveFromRecentScenes(const std::string& path);

    bool m_showDemoWindow = false;
    bool m_showHelpWindow = false;
    bool m_ShowAnimationGraph = true;
    bool m_initialized = false;
    bool m_configDirty = false;
    float m_saveTimer = 0.0f;
    static constexpr float SAVE_INTERVAL = 5.0f;

    GizmoOperation m_currentGizmoOperation = GizmoOperation::Translate;
    bool m_useSnap = false;
    float m_snapTranslate[3] = {1.0f, 1.0f, 1.0f};
    float m_snapRotate = 15.0f;
    float m_snapScale = 0.1f;

    std::unique_ptr<AnimationGraph> m_AnimationGraph;

    bool m_showExportWindow = false;
    struct {
        int width = 1920;
        int height = 1080;
    } m_imageConfig;

    struct {
        int width = 1920;
        int height = 1080;
        float length = 10.0f;
        int framerate = 60;
        float tickrate = 60.0f;
    } m_videoConfig;
};
