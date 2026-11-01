export module MoleHole:Application.ScriptTestCli;

import std;
import GPP;
import :Application.ReplayCli;

export namespace MoleHole
{
    struct ScriptTestArgs
    {
        std::vector<std::string> Paths;
        std::string ScriptsDir = "scripts";
        std::optional<std::uint64_t> Seed;
        bool Verbose = false;
        std::string Error;
    };

    [[nodiscard]] std::optional<ScriptTestArgs> ParseScriptTestArgs(int argc, char* argv[])
    {
        std::optional<ScriptTestArgs> found;
        for (int i = 1; i < argc; ++i)
        {
            if (std::string_view(argv[i]) != "--test-scripts") continue;
            found = ScriptTestArgs{};
            for (int j = i + 1; j < argc && !std::string_view(argv[j]).starts_with("--"); ++j) found->Paths.emplace_back(argv[j]);
            break;
        }
        if (!found) return std::nullopt;
        auto& args = *found;

        for (int i = 1; i < argc && args.Error.empty(); ++i)
        {
            const std::string_view arg = argv[i];
            if (arg == "--verbose") args.Verbose = true;
            if (arg != "--scripts-dir" && arg != "--seed") continue;
            if (i + 1 >= argc)
            {
                args.Error = std::format("{} expects a value", arg);
                break;
            }
            const std::string_view value = argv[++i];
            if (arg == "--scripts-dir")
            {
                args.ScriptsDir = value;
                continue;
            }
            std::uint64_t seed = 0;
            const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), seed);
            if (value.empty() || ec != std::errc{} || end != value.data() + value.size() || seed == 0)
            {
                args.Error = "--seed expects a nonzero integer";
            }
            else args.Seed = seed;
        }
        return found;
    }

    [[nodiscard]] GPP::AssetOptions MakeScriptAssetOptions(const std::filesystem::path& scriptsDir)
    {
        GPP::AssetOptions options;
        options.Kinds[GPP::kScriptAssetKind] = GPP::AssetKindOptions{{scriptsDir}, {".lua", ".luau"}};
        return options;
    }

    [[nodiscard]] GPP::ScriptTestOptions MakeScriptTestOptions(const GPP::AssetDirectories& assets)
    {
        GPP::ScriptTestOptions options;
        options.Assets = &assets;
        options.MakeModules = [] { return MakeHeadlessModules(); };
        return options;
    }

    [[nodiscard]] int RunScriptTestsCli(const ScriptTestArgs& args)
    {
        if (!args.Error.empty())
        {
            std::println(std::cerr, "{}", args.Error);
            return 2;
        }
        const GPP::AssetDirectories assets(MakeScriptAssetOptions(args.ScriptsDir));
        auto options = MakeScriptTestOptions(assets);
        if (args.Seed) options.Seed = *args.Seed;

        std::vector<std::filesystem::path> files;
        for (const auto& path : args.Paths.empty() ? std::vector<std::string>{"tests/scripts"} : args.Paths)
        {
            std::error_code ec;
            if (!std::filesystem::exists(path, ec))
            {
                std::println(std::cerr, "{}: no such file or directory", path);
                return 2;
            }
            auto found = GPP::FindScriptTests(path);
            files.insert(files.end(), found.begin(), found.end());
        }

        const auto report = GPP::RunScriptTests(files, options);
        std::print("{}", GPP::FormatScriptTestReport(report, args.Verbose));
        return report.Ok() ? 0 : 1;
    }
}
