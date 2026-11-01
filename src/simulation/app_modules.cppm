export module MoleHole:Simulation.AppModules;

import std;
import GPP;
import :Simulation.Gravity;
import :Simulation.GeodesicBodies;
import :Simulation.NBody;
import :Simulation.BodyRotation;
import :Simulation.ScriptRuntime;
import :Simulation.GraphModule;

export namespace MoleHole
{
    struct AppSimulationModules
    {
        std::shared_ptr<GraphSimulationModule> Graphs;
        std::shared_ptr<GPP::ScriptSimulationModule> Scripts;
        std::shared_ptr<GravitySimulationModule> Gravity;
        std::shared_ptr<GPP::PhysicsSimulationModule> Physics;
        std::shared_ptr<GeodesicSimulationModule> Geodesics;
        std::shared_ptr<NBodySimulationModule> NBody;
        std::shared_ptr<BodyRotationModule> Rotation;

        [[nodiscard]] std::vector<std::shared_ptr<GPP::ISimulationModule>> All() const
        {
            return {Graphs, Scripts, Gravity, Physics, Geodesics, NBody, Rotation};
        }

        void Attach(GPP::SimulationRunner& runner) const
        {
            Scripts->SetRandom(runner.Random());
            Scripts->SetInput(runner.Input());
            Graphs->SetRandom(runner.Random());
        }
    };

    [[nodiscard]] inline AppSimulationModules MakeAppSimulationModules(const std::shared_ptr<GPP::EventDispatcher>& dispatcher,
                                                                       const std::shared_ptr<GPP::Logger>& logger,
                                                                       const GPP::AssetDirectories& assets,
                                                                       RenderBridge render = {},
                                                                       std::function<void(std::string)> onPrint = {},
                                                                       std::shared_ptr<TraceRecorder> trace = nullptr)
    {
        AppSimulationModules modules;
        modules.Physics = std::make_shared<GPP::PhysicsSimulationModule>(dispatcher, logger);
        modules.Gravity = std::make_shared<GravitySimulationModule>(modules.Physics, dispatcher, logger);
        modules.Geodesics = std::make_shared<GeodesicSimulationModule>(logger);
        modules.NBody = std::make_shared<NBodySimulationModule>(logger);
        modules.Rotation = std::make_shared<BodyRotationModule>();
        modules.Scripts = MakeScriptModule(assets, render);
        GraphSimulationModule::Options graphs{.OnPrint = std::move(onPrint), .Render = std::move(render)};
        if (trace) graphs.Trace = std::move(trace);
        modules.Graphs = std::make_shared<GraphSimulationModule>(std::move(graphs));
        modules.Graphs->SetScripts(modules.Scripts);
        modules.Scripts->SetPhysics(modules.Physics);
        return modules;
    }
}
