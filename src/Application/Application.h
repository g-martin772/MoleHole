#pragma once

#include "AppState.h"
#include "UI.h"
#include "Renderer/Renderer.h"
#include "Simulation/Simulation.h"
#include "FpsCounter.h"
#include <functional>

struct GLFWwindow;

class Application {
public:
    using UpdateCallback = std::function<void(float deltaTime)>;
    using RenderCallback = std::function<void()>;
    using EventCallback = std::function<void()>;

    static Application& Instance();
    static AppState& State() { return Instance().GetState(); }
    static Renderer& GetRenderer() { return Instance().m_renderer; }
    static Simulation& GetSimulation() { return Instance().m_simulation; }

    bool Initialize();
    void Run();
    void Shutdown();

    void Update(float deltaTime);
    void Render();
    bool ShouldClose() const;

    AppState& GetState() { return m_state; }
    const AppState& GetState() const { return m_state; }

    GLFWwindow* GetWindow() const;
    void SetWindowTitle(const std::string& title);
    void RequestClose();

    void LoadScene(const std::filesystem::path& scenePath);
    void SaveScene(const std::filesystem::path& scenePath);
    void NewScene();

    void SaveState();

    void RegisterUpdateCallback(const std::string& name, UpdateCallback callback);
    void RegisterRenderCallback(const std::string& name, RenderCallback callback);
    void UnregisterUpdateCallback(const std::string& name);
    void UnregisterRenderCallback(const std::string& name);

    float GetDeltaTime() const { return m_deltaTime; }
    float GetTotalTime() const { return m_totalTime; }

    float GetFPS() const { return m_fpsCounter.GetFps(); }

    UI& GetUI() { return m_ui; };

private:
    Application() = default;
    ~Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    AppState m_state;
    UI m_ui;
    Renderer m_renderer;
    Simulation m_simulation;
    FpsCounter m_fpsCounter;

    bool m_initialized = false;
    bool m_running = false;
    float m_deltaTime = 0.0f;
    float m_totalTime = 0.0f;
    double m_lastFrameTime = 0.0;

    std::unordered_map<std::string, UpdateCallback> m_updateCallbacks;
    std::unordered_map<std::string, RenderCallback> m_renderCallbacks;

    void InitializeRenderer();
    void InitializeSimulation();
    void UpdateWindowState();
    void HandleWindowEvents();

    static void WindowSizeCallback(GLFWwindow* window, int width, int height);
    static void WindowPosCallback(GLFWwindow* window, int xpos, int ypos);
    static void WindowMaximizeCallback(GLFWwindow* window, int maximized);
    static void WindowCloseCallback(GLFWwindow* window);
};
