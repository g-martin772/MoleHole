module;
#include <yaml-cpp/yaml.h>
module MoleHole;

import :Application.Startup;
import std;

namespace MoleHole
{
    namespace
    {
        constexpr std::array kValueFlags{
            "--export-image", "--export-video", "--width", "--height", "--duration", "--fps", "--sim-start",
            "--time-scale", "--export-timeout", "--profile", "--set", "--ray-step-size", "--max-ray-steps",
            "--gpu-precision", "--metric", "--prefix", "--scene", "--template", "--recover", "--validate-scene"};
    }

    StartupArgs ParseStartupArgs(const std::span<const std::string> args)
    {
        StartupArgs result;
        for (std::size_t i = 0; i < args.size(); ++i)
        {
            const std::string_view arg = args[i];
            const auto next = [&]() -> std::optional<std::string>
            {
                return i + 1 < args.size() ? std::optional(args[++i]) : std::nullopt;
            };

            if (arg == "--scene") result.Scene = next();
            else if (arg == "--template") result.Template = next();
            else if (arg == "--recover") result.Recover = next();
            else if (arg == "--new") result.New = true;
            else if (arg == "--launcher") result.Launcher = true;
            else if (arg == "--no-launcher") result.NoLauncher = true;
            else if (arg == "--skip-intro") result.SkipIntro = true;
            else if (arg == "--export-image" || arg == "--export-video")
            {
                result.Export = true;
                ++i;
            }
            else if (arg.starts_with("--"))
            {
                if (std::ranges::contains(kValueFlags, arg)) ++i;
            }
            else if (!arg.empty() && !result.Scene)
            {
                result.Scene = std::string(arg);
            }
        }
        return result;
    }

    StartupPrefs ReadStartupPrefs(const std::filesystem::path& stateFile)
    {
        StartupPrefs prefs;
        try
        {
            if (!std::filesystem::exists(stateFile)) return prefs;
            const auto node = YAML::LoadFile(stateFile.string());
            if (const auto v = node["ShowLauncher"]; v && v.IsScalar()) prefs.ShowLauncher = v.as<bool>(true);
            if (const auto v = node["LastScenePath"]; v && v.IsScalar()) prefs.LastScenePath = v.as<std::string>("");
        }
        catch (const std::exception&)
        {
            return StartupPrefs{};
        }
        if (!prefs.LastScenePath.empty())
        {
            std::error_code error;
            prefs.LastSceneExists = std::filesystem::is_regular_file(prefs.LastScenePath, error);
        }
        return prefs;
    }

    StartupMode SelectStartupMode(const StartupArgs& args, const StartupPrefs& prefs)
    {
        if (args.Export || args.Scene || args.Template || args.Recover || args.New) return StartupMode::Editor;
        if (args.Launcher) return StartupMode::Launcher;
        if (args.NoLauncher) return StartupMode::Editor;
        return prefs.ShowLauncher || !prefs.LastSceneExists ? StartupMode::Launcher : StartupMode::Editor;
    }

    std::string RelativeTimeText(const std::int64_t then, const std::int64_t now)
    {
        if (then <= 0) return {};
        const auto seconds = std::max<std::int64_t>(now - then, 0);
        const auto plural = [](const std::int64_t n, const char* unit)
        {
            return std::format("{} {}{} ago", n, unit, n == 1 ? "" : "s");
        };
        if (seconds < 60) return "just now";
        if (seconds < 3600) return plural(seconds / 60, "minute");
        if (seconds < 86400) return plural(seconds / 3600, "hour");
        if (seconds < 86400 * 30) return plural(seconds / 86400, "day");
        if (seconds < 86400 * 365) return plural(seconds / (86400 * 30), "month");
        return plural(seconds / (86400 * 365), "year");
    }
}
