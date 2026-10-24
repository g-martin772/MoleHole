export module MoleHole:Application.Startup;

import std;

export namespace MoleHole
{
    enum class StartupMode { Editor, Launcher };

    struct StartupArgs
    {
        std::optional<std::string> Scene;
        std::optional<std::string> Template;
        std::optional<std::string> Recover;
        bool New = false;
        bool Launcher = false;
        bool NoLauncher = false;
        bool SkipIntro = false;
        bool Export = false;
    };

    struct StartupPrefs
    {
        bool ShowLauncher = true;
        std::string LastScenePath;
        bool LastSceneExists = false;
    };

    // `args` excludes argv[0]. A bare argument is the scene path.
    [[nodiscard]] StartupArgs ParseStartupArgs(std::span<const std::string> args);
    [[nodiscard]] StartupPrefs ReadStartupPrefs(const std::filesystem::path& stateFile);
    [[nodiscard]] StartupMode SelectStartupMode(const StartupArgs& args, const StartupPrefs& prefs);
    [[nodiscard]] std::string RelativeTimeText(std::int64_t then, std::int64_t now);
}
