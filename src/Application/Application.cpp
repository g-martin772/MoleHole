#include "Application.h"
#include "../Platform/Linux/LinuxGtkInit.h"
#include "Parameters.h"

Application& Application::Instance() {
    static Application instance;
    return instance;
}

bool Application::Initialize(int argc, char* argv[]) {
    m_args.Parse(argc, argv);
    return Initialize();
}

bool Application::Initialize() {
    if (m_initialized) {
        spdlog::warn("Application already initialized");
        return true;
    }

    spdlog::info("Initializing MoleHole Application");

#ifdef __linux__
    TryInitializeGtk();
#endif

    AppState::Params().LoadDefinitionsFromYaml("../assets/config/parameters.yaml");
    m_state.LoadState("config.yaml");

    try {
        bool headlessMode = m_args.IsHeadless();
        spdlog::debug("Headless mode: {}", headlessMode);

        InitializeRenderer();
        InitializeSimulation();

        if (!headlessMode) {
            m_ui.Initialize();

/*
            if (m_args.ShouldShowIntro()) {
                m_introAnimation = std::make_unique<IntroAnimation>();
                //m_introAnimation->Init();
                spdlog::info("Intro animation enabled");
            } else {
                spdlog::info("Intro animation disabled via command line");
            }
*/
        } else {
            spdlog::info("Running in headless mode - skipping UI and intro");
        }

        const std::string lastScene = Params().Get(Params::AppLastOpenScene, std::string());

        if (m_args.IsHeadless()) {
            auto sceneArg = m_args.GetValue("scene");
            if (sceneArg.has_value() && std::filesystem::exists(sceneArg.value())) {
                LoadScene(sceneArg.value());
                spdlog::info("Loaded scene from command line: {}", sceneArg.value());
            } else if (!lastScene.empty() && std::filesystem::exists(lastScene)) {
                LoadScene(lastScene);
            } else if (sceneArg.has_value()) {
                spdlog::error("Scene file not found: {}", sceneArg.value());
                return false;
            }
        } else if (!lastScene.empty() && std::filesystem::exists(lastScene)) {
            LoadScene(lastScene);
        }

        m_initialized = true;
        if (auto* window = m_renderer.GetWindowAbstraction()) {
            m_lastFrameTime = window->GetTime();
        }

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
        auto* window = m_renderer.GetWindowAbstraction();
        double currentTime = window ? window->GetTime() : 0.0;
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

void Application::RunHeadless() {
    if (!m_initialized) {
        spdlog::error("Cannot run application - not initialized");
        return;
    }

    spdlog::info("Starting headless mode");

    auto exportImagePath = m_args.GetValue("export-image");
    auto exportVideoPath = m_args.GetValue("export-video");

    if (!exportImagePath.has_value() && !exportVideoPath.has_value()) {
        spdlog::error("Headless mode requires --export-image or --export-video argument");
        return;
    }

    auto scene = m_simulation.GetScene();
    if (!scene) {
        spdlog::error("No scene loaded for export");
        return;
    }

    int width = m_args.GetValueInt("width", 1920);
    int height = m_args.GetValueInt("height", 1080);

    if (exportImagePath.has_value()) {
        spdlog::info("Starting image export: {}x{} -> {}", width, height, exportImagePath.value());

        // ExportRenderer::ImageConfig config;
        // config.width = width;
        // config.height = height;

        // m_exportRenderer.StartImageExport(config, exportImagePath.value(), scene);
    } else if (exportVideoPath.has_value()) {
        float videoLength = m_args.GetValueFloat("video-length", 10.0f);
        int videoFps = m_args.GetValueInt("video-fps", 60);

        spdlog::info("Starting video export: {}x{} {}s @{}fps -> {}",
                    width, height, videoLength, videoFps, exportVideoPath.value());

        // ExportRenderer::VideoConfig config;
        // config.width = width;
        // config.height = height;
        // config.length = videoLength;
        // config.framerate = videoFps;
        // config.tickrate = videoFps;

        if (m_args.HasFlag("use-custom-ray-settings")) {
            // config.useCustomRaySettings = true;
            // config.customRayStepSize = m_args.GetValueFloat("ray-step-size", 0.01f);
            // config.customMaxRaySteps = m_args.GetValueInt("max-ray-steps", 1000);
        }

        // m_exportRenderer.StartVideoExport(config, exportVideoPath.value(), scene);
    }

    // Main export loop
    m_running = true;
    {
        auto* window = m_renderer.GetWindowAbstraction();
        m_lastFrameTime = window ? window->GetTime() : 0.0;
    }

    while (m_running /*&& m_exportRenderer.IsExporting()*/) {
        auto* window = m_renderer.GetWindowAbstraction();
        double currentTime = window ? window->GetTime() : 0.0;
        m_deltaTime = static_cast<float>(currentTime - m_lastFrameTime);
        m_lastFrameTime = currentTime;

        if (window) {
            window->PollEvents();
        }

        // m_exportRenderer.Update();

        static float lastProgressLog = 0.0f;
        // float currentProgress = m_exportRenderer.GetProgress();
        /*if (currentProgress - lastProgressLog >= 0.05f) { // Log every 5%
            spdlog::info("Export progress: {:.1f}% - {}",
                        currentProgress * 100.0f,
                        m_exportRenderer.GetCurrentTask());
            lastProgressLog = currentProgress;
        }*/

        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    /*if (m_exportRenderer.IsExporting()) {
        spdlog::warn("Export loop ended but export is still in progress");
    } else {
        spdlog::info("Export completed successfully");
    }*/

    if (m_args.ShouldExitOnComplete()) {
        spdlog::info("Exiting as requested by --exit-on-complete flag");
    }

    spdlog::info("Headless mode finished");
}

void Application::Shutdown() {
    if (!m_initialized) {
        return;
    }

    spdlog::info("Shutting down application");

    UpdateWindowState();

    m_state.SaveState();

    m_renderer.Shutdown();

    m_initialized = false;
    m_running = false;

    spdlog::info("Application shutdown complete");
}

void Application::Update(float deltaTime) {
    // Update intro animation first
    /*if (m_introAnimation && m_introAnimation->IsActive()) {
        m_introAnimation->Update(deltaTime);

        // Allow skipping intro with Escape or Space
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Space)) {
            m_introAnimation->Skip();
        }

        // Don't update simulation/UI during intro
        return;
    }*/

    m_simulation.Update(deltaTime);
    m_ui.Update(deltaTime);
    // m_exportRenderer.Update();

    for (const auto& [name, callback] : m_updateCallbacks) {
        try {
            callback(deltaTime);
        } catch (const std::exception& e) {
            spdlog::error("Update callback '{}' threw exception: {}", name, e.what());
        }
    }

    if (!m_args.IsHeadless()) {
        auto scene = m_simulation.GetScene();
        std::string sceneName = "New Scene";

        if (scene) {
            if (!scene->name.empty()) {
                sceneName = scene->name;
            } else if (!scene->currentPath.empty()) {
                sceneName = scene->currentPath.filename().string();
            }
        }

        std::string title = "MoleHole - " + sceneName + " - " + std::to_string(static_cast<int>(GetFPS())) + " FPS";
        SetWindowTitle(title);
    }
}

void Application::Render() {
    m_renderer.BeginFrame();

    // If intro animation is active, render only that
    /*if (Params().Get(Params::AppIntroAnimationEnabled, true) && m_introAnimation && m_introAnimation->IsActive()) {
        int width, height;
        if (auto* window = m_renderer.GetWindowAbstraction()) {
            window->GetFramebufferSize(width, height);
        }
        //m_introAnimation->Render(width, height);
        m_renderer.EndFrame(false);
        return;
    }*/

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
    auto* window = m_renderer.GetWindowAbstraction();
    return window ? window->ShouldClose() : true;
}

Window* Application::GetWindow() const {
    return m_renderer.GetWindowAbstraction();
}

void Application::SetWindowTitle(const std::string& title) const {
    if (auto* window = m_renderer.GetWindowAbstraction()) {
        window->SetTitle(title);
    }
}

void Application::RequestClose() const {
    if (auto* window = m_renderer.GetWindowAbstraction()) {
        window->SetShouldClose(true);
    }
}

void Application::LoadScene(const std::filesystem::path& scenePath) {
    m_simulation.LoadScene(scenePath);
}

void Application::SaveScene(const std::filesystem::path& scenePath) {
    m_simulation.SaveScene(scenePath);
}

void Application::NewScene() {
    m_simulation.NewScene();
}

void Application::SaveState() {
    UpdateWindowState();
    m_state.SaveState();
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
    bool headless = m_args.IsHeadless();
    m_renderer.Init(headless);

    if (auto* window = m_renderer.GetWindowAbstraction()) {
        if (!headless) {
            window->SetUserPointer(this);

            window->SetResizeCallback([](uint32_t width, uint32_t height) {
                auto* win = Instance().m_renderer.GetWindowAbstraction();
                auto* app = static_cast<Application*>(win->GetUserPointer());
                if (app) {
                    app->Params().Set(Params::WindowWidth, static_cast<int>(width));
                    app->Params().Set(Params::WindowHeight, static_cast<int>(height));
                }
            });

            window->SetPositionCallback([](int x, int y) {
                auto* win = Instance().m_renderer.GetWindowAbstraction();
                auto* app = static_cast<Application*>(win->GetUserPointer());
                if (app) {
                    app->Params().Set(Params::WindowPosX, x);
                    app->Params().Set(Params::WindowPosY, y);
                }
            });

            window->SetMaximizeCallback([](bool maximized) {
                Params().Set(Params::WindowMaximized, maximized);
            });

            window->SetSize(
                Params().Get(Params::WindowWidth, 1280),
                Params().Get(Params::WindowHeight, 720)
            );

            int posX = Params().Get(Params::WindowPosX, -1);
            int posY = Params().Get(Params::WindowPosY, -1);
            if (posX >= 0 && posY >= 0) {
                window->SetPosition(posX, posY);
            }

            if (Params().Get(Params::WindowMaximized, false)) {
                window->Maximize();
            }

            window->SetTitle("MoleHole");
        }
    }

    if (auto* window = m_renderer.GetWindowAbstraction()) {
        window->SetVSync(Params().Get(Params::WindowVSync, true));
    }
}

void Application::InitializeSimulation() {
    m_simulation.Initialize();
    m_simulation.SetAnimationGraph(m_ui.GetAnimationGraph());
    m_simulation.GetPhysics()->SetRenderer(&m_renderer);

    auto* physics = m_simulation.GetPhysics();
    if (physics) {
        uint32_t flags = static_cast<uint32_t>(Params().Get(Params::RenderingPhysicsDebugFlags, 0));
        float scale = Params().Get(Params::RenderingPhysicsDebugScale, 1.0f);
        physics->ApplyDebugVisualizationFlags(flags, scale);
    }

    m_renderer.SetPhysicsDebugEnabled(Params().Get(Params::RenderingPhysicsDebugEnabled, false));
    m_renderer.SetPhysicsDebugDepthTestEnabled(Params().Get(Params::RenderingPhysicsDebugDepthTest, true));
}

void Application::UpdateWindowState() {
    auto* window = m_renderer.GetWindowAbstraction();
    if (!window) return;

    uint32_t width = window->GetWidth();
    uint32_t height = window->GetHeight();

    int posX, posY;
    window->GetPosition(posX, posY);

    bool maximized = window->IsMaximized();

    Params().Set(Params::WindowWidth, static_cast<int>(width));
    Params().Set(Params::WindowHeight, static_cast<int>(height));
    Params().Set(Params::WindowPosX, posX);
    Params().Set(Params::WindowPosY, posY);
    Params().Set(Params::WindowMaximized, maximized);
}

void Application::HandleWindowEvents() {
    auto& instance = Instance();
    if (auto* window = instance.m_renderer.GetWindowAbstraction()) {
        window->PollEvents();
    }
}
