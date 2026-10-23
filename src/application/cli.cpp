module MoleHole;

import :Application.Cli;
import :Rendering.Settings;
import GPP;
import std;

namespace MoleHole
{
    namespace
    {
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
    }

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
            else if (arg == "--scene-camera") args.SceneCamera = true;
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

    std::optional<std::string> ParseValidateSceneArg(int argc, char* argv[])
    {
        for (int i = 1; i < argc; ++i)
        {
            if (std::string_view(argv[i]) == "--validate-scene") return i + 1 < argc ? std::string(argv[i + 1]) : std::string();
        }
        return std::nullopt;
    }

    int RunValidateScene(const std::string& path)
    {
        if (path.empty())
        {
            std::cerr << "--validate-scene expects a scene file\n";
            return 2;
        }
        const auto diagnostics = GPP::ValidateSceneFile(path);
        for (const auto& diagnostic : diagnostics) std::println("{}", diagnostic.Format(path));
        const auto errors = GPP::CountDiagnostics(diagnostics, GPP::DiagnosticSeverity::Error);
        const auto warnings = GPP::CountDiagnostics(diagnostics, GPP::DiagnosticSeverity::Warning);
        std::ifstream file(path, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto versions = GPP::DescribeSceneVersions(text);
        std::println(std::cerr, "{}: {}{} error{}, {} warning{}", path, versions.empty() ? "" : versions + "; ", errors,
                     errors == 1 ? "" : "s", warnings, warnings == 1 ? "" : "s");
        return errors > 0 ? 1 : 0;
    }
}
