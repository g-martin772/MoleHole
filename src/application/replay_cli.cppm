export module MoleHole:Application.ReplayCli;

import std;
import GPP;
import :Simulation.Gravity;
import :Simulation.GeodesicBodies;
import :Simulation.NBody;
import :Simulation.BodyRotation;

export namespace MoleHole
{
    struct ReplayArgs
    {
        std::string Path;
        bool Verify = false;
    };

    [[nodiscard]] std::optional<ReplayArgs> ParseReplayArgs(int argc, char* argv[])
    {
        std::optional<ReplayArgs> args;
        bool verify = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg = argv[i];
            if (arg == "--replay") args = ReplayArgs{i + 1 < argc ? std::string(argv[i + 1]) : std::string(), false};
            else if (arg == "--verify") verify = true;
        }
        if (args) args->Verify = verify;
        return args;
    }

    // The simulation module set the viewport runs, without UI state.
    [[nodiscard]] std::vector<std::shared_ptr<GPP::ISimulationModule>> MakeHeadlessModules()
    {
        auto logger = std::make_shared<GPP::Logger>();
        auto dispatcher = std::make_shared<GPP::EventDispatcher>();
        auto physics = std::make_shared<GPP::PhysicsSimulationModule>(dispatcher, logger);
        auto gravity = std::make_shared<GravitySimulationModule>(physics, dispatcher, logger);
        return {gravity, physics, std::make_shared<GeodesicSimulationModule>(logger),
                std::make_shared<NBodySimulationModule>(logger), std::make_shared<BodyRotationModule>()};
    }

    // Headless: no window, no Vulkan. Exit 0 when every recorded hash matches, 1 on divergence, 2 on usage or load errors.
    [[nodiscard]] int RunReplay(const ReplayArgs& args)
    {
        if (args.Path.empty())
        {
            std::cerr << "--replay expects a recording file\n";
            return 2;
        }
        std::string error;
        const auto recording = GPP::LoadRecording(args.Path, &error);
        if (!recording)
        {
            std::println(std::cerr, "{}", error);
            return 2;
        }

        std::println("{}: {} ticks from tick {}, {} commands, {} checkpoints every {} ticks", args.Path,
                     recording->Ticks(), recording->StartTick, recording->Commands.size(), recording->Checkpoints.size(),
                     recording->CheckpointInterval);
        if (!args.Verify)
        {
            std::println("Pass --verify to replay and compare the recorded hashes");
            return 0;
        }
        const auto result = GPP::VerifyReplay(*recording, MakeHeadlessModules());
        std::println("{}: {}", result.Ok ? "OK" : "DIVERGED", result.Message);
        return result.Ok ? 0 : 1;
    }
}
