export module MoleHole:Application.SimulateCli;

import std;
import GPP;
import :Application.ReplayCli;

export namespace MoleHole
{
    struct SimulateArgs
    {
        std::string Path;
        std::optional<std::uint64_t> Ticks;
        std::optional<double> Dt;
        std::optional<std::uint64_t> Seed;
        std::string Out;
        std::string Error;
    };

    [[nodiscard]] std::optional<SimulateArgs> ParseSimulateArgs(int argc, char* argv[])
    {
        std::optional<SimulateArgs> found;
        for (int i = 1; i < argc; ++i)
        {
            if (std::string_view(argv[i]) == "--simulate")
            {
                found = SimulateArgs{};
                if (i + 1 < argc) found->Path = argv[i + 1];
                break;
            }
        }
        if (!found) return std::nullopt;
        auto& args = *found;

        const auto parse = [&]<typename T>(std::string_view flag, std::string_view text, T& out)
        {
            const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
            if (text.empty() || ec != std::errc{} || end != text.data() + text.size())
            {
                args.Error = std::format("{} expects a number, got '{}'", flag, text);
                return false;
            }
            return true;
        };

        for (int i = 1; i < argc && args.Error.empty(); ++i)
        {
            const std::string_view arg = argv[i];
            if (arg != "--ticks" && arg != "--dt" && arg != "--seed" && arg != "--out") continue;
            if (i + 1 >= argc)
            {
                args.Error = std::format("{} expects a value", arg);
                break;
            }
            const std::string_view value = argv[++i];
            if (arg == "--ticks")
            {
                std::uint64_t ticks = 0;
                if (parse(arg, value, ticks)) args.Ticks = ticks;
            }
            else if (arg == "--dt")
            {
                double dt = 0.0;
                if (parse(arg, value, dt)) args.Dt = dt;
            }
            else if (arg == "--seed")
            {
                std::uint64_t seed = 0;
                if (parse(arg, value, seed))
                {
                    if (seed == 0) args.Error = "--seed expects a nonzero integer";
                    else args.Seed = seed;
                }
            }
            else if (arg == "--out") args.Out = value;
        }
        return found;
    }

    [[nodiscard]] int RunSimulate(const SimulateArgs& args)
    {
        if (!args.Error.empty())
        {
            std::println(std::cerr, "{}", args.Error);
            return 2;
        }
        if (args.Path.empty() || args.Path.starts_with("--"))
        {
            std::println(std::cerr, "--simulate expects a scene file");
            return 2;
        }
        if (!args.Ticks)
        {
            std::println(std::cerr, "--simulate needs --ticks N");
            return 2;
        }

        std::ifstream file(args.Path, std::ios::binary);
        if (!file)
        {
            std::println(std::cerr, "{}: cannot open scene", args.Path);
            return 1;
        }
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};

        const auto diagnostics = GPP::ValidateSceneYaml(text);
        for (const auto& diagnostic : diagnostics) std::println(std::cerr, "{}", diagnostic.Format(args.Path));
        if (GPP::CountDiagnostics(diagnostics, GPP::DiagnosticSeverity::Error) > 0)
        {
            std::println(std::cerr, "{}: scene has errors, not simulating", args.Path);
            return 1;
        }

        GPP::Scene scene;
        try
        {
            scene.DeserializeFromYaml(text);
        }
        catch (const std::exception& error)
        {
            std::println(std::cerr, "{}: {}", args.Path, error.what());
            return 1;
        }

        auto result = GPP::RunHeadless(std::move(scene), MakeHeadlessModules(),
                                       {.Ticks = *args.Ticks, .Dt = args.Dt, .Seed = args.Seed});
        if (!result)
        {
            std::println(std::cerr, "{}", result.error());
            return 2;
        }
        if (!result->InitErrors.empty())
        {
            for (const auto& error : result->InitErrors) std::println(std::cerr, "{}: {}", args.Path, error);
            return 1;
        }

        if (!args.Out.empty())
        {
            std::ofstream out(args.Out, std::ios::binary | std::ios::trunc);
            if (out) out << result->Final.SerializeToYaml();
            if (!out || !out.flush())
            {
                std::println(std::cerr, "{}: cannot write output", args.Out);
                return 1;
            }
        }

        std::println("{}: {} ticks at {:g} Hz, sim time {:.9g} s, hash {}", args.Path, result->Ticks, result->TickRate,
                     result->SimTime, GPP::FormatHash(result->Hash));
        return 0;
    }
}
