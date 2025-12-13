#include "Application.h"
#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include "LinuxGtkInit.h"
#include "Renderer/PhysicsDebugRenderer.h"
#include "imgui.h"

#ifndef _DEBUG
#define _DEBUG
#endif
#include <PxPhysicsAPI.h>

using namespace physx;

Application& Application::Instance() {
    static Application instance;
    return instance;
}

bool Application::Initialize() {
    if (m_initialized) {
        spdlog::warn("Application already initialized");
        return true;
    }

    spdlog::info("Initializing MoleHole Application");

    TryInitializeGtk();
    m_state.LoadFromFile("config.yaml");

    try {
        InitializeRenderer();
        InitializeSimulation();

        m_ui.Initialize();

        // Initialize intro animation (after UI and renderer are ready)
        m_introAnimation = std::make_unique<IntroAnimation>();
        m_introAnimation->Init();

        if (!m_state.GetLastOpenScene().empty() &&
            std::filesystem::exists(m_state.GetLastOpenScene())) {
            LoadScene(m_state.GetLastOpenScene());
        }

        m_initialized = true;
        m_lastFrameTime = glfwGetTime();

        spdlog::info("Application initialized successfully");
        return true;
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize application: {}", e.what());
        return false;
    }
}

void Application::Run() {
    if (!m_initialized) {
        spdlog::error("Cannot run application - not initialized");
        return;
    }

    m_running = true;
    spdlog::info("Starting main application loop");

    while (!ShouldClose() && m_running) {
        double currentTime = glfwGetTime();
        m_deltaTime = static_cast<float>(currentTime - m_lastFrameTime);
        m_lastFrameTime = currentTime;
        m_totalTime += m_deltaTime;

        m_fpsCounter.Frame();

        HandleWindowEvents();
        Update(m_deltaTime);
        Render();
    }

    spdlog::info("Application loop ended");
}

void Application::Shutdown() {
    if (!m_initialized) {
        return;
    }

    spdlog::info("Shutting down application");

    UpdateWindowState();

    m_state.SaveToFile();

    m_renderer.Shutdown();

    m_initialized = false;
    m_running = false;

    spdlog::info("Application shutdown complete");
}

void Application::Update(float deltaTime) {
    // Update intro animation first
    if (m_introAnimation && m_introAnimation->IsActive()) {
        m_introAnimation->Update(deltaTime);
        
        // Allow skipping intro with Escape or Space
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Space)) {
            m_introAnimation->Skip();
        }
        
        // Don't update simulation/UI during intro
        return;
    }
    
    m_simulation.Update(deltaTime);
    m_ui.Update(deltaTime);
    m_exportRenderer.Update();

    for (const auto& [name, callback] : m_updateCallbacks) {
        try {
            callback(deltaTime);
        } catch (const std::exception& e) {
            spdlog::error("Update callback '{}' threw exception: {}", name, e.what());
        }
    }
}

void Application::Render() {
    m_renderer.BeginFrame();

    // If intro animation is active, render only that
    if (m_introAnimation && m_introAnimation->IsActive()) {
        int width, height;
        glfwGetFramebufferSize(GetWindow(), &width, &height);
        m_introAnimation->Render(width, height);
        m_renderer.EndFrame(false); // Don't clear screen - intro has already rendered everything
        return;
    }

    auto scene = m_simulation.GetScene();

    m_renderer.HandleMousePicking(scene);

    m_ui.RenderDockspace(scene);
    m_ui.RenderMainUI(GetFPS(), scene);
    m_renderer.RenderScene(scene);
    m_ui.RenderSimulationControls(); // Delegates to SimulationWindow namespace

    for (const auto& [name, callback] : m_renderCallbacks) {
        try {
            callback();
        } catch (const std::exception& e) {
            spdlog::error("Render callback '{}' threw exception: {}", name, e.what());
        }
    }

    m_renderer.EndFrame(true);
}

bool Application::ShouldClose() const {
    return m_renderer.GetWindow() ? glfwWindowShouldClose(m_renderer.GetWindow()) : true;
}

GLFWwindow* Application::GetWindow() const {
    return m_renderer.GetWindow();
}

void Application::SetWindowTitle(const std::string& title) const {
    if (auto window = GetWindow()) {
        glfwSetWindowTitle(window, title.c_str());
    }
}

void Application::RequestClose() const {
    if (auto window = GetWindow()) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

void Application::LoadScene(const std::filesystem::path& scenePath) {
    try {
        if (auto scene = m_simulation.GetScene(); scene && std::filesystem::exists(scenePath)) {
            scene->Deserialize(scenePath);
            m_state.SetLastOpenScene(scenePath.string());
            spdlog::info("Loaded scene: {}", scenePath.string());
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to load scene {}: {}", scenePath.string(), e.what());
    }
}

void Application::SaveScene(const std::filesystem::path& scenePath) {
    try {
        if (auto scene = m_simulation.GetScene()) {
            scene->Serialize(scenePath);
            m_state.SetLastOpenScene(scenePath.string());
            spdlog::info("Saved scene: {}", scenePath.string());
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to save scene {}: {}", scenePath.string(), e.what());
    }
}

void Application::NewScene() {
    if (auto scene = m_simulation.GetScene()) {
        scene->blackHoles.clear();
        scene->name = "New Scene";
        scene->currentPath.clear();
        m_state.SetLastOpenScene("");
        spdlog::info("Created new scene");
    }
}

void Application::SaveState() {
    UpdateWindowState();
    m_state.SaveToFile();
}

void Application::RegisterUpdateCallback(const std::string& name, UpdateCallback callback) {
    m_updateCallbacks[name] = std::move(callback);
}

void Application::RegisterRenderCallback(const std::string& name, RenderCallback callback) {
    m_renderCallbacks[name] = std::move(callback);
}

void Application::UnregisterUpdateCallback(const std::string& name) {
    m_updateCallbacks.erase(name);
}

void Application::UnregisterRenderCallback(const std::string& name) {
    m_renderCallbacks.erase(name);
}

void Application::InitializeRenderer() {
    m_renderer.Init();

    if (auto window = m_renderer.GetWindow()) {
        glfwSetWindowUserPointer(window, this);
        glfwSetWindowSizeCallback(window, WindowSizeCallback);
        glfwSetWindowPosCallback(window, WindowPosCallback);
        glfwSetWindowMaximizeCallback(window, WindowMaximizeCallback);
        glfwSetWindowCloseCallback(window, WindowCloseCallback);

        glfwSetWindowSize(window, m_state.window.width, m_state.window.height);
        if (m_state.window.posX >= 0 && m_state.window.posY >= 0) {
            glfwSetWindowPos(window, m_state.window.posX, m_state.window.posY);
        }
        if (m_state.window.maximized) {
            glfwMaximizeWindow(window);
        }

        glfwSetWindowTitle(window, "MoleHole");
    }

    glfwSwapInterval(m_state.window.vsync ? 1 : 0);
}

void Application::InitializeSimulation() {
    m_simulation.SetAnimationGraph(m_ui.GetAnimationGraph());
    m_simulation.GetPhysics()->SetRenderer(&m_renderer);

    auto* physics = m_simulation.GetPhysics();
    if (physics) {
        physics->SetVisualizationScale(m_state.rendering.physicsDebugScale);

        uint32_t flags = m_state.rendering.physicsDebugFlags;
        physics->SetVisualizationParameter(PxVisualizationParameter::eWORLD_AXES, (flags & (1 << 0)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eBODY_AXES, (flags & (1 << 1)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eBODY_MASS_AXES, (flags & (1 << 2)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eBODY_LIN_VELOCITY, (flags & (1 << 3)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eBODY_ANG_VELOCITY, (flags & (1 << 4)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCONTACT_POINT, (flags & (1 << 5)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCONTACT_NORMAL, (flags & (1 << 6)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCONTACT_ERROR, (flags & (1 << 7)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCONTACT_FORCE, (flags & (1 << 8)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eACTOR_AXES, (flags & (1 << 9)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_AABBS, (flags & (1 << 10)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_SHAPES, (flags & (1 << 11)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_AXES, (flags & (1 << 12)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_COMPOUNDS, (flags & (1 << 13)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_FNORMALS, (flags & (1 << 14)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_EDGES, (flags & (1 << 15)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_STATIC, (flags & (1 << 16)) ? 1.0f : 0.0f);
        physics->SetVisualizationParameter(PxVisualizationParameter::eCOLLISION_DYNAMIC, (flags & (1 << 17)) ? 1.0f : 0.0f);
    }

    auto* physicsDebugRenderer = m_renderer.GetPhysicsDebugRenderer();
    if (physicsDebugRenderer) {
        physicsDebugRenderer->SetEnabled(m_state.rendering.physicsDebugEnabled);
        physicsDebugRenderer->SetDepthTestEnabled(m_state.rendering.physicsDebugDepthTest);
    }
}

void Application::UpdateWindowState() {
    auto window = GetWindow();
    if (!window) return;

    int width, height;
    glfwGetWindowSize(window, &width, &height);

    int posX, posY;
    glfwGetWindowPos(window, &posX, &posY);

    int maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);

    m_state.UpdateWindowState(width, height, posX, posY, maximized == GLFW_TRUE);
}

void Application::HandleWindowEvents() {
    glfwPollEvents();
}

void Application::WindowSizeCallback(GLFWwindow* window, int width, int height) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->m_state.window.width = width;
        app->m_state.window.height = height;
    }
}

void Application::WindowPosCallback(GLFWwindow* window, int xpos, int ypos) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window))) {
        app->m_state.window.posX = xpos;
        app->m_state.window.posY = ypos;
    }
}

void Application::WindowMaximizeCallback(GLFWwindow* window, int maximized) {
    if (auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window))) {
        app->m_state.window.maximized = (maximized == GLFW_TRUE);
    }
}

void Application::WindowCloseCallback(GLFWwindow* window) {

}
