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
    void SetCurrentGizmoOperation(GizmoOperation op) { m_currentGizmoOperation = op; }
    bool IsUsingSnap() const { return m_useSnap; }
    void SetUsingSnap(bool snap) { m_useSnap = snap; }
    const float* GetSnapTranslate() const { return m_snapTranslate; }
    float* GetSnapTranslate() { return m_snapTranslate; }
    float GetSnapRotate() const { return m_snapRotate; }
    float* GetSnapRotatePtr() { return &m_snapRotate; }
    float GetSnapScale() const { return m_snapScale; }
    float* GetSnapScalePtr() { return &m_snapScale; }

    void TakeScreenshot();
    void TakeViewportScreenshot();
    void TakeScreenshotWithDialog();
    void TakeViewportScreenshotWithDialog();

    void ShowExportWindow(Scene* scene);

    void MarkConfigDirty() { m_configDirty = true; }
    
    void ReloadFonts();
    std::vector<std::string> GetAvailableFonts() const;
    
    // Screenshot state
    bool IsTakingScreenshot() const { return m_takingScreenshot; }
    void SetTakingScreenshot(bool taking) { m_takingScreenshot = taking; }

    // Sidebar accessors
    float* GetSidebarHoverAnim() { return m_sidebarHoverAnim; }
    int GetHoveredSidebarItem() const { return m_hoveredSidebarItem; }
    void SetHoveredSidebarItem(int item) { m_hoveredSidebarItem = item; }
    struct ImFont* GetIconFont() const { return m_iconFont; }
    bool* GetShowAnimationGraphPtr() { return &m_ShowAnimationGraph; }
    bool* GetShowSystemWindowPtr() { return &m_showSystemWindow; }
    bool* GetShowSimulationWindowPtr() { return &m_showSimulationWindow; }
    bool* GetShowSceneWindowPtr() { return &m_showSceneWindow; }
    bool* GetShowSettingsWindowPtr() { return &m_showSettingsWindow; }
    bool* GetShowCameraWindowPtr() { return &m_showCameraWindow; }
    bool* GetShowDebugWindowPtr() { return &m_showDebugWindow; }
    bool* GetShowDemoWindowPtr() { return &m_showDemoWindow; }
    bool* GetShowHelpWindowPtr() { return &m_showHelpWindow; }
    bool* GetShowExportWindowPtr() { return &m_showExportWindow; }

private:
    // Window rendering (kept for special cases like HelpWindow and export)
    void RenderHelpWindow();
    void RenderImageExportSettings();
    void RenderVideoExportSettings();
    void RenderExportProgress();

    void Style();


    bool m_showDemoWindow = false;
    bool m_showHelpWindow = false;
    bool m_ShowAnimationGraph = true;
    bool m_showSystemWindow = true;
    bool m_showSceneWindow = true;
    bool m_showSimulationWindow = true;
    bool m_showSettingsWindow = false;
    bool m_showCameraWindow = true;
    bool m_showDebugWindow = true;
    bool m_initialized = false;
    bool m_configDirty = false;
    float m_saveTimer = 0.0f;
    static constexpr float SAVE_INTERVAL = 5.0f;
    bool m_takingScreenshot = false;

    GizmoOperation m_currentGizmoOperation = GizmoOperation::Translate;
    bool m_useSnap = false;
    float m_snapTranslate[3] = {1.0f, 1.0f, 1.0f};
    float m_snapRotate = 15.0f;
    float m_snapScale = 0.1f;

    std::unique_ptr<AnimationGraph> m_AnimationGraph;

    struct ImFont* m_iconFont = nullptr;
    struct ImFont* m_mainFont = nullptr;
    std::unordered_map<std::string, struct ImFont*> m_loadedFonts; // Map of font name to ImFont*

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
        bool useCustomRaySettings = false;
        float customRayStepSize = 0.01f;
        int customMaxRaySteps = 1000;
    } m_videoConfig;

    // Sidebar animation
    float m_sidebarHoverAnim[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    int m_hoveredSidebarItem = -1;
};
