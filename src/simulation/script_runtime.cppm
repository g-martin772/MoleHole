export module MoleHole:Simulation.ScriptRuntime;

import std;
import GPP;
import :Simulation.AnimationGraph;
import :Simulation.SceneGraphs;
import :Simulation.GraphTrace;
import :Simulation.GraphIr;
import :Simulation.GraphTranspiler;
import :Simulation.AnimationGraphExecutor;

export namespace MoleHole
{
    // Drives the transpiled Luau of every enabled event graph with the interpreter's lifecycle and semantics.
    class ScriptRuntime final : public IGraphRuntime
    {
    public:
        // Script components (.luau files attached to entities) run when assets is given; component graphs always do.
        ScriptRuntime(const SceneGraphs& graphs, ScriptCache& cache, std::function<void(std::string)> onPrint = nullptr,
                      const TranspileOptions& options = {}, const GPP::LuauLimits& limits = {},
                      const GPP::AssetDirectories* assets = nullptr);
        ~ScriptRuntime() override;
        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;

        // False when a graph failed to compile or load; the caller should fall back to the interpreter.
        [[nodiscard]] bool Ok() const;
        [[nodiscard]] const std::string& Error() const;
        [[nodiscard]] const std::string& LastRuntimeError() const;

        void SetGuidSource(const std::function<std::uint64_t()>& source) override;
        void SetTraceSink(ITraceSink* sink) override;
        void SetRandom(std::shared_ptr<GPP::SimulationRandom> random) override;
        [[nodiscard]] PendingWrites ExecuteStartEvent(const GPP::Scene& scene) override;
        [[nodiscard]] PendingWrites ExecuteTickEvent(const GPP::Scene& scene, float deltaTime) override;
        void PostEvent(std::string name, std::vector<GPP::LuauValue> args) override;
        [[nodiscard]] std::vector<GPP::ScriptError> TakeScriptErrors() override;
        [[nodiscard]] std::vector<GPP::ScriptStatus> ScriptStatuses() const override;
        // Tasks currently suspended across all graphs (latent nodes waiting on time, conditions or events).
        [[nodiscard]] std::size_t ActiveTasks() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
