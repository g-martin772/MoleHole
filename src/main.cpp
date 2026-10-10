import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct CliExportArgs
    {
        bool Requested = false;
        ExportRequest Request;
        std::string ScenePath;
    };

    void EnsureRuntimeDirectories()
    {
        for (const char* directory : {".gpp/exports", ".gpp/hot-reload-cache", ".gpp/shader-cache"})
        {
            std::error_code error;
            std::filesystem::create_directories(directory, error);
        }
    }

    std::optional<CliExportArgs> ParseExportArgs(int argc, char* argv[])
    {
        CliExportArgs args;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            auto next = [&]() -> std::string
            {
                return (i + 1 < argc) ? std::string(argv[++i]) : std::string();
            };

            if (arg == "--export-image")
            {
                args.Requested = true;
                args.Request.RequestKind = ExportRequest::Kind::Image;
                args.Request.OutputPath = next();
            }
            else if (arg == "--export-video")
            {
                args.Requested = true;
                args.Request.RequestKind = ExportRequest::Kind::Video;
                args.Request.OutputPath = next();
            }
            else if (arg == "--width") args.Request.Width = static_cast<std::uint32_t>(std::stoul(next()));
            else if (arg == "--height") args.Request.Height = static_cast<std::uint32_t>(std::stoul(next()));
            else if (arg == "--duration") args.Request.DurationSeconds = std::stof(next());
            else if (arg == "--fps") args.Request.Framerate = std::stoi(next());
            else if (arg == "--scene") args.ScenePath = next();
            else if (arg == "--ray-step-size") args.Request.RayStepSize = std::stof(next());
            else if (arg == "--max-ray-steps") args.Request.MaxRaySteps = std::stoi(next());
        }
        return args.Requested ? std::optional(args) : std::nullopt;
    }
}

int main(int argc, char* argv[])
{
    EnsureRuntimeDirectories();
    MoleHole::RegisterComponents();

    const auto exportArgs = ParseExportArgs(argc, argv);

    auto builder = GuiApplicationBuilder();

    builder.Configuration
           .AddJsonFile("config.json")
           .AddCommandLine(argc, argv)
           .AddEnvironmentVariables();

    builder.ConfigureImGui({"DockingEnable", "ViewportsEnable"}, true);
    builder.SetTheme("molehole_theme.so", true);

    if (exportArgs)
    {
        builder.SetHeadless(true);
        builder.Services.AddSingleton<UiState>([exportArgs](ServiceProvider&) -> std::shared_ptr<IService>
        {
            auto state = std::make_shared<UiState>();
            state->PendingExport.Set(exportArgs->Request);
            state->ExitWhenExportDone = true;
            if (!exportArgs->ScenePath.empty())
            {
                state->StartupScenePath = exportArgs->ScenePath;
            }
            return state;
        });
    }
    else
    {
        builder.Services.AddSingleton<UiState>([](ServiceProvider&) -> std::shared_ptr<IService>
        {
            auto state = std::make_shared<UiState>();
            state->IntroActive = true;
            return state;
        });
    }

    builder.Services.AddSingleton<LatexRenderer>();
    builder.Services.AddHostedService<AppStateService>();

    if (!exportArgs)
    {
        builder.AddHotReloadableLayer("intro", "molehole_intro_layer.so").SetBufferTarget(kIntroBufferId);
        builder.AddHotReloadableLayer("dock-layout", "molehole_dock_layout_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("sidebar", "molehole_sidebar_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("topbar", "molehole_topbar_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("scene-window", "molehole_scene_window_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("camera-window", "molehole_camera_window_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("debug-window", "molehole_debug_window_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("system-window", "molehole_system_window_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("viewport-hud", "molehole_viewport_hud_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("simulation-controls", "molehole_simulation_controls_layer.so")
               .SetWindowTarget("main");
        builder.AddHotReloadableLayer("general-relativity-window", "molehole_general_relativity_window_layer.so")
               .SetWindowTarget("main");
        builder.AddHotReloadableLayer("science-window", "molehole_science_window_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("animation-graph-window", "molehole_animation_graph_window_layer.so")
               .SetWindowTarget("main");
        builder.AddHotReloadableLayer("tutorial-overlay", "molehole_tutorial_overlay_layer.so")
               .SetWindowTarget("main");
    }

    builder.AddHotReloadableLayer("viewport", "molehole_viewport_layer.so")
           .SetBufferTarget(kViewportBufferId);

    auto app = builder.Build();
    return app->Run();
}
