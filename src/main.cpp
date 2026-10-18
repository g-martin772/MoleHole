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
        bool ListSettings = false;
        ExportRequest Request;
        std::string ScenePath;
        float TimeoutSeconds = 120.0f;
        std::string Error;
    };

    void PrintSettings()
    {
        const auto& registry = RenderSettingRegistry();
        const RenderSettings defaults;
        for (const auto& group : registry.Groups())
        {
            std::println("{}", group);
            for (const auto* setting : registry.InGroup(group))
            {
                std::string options;
                for (const auto& option : setting->Meta.Options) options += (options.empty() ? "" : "|") + option;
                std::println("  {:<32} default {:<14} {}{}", setting->Key, registry.ToText(defaults, *setting),
                             setting->Meta.Label, options.empty() ? "" : "  [" + options + "]");
            }
        }
        std::println("\nProfiles:");
        for (const auto& profile : BuiltInRenderProfiles()) std::println("  {:<12} {}", profile.Name, profile.Description);
    }

    bool AddOverride(CliExportArgs& args, std::string key, std::string value)
    {
        RenderSettings scratch;
        if (const auto result = RenderSettingRegistry().SetFromText(scratch, key, value); !result)
        {
            args.Error = "--set " + key + "=" + value + ": " + result.error();
            return false;
        }
        std::erase_if(args.Request.Overrides, [&](const auto& entry) { return entry.first == key; });
        args.Request.Overrides.emplace_back(std::move(key), std::move(value));
        return true;
    }

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
            else if (arg == "--sim-start" || arg == "--time-scale")
            {
                const auto value = next();
                double number = std::numeric_limits<double>::quiet_NaN();
                std::from_chars(value.data(), value.data() + value.size(), number);
                const bool start = arg == "--sim-start";
                if (!std::isfinite(number) || (start ? number < 0.0 : number <= 0.0))
                {
                    args.Error = std::string(arg) + " expects a " + (start ? "number of seconds >= 0" : "positive number") + ", got '" + value + "'";
                    break;
                }
                (start ? args.Request.SimStart : args.Request.TimeScale) = number;
            }
            else if (arg == "--export-timeout")
            {
                const auto value = next();
                float seconds = -1.0f;
                std::from_chars(value.data(), value.data() + value.size(), seconds);
                if (seconds < 0.0f)
                {
                    args.Error = "--export-timeout expects seconds >= 0, got '" + value + "'";
                    break;
                }
                args.TimeoutSeconds = seconds;
            }
            else if (arg == "--profile") args.Request.Profile = next();
            else if (arg == "--set")
            {
                const auto pair = next();
                const auto equals = pair.find('=');
                if (equals == std::string::npos)
                {
                    args.Error = "--set expects key=value, got '" + pair + "'";
                    break;
                }
                if (!AddOverride(args, pair.substr(0, equals), pair.substr(equals + 1))) break;
            }
            else if (arg == "--list-settings") args.ListSettings = true;
            // Older flags, now spelled as settings
            else if (arg == "--ray-step-size") { if (!AddOverride(args, "transport.step_size", next())) break; }
            else if (arg == "--max-ray-steps") { if (!AddOverride(args, "transport.max_steps", next())) break; }
            else if (arg == "--gpu-precision") { if (!AddOverride(args, "transport.gpu_precision", next())) break; }
            else if (arg == "--physically-accurate") { if (!AddOverride(args, "transport.mode", "Integrated")) break; }
            else if (arg == "--metric") { if (!AddOverride(args, "spacetime.solution", next())) break; }
        }
        return (args.Requested || args.ListSettings || !args.Error.empty()) ? std::optional(args) : std::nullopt;
    }
}

namespace
{
    struct DebugParticlesArgs
    {
        std::string Scene;
        std::string Png;
        double Seconds{1.0};
        int Frames{2};
    };

    // --debug-particles out.png --scene s.yaml [--particle-seconds T] [--particle-frames N]: headless CPU run of the
    // scene's particle disk, scatter pictures at N+1 moments (no GUI, no GPU)
    std::optional<DebugParticlesArgs> ParseDebugParticles(int argc, char* argv[])
    {
        DebugParticlesArgs args;
        bool requested = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            const auto next = [&]() -> std::string { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
            if (arg == "--debug-particles") { args.Png = next(); requested = true; }
            else if (arg == "--scene") args.Scene = next();
            else if (arg == "--particle-seconds") args.Seconds = std::atof(next().c_str());
            else if (arg == "--particle-frames") args.Frames = std::atoi(next().c_str());
        }
        return requested ? std::optional(args) : std::nullopt;
    }
}

int main(int argc, char* argv[])
{
    EnsureRuntimeDirectories();
    SetGlobalShaderDefines(Units::ShaderDefines());
    MoleHole::RegisterComponents();

    if (const auto debug = ParseDebugParticles(argc, argv)) return MoleHole::RunDebugParticles(debug->Scene, debug->Png, debug->Seconds, debug->Frames);

    auto cli = ParseExportArgs(argc, argv);
    if (cli && !cli->Error.empty())
    {
        std::cerr << cli->Error << '\n';
        return 2;
    }
    if (cli && cli->ListSettings)
    {
        PrintSettings();
        return 0;
    }
    const auto exportArgs = cli && cli->Requested ? cli : std::nullopt;

    auto builder = GuiApplicationBuilder();

    builder.Configuration
           .AddJsonFile("config.json")
           .AddCommandLine(argc, argv)
           .AddEnvironmentVariables();

    builder.ConfigureImGui({"DockingEnable", "ViewportsEnable"}, true);
    builder.SetTheme("molehole_theme.so", true);

    std::shared_ptr<UiState> exportState;
    if (exportArgs)
    {
        builder.SetHeadless(true);
        exportState = std::make_shared<UiState>();
        builder.Services.AddSingleton<UiState>([exportArgs, exportState](ServiceProvider&) -> std::shared_ptr<IService>
        {
            auto state = exportState;
            state->PendingExport.Set(exportArgs->Request);
            state->ExitWhenExportDone = true;
            state->ExportTimeoutSeconds = exportArgs->TimeoutSeconds;
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

    builder.Services.AddSingleton<RenderSettingsService>([cli, exportArgs](ServiceProvider&) -> std::shared_ptr<IService>
    {
        auto service = std::make_shared<RenderSettingsService>();
        if (!exportArgs && cli && cli->Request.Profile) service->SetProfileOverride(*cli->Request.Profile);
        return service;
    });
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
        builder.AddHotReloadableLayer("simulation-window", "molehole_simulation_window_layer.so").SetWindowTarget("main");
        builder.AddHotReloadableLayer("render-window", "molehole_render_window_layer.so").SetWindowTarget("main");
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

    if (exportArgs)
    {
        builder.AddHotReloadableLayer("animation-graph-window", "molehole_animation_graph_window_layer.so")
               .SetBufferTarget(kViewportBufferId);
    }

    builder.AddHotReloadableLayer("viewport", "molehole_viewport_layer.so")
           .SetBufferTarget(kViewportBufferId);

    auto app = builder.Build();
    const int result = app->Run();
    return result != 0 || !exportState ? result : exportState->ExportExitCode;
}
