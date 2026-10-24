#include <gpp/hot_reload_build_id.h>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

#ifndef MOLEHOLE_VERSION
#define MOLEHOLE_VERSION "0.0.0"
#endif

int main(int argc, char* argv[])
{
    SetHotReloadHostBuildId(GPP_HOT_RELOAD_BUILD_ID);
    SetGlobalShaderDefines(Units::ShaderDefines());
    RegisterComponents();

    const std::vector<std::string> cliArgs(argv + 1, argv + argc);
    const auto startupArgs = ParseStartupArgs(cliArgs);

    if (const auto validatePath = ParseValidateSceneArg(argc, argv)) return RunValidateScene(*validatePath);

    if (const auto replayArgs = ParseReplayArgs(argc, argv)) return RunReplay(*replayArgs);

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

    const auto appInfo = MakeAppInfo(MOLEHOLE_VERSION);
    builder.SetAppInfo(appInfo);
    const auto paths = AppPaths::Resolve({
        .BinaryDir = builder.FS.GetBinaryDirectory(),
        .WorkingDir = builder.FS.GetWorkingDirectory(),
        .Info = appInfo,
        .Env = AppPaths::ProcessEnv()
    });

    StdioWmPrompter prompter;
    if (const auto desktopResult = HandleDesktopArgs(argc, argv, {
        .Info = appInfo,
        .ResourceDir = paths.ResourceDir(),
        .ConfigDir = paths.ConfigDir(),
        .Env = AppPaths::ProcessEnv(),
        .Prompter = &prompter,
        .Out = &std::cout
    }))
    {
        return *desktopResult;
    }
    EnsureRuntimeDirectories(paths);

    const auto logger = std::make_shared<Logger>();
    PruneLogFiles(paths.LogDir(), "molehole");
    const auto logFile = LogFilePath(paths.LogDir(), "molehole", CurrentProcessId());
    logger->CreateRotatingMultiLogger("GPP APP", logFile.string(), std::size_t{5} << 20, 5);
#ifdef NDEBUG
    logger->SetLevel(LogLevel::Info);
#else
    logger->SetLevel(LogLevel::Trace);
#endif
    builder.Services.AddSingleton<Logger>([logger](ServiceProvider&) -> std::shared_ptr<IService> { return logger; });
    InstallCrashHandler({.ReportDir = paths.LogDir(),
                         .AppName = "molehole",
                         .LogFile = logFile.string(),
                         .Buffer = logger->Buffer(),
                         .Flush = [logger] { logger->Flush(); }});

    builder.Configuration
           .AddJsonFile((paths.ResourceDir() / "config.json").string())
           .AddJsonFile((paths.ConfigDir() / "config.json").string(), true);
    if (paths.IsInstalled())
    {
        builder.Configuration.AddInMemory(InstalledAssetOverrides(paths));
    }
    const auto statePath = builder.FS.ResolvePath(paths.ConfigDir() / "molehole_state.yaml", PathAnchor::WorkingDir);
    const auto mode = SelectStartupMode(startupArgs, ReadStartupPrefs(statePath));
    const bool launcherMode = mode == StartupMode::Launcher;

    if (launcherMode) builder.Configuration.AddInMemory(LauncherWindowOverrides(appInfo.Name));
    if (!launcherMode && !exportArgs)
    {
        if (const auto report = FindPendingCrashReport(paths.LogDir()))
        {
            logger->Notifications()->Post(NotificationKind::Warning, "Last run crashed",
                                          std::format("Report: {}", report->string()), 0.0f, "crash-report");
            MarkCrashReportSeen(*report);
        }
    }
    builder.Configuration
           .AddEnvironmentVariables()
           .AddCommandLine(argc, argv);

    if (launcherMode) builder.ConfigureImGui({}, false);
    else builder.ConfigureImGui({"DockingEnable", "ViewportsEnable"}, true);

    const auto fontDirectory = paths.ResourceDir() / "font";
    const auto uiState = std::make_shared<UiState>();
    uiState->FontDirectory = fontDirectory;
    if (exportArgs)
    {
        builder.SetHeadless(true);
        uiState->PendingExport.Set(exportArgs->Request);
        uiState->ExitWhenExportDone = true;
        uiState->ExportTimeoutSeconds = exportArgs->TimeoutSeconds;
        uiState->PreviewSceneCamera = exportArgs->SceneCamera;
    }
    else
    {
        uiState->IntroActive = !launcherMode && !startupArgs.SkipIntro;
        uiState->LauncherMode = launcherMode;
        uiState->PendingNewScene = startupArgs.New;
        if (startupArgs.Template) uiState->PendingLoadTemplatePath.Set(*startupArgs.Template);
    }
    if (startupArgs.Scene) uiState->StartupScenePath = startupArgs.Scene;
    builder.Services.AddSingleton<UiState>([uiState](ServiceProvider&) -> std::shared_ptr<IService> { return uiState; });

    builder.Services.AddSingleton<RenderSettingsService>([cli, exportArgs](ServiceProvider&) -> std::shared_ptr<IService>
    {
        auto service = std::make_shared<RenderSettingsService>();
        if (!exportArgs && cli && cli->Request.Profile) service->SetProfileOverride(*cli->Request.Profile);
        return service;
    });
    builder.Services.AddSingleton<LatexRenderer>();
    builder.Services.AddHostedService<AppStateService>();

    RegisterLayers(builder, launcherMode, exportArgs.has_value());

    auto app = builder.Build();
    const int result = app->Run();
    app.reset();
    if (const auto relaunch = uiState->PendingRelaunch.Take())
    {
        if (const auto relaunched = RelaunchSelf(*relaunch); !relaunched)
        {
            std::cerr << "Relaunch failed: " << relaunched.error() << '\n';
            return 1;
        }
    }
    return result != 0 || !exportArgs ? result : uiState->ExportExitCode;
}
