#include "UI.h"
#include "Application.h"
#include "../UI/IconsFontAwesome6.h"
#include "../UI/TopBar.h"
#include "../UI/SideBar.h"
#include "../UI/SettingsPopUp.h"
#include "../UI/RendererWindow.h"
#include "../UI/CameraWindow.h"
#include "../UI/DebugWindow.h"
#include "../UI/SceneWindow.h"
#include "../UI/SimulationWindow.h"
#include "../UI/AnimationGraphWindow.h"
#include "../UI/GeneralRelativityWindow.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <nfd.h>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <imgui_node_editor.h>
#include <vector>
#include <memory>
#include "../Renderer/ExportRenderer.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

#include "Parameters.h"
#include "Renderer/PhysicsDebugRenderer.h"

#ifndef _DEBUG
#define _DEBUG
#endif
#include <PxPhysicsAPI.h>

using namespace physx;

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

    Style();
    
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
    // --------------------------------------------------------------------------------------------
    //
    // Section: Keyboard Shortcuts
    //
    // --------------------------------------------------------------------------------------------
    //
    // - Maybe add custom shortcuts?
    // 

    // setup
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl;
    static bool prevCtrlS = false, prevCtrlO = false;
    static bool prevF12 = false, prevF11 = false, prevCtrlF11 = false, prevCtrlF12 = false;
    static bool prevSpace = false, prevP = false, prevS = false, prevR = false;

    // define keys
    bool ctrlS = ctrl && ImGui::IsKeyPressed(ImGuiKey_S);
    bool ctrlO = ctrl && ImGui::IsKeyPressed(ImGuiKey_O);
    bool f12 = ImGui::IsKeyPressed(ImGuiKey_F12);
    bool f11 = ImGui::IsKeyPressed(ImGuiKey_F11);
    bool ctrlf12 = ctrl && ImGui::IsKeyPressed(ImGuiKey_F12);
    bool ctrlf11 = ctrl && ImGui::IsKeyPressed(ImGuiKey_F11);
    bool spaceKey = ImGui::IsKeyPressed(ImGuiKey_Space);
    bool pKey = ImGui::IsKeyPressed(ImGuiKey_P);
    bool sKey = ImGui::IsKeyPressed(ImGuiKey_S) && !ctrl;
    bool rKey = ImGui::IsKeyPressed(ImGuiKey_R);
    
    // Screenshot shortcuts
    bool doTakeScreenshotDialog = false, doTakeScreenshotViewportDialog = false, doTakeScreenshot = false, doTakeScreenshotViewport = false;

    // other shortcuts
    bool doSave = false, doOpen = false;
    
    // Simulation control shortcuts
    bool doSimStart = false, doSimPause = false, doSimStop = false, doSimResume = false;
    
    // set variables for pressed keys
    if (ctrlS && !prevCtrlS) doSave = true;
    if (ctrlO && !prevCtrlO) doOpen = true;
    if (f12 && !prevF12 && !ctrlf12) doTakeScreenshotViewportDialog = true;
    if (ctrlf12 && !prevCtrlF12) doTakeScreenshotViewport = true;
    if (f11 && !prevF11 && !ctrlf11) doTakeScreenshotDialog = true;
    if (ctrlf11 && !prevCtrlF11) doTakeScreenshot = true;
    
    // Simulation shortcuts (Space for Start/Resume, P for Pause, S for Stop, R for Resume)
    if (spaceKey && !prevSpace) {
        auto& simulation = Application::GetSimulation();
        if (simulation.IsStopped()) {
            doSimStart = true;
        } else if (simulation.IsPaused()) {
            doSimResume = true;
        } else if (simulation.IsRunning()) {
            doSimPause = true;
        }
    }
    if (pKey && !prevP) doSimPause = true;
    if (sKey && !prevS) doSimStop = true;
    if (rKey && !prevR) doSimResume = true;

    prevCtrlS = ctrlS;
    prevCtrlO = ctrlO;
    prevF12 = f12;
    prevF11 = f11;
    prevCtrlF12 = ctrlf12;
    prevCtrlF11 = ctrlf11;
    prevSpace = spaceKey;
    prevP = pKey;
    prevS = sKey;
    prevR = rKey;

    TopBar::RenderMainMenuBar(this, scene, doSave, doOpen, doTakeScreenshotDialog, doTakeScreenshotViewportDialog, doTakeScreenshot, doTakeScreenshotViewport);
    TopBar::HandleFileOperations(this, scene, doSave, doOpen);
    TopBar::HandleImageShortcuts(scene, doTakeScreenshotViewport, doTakeScreenshot, doTakeScreenshotViewportDialog, doTakeScreenshotDialog);
    
    // Handle simulation control shortcuts
    auto& simulation = Application::GetSimulation();
    if (doSimStart) simulation.Start();
    if (doSimPause) simulation.Pause();
    if (doSimStop) simulation.Stop();
    if (doSimResume) simulation.Start(); // Resume is just calling Start() when paused

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float sidebarWidth = 60.0f; // Match the sidebar width from RenderSidebar()
    
    // Position dockspace to the right of the sidebar
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + sidebarWidth, viewport->Pos.y + ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - sidebarWidth, viewport->Size.y - ImGui::GetFrameHeight()));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGuiWindowFlags dockspace_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                      ImGuiWindowFlags_NoNavFocus;

    ImGui::Begin("##DockSpace", nullptr, dockspace_flags);
    ImGui::DockSpace(ImGui::GetID("DockSpace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void UI::RenderMainUI(float fps, Scene* scene) {
    SideBar::Render(this);

    if (m_showSystemWindow) {
        RendererWindow::Render(this, fps);
    }

    if (m_showCameraWindow) {
        CameraWindow::Render(this, scene);
    }

    if (m_showDebugWindow) {
        DebugWindow::Render(this);
    }

    if (m_showSceneWindow) {
        SceneWindow::Render(this, scene);
    }

    if (m_showSimulationWindow) {
        SimulationWindow::Render(this, scene);
    }

    if (m_showDemoWindow) {
        ImGui::ShowDemoWindow(&m_showDemoWindow);
    }

    if (m_showHelpWindow) {
        RenderHelpWindow();
    }

    if (m_ShowAnimationGraph) {
        AnimationGraphWindow::Render(this, scene);
    }

    if (m_showExportWindow) {
        ShowExportWindow(scene);
    }

    if (m_showSettingsWindow) {
        SettingsPopUp::Render(this, &m_showSettingsWindow);
    }

    if (m_showGeneralRelativityWindow)
    {
        bool* p_open = nullptr;
        GeneralRelativityWindow::Render(p_open);
    }
}


// --------------------------------------------------------------------------------------------
//
// Section: Styling
//
// --------------------------------------------------------------------------------------------
//
// Set custom font.
// Set custom theme.

void UI::Style() {

    ImGuiIO &io = ImGui::GetIO();
    
    // Get all available fonts and load them at the configured size
    float fontSize = Application::Params().Get(Params::UIFontSize, 16.0f);
    std::vector<std::string> availableFonts = GetAvailableFonts();
    
    // Load all available fonts into the atlas
    for (const auto& fontFile : availableFonts) {
        std::string fontPath = "../font/" + fontFile;
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize);
        if (font) {
            m_loadedFonts[fontFile] = font;
            spdlog::info("Loaded font: {} ({}pt)", fontFile, fontSize);
        } else {
            spdlog::warn("Failed to load font: {}", fontFile);
        }
    }
    
    // Get configured font from settings, default to Roboto-Regular.ttf
    const std::string defaultFont = "Roboto-Regular.ttf";
    std::string fontName = Application::Params().Get(Params::UIMainFont, defaultFont);

    // Set the configured font as main font
    if (m_loadedFonts.count(fontName) > 0) {
        m_mainFont = m_loadedFonts[fontName];
        io.FontDefault = m_mainFont;
        spdlog::info("Set main font to: {}", fontName);
    } else if (!m_loadedFonts.empty()) {
        // Fallback to first loaded font
        m_mainFont = m_loadedFonts.begin()->second;
        io.FontDefault = m_mainFont;
        spdlog::warn("Font '{}' not found, using fallback: {}", fontName, m_loadedFonts.begin()->first);
    } else {
        // Last resort: ImGui default font
        m_mainFont = io.Fonts->AddFontDefault();
        io.FontDefault = m_mainFont;
        spdlog::warn("No fonts loaded, using ImGui default font");
    }
    
    // Load Font Awesome icon font
    // Font Awesome 6 Free is licensed under SIL OFL 1.1 (https://scripts.sil.org/OFL)
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
    ImFontConfig icons_config;
    //icons_config.MergeMode = false;
    //icons_config.PixelSnapH = true;
    //icons_config.GlyphMinAdvanceX = 24.0f;
    
    m_iconFont = io.Fonts->AddFontFromFileTTF("../font/fa-solid-900.ttf", 24.0f, &icons_config, icons_ranges);
    if (m_iconFont) {
        spdlog::info("Font Awesome icon font loaded successfully");
    } else {
        spdlog::warn("Failed to load Font Awesome icon font");
    }
    
    
    // Rest style by AaronBeardless from ImThemes
	ImGuiStyle& style = ImGui::GetStyle();
	
	style.Alpha = 1.0f;
	style.DisabledAlpha = 0.5f;
	style.WindowPadding = ImVec2(8.0f, 8.0f);
	style.WindowRounding = 0.0f;
	style.WindowBorderSize = 0.0f;
	style.WindowMinSize = ImVec2(32.0f, 32.0f);
	style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	style.WindowMenuButtonPosition = ImGuiDir_Right;
	style.ChildRounding = 0.0f;
	style.ChildBorderSize = 1.0f;
	style.PopupRounding = 0.0f;
	style.PopupBorderSize = 1.0f;
	style.FramePadding = ImVec2(20.0f, 8.1f);
	style.FrameRounding = 2.0f;
	style.FrameBorderSize = 0.0f;
	style.ItemSpacing = ImVec2(3.0f, 3.0f);
	style.ItemInnerSpacing = ImVec2(3.0f, 8.0f);
	style.CellPadding = ImVec2(6.0f, 14.1f);
	style.IndentSpacing = 0.0f;
	style.ColumnsMinSpacing = 10.0f;
	style.ScrollbarSize = 10.0f;
	style.ScrollbarRounding = 2.0f;
	style.GrabMinSize = 12.1f;
	style.GrabRounding = 1.0f;
	style.TabRounding = 0.0f;
	style.TabBorderSize = 1.0f;
	style.TabBarBorderSize = 1.0f;
	style.SeparatorTextBorderSize = 1.0f;
	style.ColorButtonPosition = ImGuiDir_Right;
	style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
	
	style.Colors[ImGuiCol_Text] = ImVec4(0.98039216f, 0.98039216f, 0.98039216f, 1.0f);
	style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.49803922f, 0.49803922f, 0.49803922f, 1.0f);
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.09411765f, 0.09411765f, 0.09411765f, 1.0f);
	style.Colors[ImGuiCol_ChildBg] = ImVec4(0.15686275f, 0.15686275f, 0.15686275f, 1.0f);
	style.Colors[ImGuiCol_PopupBg] = ImVec4(0.09411765f, 0.09411765f, 0.09411765f, 1.0f);
	style.Colors[ImGuiCol_Border] = ImVec4(0.2f, 0.2f, 0.2f, 1.0f);
	style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.09803922f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.15686275f);
	style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.047058824f);
	style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.10980392f);
	style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(1.0f, 1.0f, 1.0f, 0.39215687f);
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.47058824f);
	style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.0f, 0.0f, 0.0f, 0.09803922f);
	// Darker orange accent color (from RGB 229,161,80 to 180,100,40)
	style.Colors[ImGuiCol_CheckMark] = ImVec4(180.0f/255,100.0f/255,40.0f/255,255.0f/255);
	style.Colors[ImGuiCol_SliderGrab] = ImVec4(180.0f/255,100.0f/255,40.0f/255,255.0f/255);
	style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.3137255f);
	style.Colors[ImGuiCol_Button] = ImVec4(180.0f/255,100.0f/255,40.0f/255,255.0f/255);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(200.0f/255,120.0f/255,50.0f/255,255.0f/255);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(160.0f/255,90.0f/255,35.0f/255,255.0f/255);
	style.Colors[ImGuiCol_Header] = ImVec4(180.0f/255,100.0f/255,40.0f/255,255.0f/255);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(200.0f/255,120.0f/255,50.0f/255,255.0f/255);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(160.0f/255,90.0f/255,35.0f/255,255.0f/255);
	style.Colors[ImGuiCol_Separator] = ImVec4(180.0f/255,100.0f/255,40.0f/255,255.0f/255);
	style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(200.0f/255,120.0f/255,50.0f/255,255.0f/255);
	style.Colors[ImGuiCol_SeparatorActive] = ImVec4(160.0f/255,90.0f/255,35.0f/255,255.0f/255);
	style.Colors[ImGuiCol_ResizeGrip] = ImVec4(1.0f, 1.0f, 1.0f, 0.15686275f);
	style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.0f, 1.0f, 1.0f, 0.23529412f);
	style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(1.0f, 1.0f, 1.0f, 0.23529412f);
	// Tab styling for JetBrains IDE-like appearance
	style.Colors[ImGuiCol_Tab] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.0f);
	style.Colors[ImGuiCol_TabActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
	style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
	style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_DockingPreview] = ImVec4(180.0f/255,100.0f/255,40.0f/255, 0.7f);
	style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.09411765f, 0.09411765f, 0.09411765f, 1.0f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.18f, 0.18f, 1.0f);
	style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.12f, 0.12f, 0.12f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(1.0f, 1.0f, 1.0f, 0.3529412f);
	style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_PlotHistogram] = ImVec4(1.0f, 1.0f, 1.0f, 0.3529412f);
	style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.15686275f, 0.15686275f, 0.15686275f, 1.0f);
	style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(1.0f, 1.0f, 1.0f, 0.3137255f);
	style.Colors[ImGuiCol_TableBorderLight] = ImVec4(1.0f, 1.0f, 1.0f, 0.19607843f);
	style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
	style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.019607844f);
	style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.16862746f, 0.23137255f, 0.5372549f, 1.0f);
	style.Colors[ImGuiCol_NavHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
	style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.5647059f);

    /*
    _style.FrameRounding = 5.0f;
    _style.Colors[ImGuiCol_Button] = ImVec4(229.0f/255,161.0f/255,80.0f/255,255.0f/255);
    _style.Colors[ImGuiCol_FrameBg] = ImVec4(229.0f/255,161.0f/255,80.0f/255,255.0f/255);
    _style.Colors[ImGuiCol_Separator] = ImVec4(229.0f/255,161.0f/255,80.0f/255,255.0f/255);
    _style.Colors[ImGuiCol_Text] = ImVec4(27.0f/255,52.0f/255,57.0f/255,255.0f/255);
    _style.Colors[ImGuiCol_Header] = ImVec4(229.0f/255,161.0f/255,80.0f/255,255.0f/255);
    


    // Hovers
    _style.Colors[ImGuiCol_HeaderHovered] = ImVec4(249.0f/255,186.0f/255,106.0f/255,255.0f/255);
    _style.Colors[ImGuiCol_ButtonHovered] = ImVec4(249.0f/255,186.0f/255,106.0f/255,255.0f/255);

    // Actives
    _style.Colors[ImGuiCol_HeaderActive] = ImVec4(249.0f/255,186.0f/255,106.0f/255,255.0f/255);
    _style.Colors[ImGuiCol_ButtonActive] = ImVec4(249.0f/255,186.0f/255,106.0f/255,255.0f/255);
    _style.Colors[ImGuiCol_TabActive] = ImVec4()*/
    
}

// --------------------------------------------------------------------------------------------
//
// Section: Delegated Window Functions
//
// --------------------------------------------------------------------------------------------

void UI::RenderSimulationControls() {
    SimulationWindow::RenderSimulationControls(this);
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

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Troubleshooting")) {
            ImGui::Text("Common Issues:");
            ImGui::BulletText("Performance issues? Reduce resolution or ray steps");
            ImGui::BulletText("Black screen? Check camera position and orientation");
            ImGui::BulletText("Weird artifacts? Try adjusting max ray distance");
            ImGui::BulletText("Nothing visible? Ensure black holes are in scene");

            ImGui::Separator();
            ImGui::Text("Tips:");
            ImGui::BulletText("Use debug modes to understand what's happening");
            ImGui::BulletText("Start with default settings and adjust gradually");
            ImGui::BulletText("Save your working scenes frequently");
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

// --------------------------------------------------------------------------------------------
//
// Section: Main Menu Top Bar (Moved to TopBar namespace in UI/TopBar.h)
//
// --------------------------------------------------------------------------------------------


void UI::ShowExportWindow(Scene* scene) {
    ImGui::SetNextWindowSize(ImVec2(600, 700), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Export", &m_showExportWindow)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("ExportTabs")) {
        if (ImGui::BeginTabItem("Image Export")) {
            RenderImageExportSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Video Export")) {
            RenderVideoExportSettings();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();
    RenderExportProgress();

    ImGui::End();
}

void UI::RenderImageExportSettings() {
    ImGui::TextWrapped("Export a high-resolution image of the current scene.");
    ImGui::Spacing();

    ImGui::Text("Resolution Settings:");
    ImGui::DragInt("Width", &m_imageConfig.width, 1.0f, 256, 7680);
    ImGui::DragInt("Height", &m_imageConfig.height, 1.0f, 256, 4320);

    if (ImGui::Button("Set 1080p (1920x1080)")) {
        m_imageConfig.width = 1920;
        m_imageConfig.height = 1080;
    }
    ImGui::SameLine();
    if (ImGui::Button("Set 4K (3840x2160)")) {
        m_imageConfig.width = 3840;
        m_imageConfig.height = 2160;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Preview:");
    ImGui::Text("Resolution: %dx%d", m_imageConfig.width, m_imageConfig.height);
    float aspectRatio = (float)m_imageConfig.width / (float)m_imageConfig.height;
    ImGui::Text("Aspect Ratio: %.2f:1", aspectRatio);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Export Image (PNG)...", ImVec2(-1, 40))) {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filterItems[] = {
            { "PNG Image", "png" }
        };

        const std::string fallbackPath = ".";
        std::string defaultPath = Application::Params().Get(Params::UIDefaultExportPath, fallbackPath);
        nfdresult_t result = NFD_SaveDialog(&outPath, filterItems, 1, defaultPath.c_str(), "export.png");

        if (result == NFD_OKAY && outPath) {
            auto& app = Application::Instance();
            auto& exportRenderer = app.GetExportRenderer();
            
            ExportRenderer::ImageConfig config;
            config.width = m_imageConfig.width;
            config.height = m_imageConfig.height;

            exportRenderer.StartImageExport(config, std::string(outPath), app.GetSimulation().GetScene());

            free(outPath);
        }
    }

    ImGui::TextDisabled("Click to choose output location and start export");
}

void UI::RenderVideoExportSettings() {
    ImGui::TextWrapped("Export a video of the simulation with configurable parameters.");
    ImGui::Spacing();

    ImGui::Text("Resolution Settings:");
    ImGui::DragInt("Width", &m_videoConfig.width, 1.0f, 256, 7680);
    ImGui::DragInt("Height", &m_videoConfig.height, 1.0f, 256, 4320);

    if (ImGui::Button("Set 1080p (1920x1080)")) {
        m_videoConfig.width = 1920;
        m_videoConfig.height = 1080;
    }
    ImGui::SameLine();
    if (ImGui::Button("Set 4K (3840x2160)")) {
        m_videoConfig.width = 3840;
        m_videoConfig.height = 2160;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Video Settings:");
    ImGui::DragFloat("Length (seconds)", &m_videoConfig.length, 0.1f, 0.1f, 300.0f);
    ImGui::DragInt("Framerate (fps)", &m_videoConfig.framerate, 1.0f, 1, 240);
    ImGui::DragFloat("Tickrate (tps)", &m_videoConfig.tickrate, 1.0f, 1.0f, 240.0f);

    ImGui::TextDisabled("Tickrate controls simulation speed");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Ray Marching Quality:");
    ImGui::Checkbox("Use Custom Ray Settings", &m_videoConfig.useCustomRaySettings);
    
    if (m_videoConfig.useCustomRaySettings) {
        ImGui::Indent();
        ImGui::DragFloat("Ray Step Size", &m_videoConfig.customRayStepSize, 
                        0.001f, 0.001f, 1.0f, "%.4f");
        ImGui::DragInt("Max Ray Steps", &m_videoConfig.customMaxRaySteps, 
                      10, 100, 5000);
        ImGui::TextDisabled("Lower step size = better quality, slower export");
        ImGui::TextDisabled("Higher max steps = more detail, slower export");
        ImGui::Unindent();
    } else {
        ImGui::TextDisabled("Using current application ray marching settings");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("Preview:");
    ImGui::Text("Resolution: %dx%d", m_videoConfig.width, m_videoConfig.height);
    ImGui::Text("Duration: %.1f seconds", m_videoConfig.length);
    ImGui::Text("Total Frames: %d", static_cast<int>(m_videoConfig.length * m_videoConfig.framerate));
    float aspectRatio = (float)m_videoConfig.width / (float)m_videoConfig.height;
    ImGui::Text("Aspect Ratio: %.2f:1", aspectRatio);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Export Video (MP4)...", ImVec2(-1, 40))) {
        nfdchar_t* outPath = nullptr;
        nfdfilteritem_t filterItems[] = {
            { "MP4 Video", "mp4" }
        };

        const std::string fallbackPath = ".";
        std::string defaultPath = Application::Params().Get(Params::UIDefaultExportPath, fallbackPath);
        nfdresult_t result = NFD_SaveDialog(&outPath, filterItems, 1, defaultPath.c_str(), "export.mp4");

        if (result == NFD_OKAY && outPath) {
            auto& app = Application::Instance();
            auto& exportRenderer = app.GetExportRenderer();
            
            ExportRenderer::VideoConfig config;
            config.width = m_videoConfig.width;
            config.height = m_videoConfig.height;
            config.length = m_videoConfig.length;
            config.framerate = m_videoConfig.framerate;
            config.tickrate = m_videoConfig.tickrate;
            config.useCustomRaySettings = m_videoConfig.useCustomRaySettings;
            config.customRayStepSize = m_videoConfig.customRayStepSize;
            config.customMaxRaySteps = m_videoConfig.customMaxRaySteps;

            exportRenderer.StartVideoExport(config, std::string(outPath), app.GetSimulation().GetScene());

            free(outPath);
        }
    }

    ImGui::TextDisabled("Click to choose output location and start export");
}

void UI::RenderExportProgress() {
    auto& exportRenderer = Application::Instance().GetExportRenderer();
    
    if (exportRenderer.IsExporting()) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Export in Progress");
        ImGui::ProgressBar(exportRenderer.GetProgress());
        ImGui::Text("Status: %s", exportRenderer.GetCurrentTask().c_str());
        ImGui::TextDisabled("Do not close the application while exporting");
    } else {
        ImGui::TextDisabled("No export in progress");
    }
}

void UI::ReloadFonts() {
    // Switch to the selected font without rebuilding the atlas
    const std::string defaultFont = "Roboto-Regular.ttf";
    std::string fontName = Application::Params().Get(Params::UIMainFont, defaultFont);

    // Check if the font is already loaded
    if (m_loadedFonts.count(fontName) > 0) {
        ImGuiIO& io = ImGui::GetIO();
        m_mainFont = m_loadedFonts[fontName];
        io.FontDefault = m_mainFont;
        spdlog::info("Switched to font: {}", fontName);
    } else {
        spdlog::warn("Font '{}' not found in loaded fonts. Available fonts must be added via 'Add Custom Font'.", fontName);
    }
}

std::vector<std::string> UI::GetAvailableFonts() const {
    std::vector<std::string> fonts;
    
    try {
        std::filesystem::path fontDir = "../font";
        if (std::filesystem::exists(fontDir) && std::filesystem::is_directory(fontDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(fontDir)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().filename().string();
                    std::string ext = entry.path().extension().string();
                    
                    // Convert extension to lowercase for comparison
                    std::transform(ext.begin(), ext.end(), ext.begin(), 
                                 [](unsigned char c){ return std::tolower(c); });
                    
                    // Only include .ttf files, exclude Font Awesome
                    if (ext == ".ttf" && filename != "fa-solid-900.ttf") {
                        fonts.push_back(filename);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to scan font directory: {}", e.what());
    }
    
    // Sort alphabetically
    std::sort(fonts.begin(), fonts.end());
    
    return fonts;
}
