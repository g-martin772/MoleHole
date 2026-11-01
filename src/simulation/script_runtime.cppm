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
    struct RenderBridge
    {
        std::function<std::string(const std::string& key, const std::string& text)> Set;
        std::function<std::optional<std::string>(const std::string& key)> Get;
    };

    using EventForward = std::function<void(const std::string& name, const std::vector<GPP::LuauValue>& args)>;

    void RegisterRenderBindings(GPP::LuauVm& vm, std::function<const RenderBridge&()> bridge);

    [[nodiscard]] std::shared_ptr<GPP::ScriptSimulationModule> MakeScriptModule(const GPP::AssetDirectories& assets,
                                                                                RenderBridge render = {});

    class ScriptRuntime final : public IGraphRuntime
    {
    public:
        ScriptRuntime(const SceneGraphs& graphs, ScriptCache& cache, std::function<void(std::string)> onPrint = nullptr,
                      const TranspileOptions& options = {}, const GPP::LuauLimits& limits = {},
                      const GPP::AssetDirectories* assets = nullptr);
        ~ScriptRuntime() override;
        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;

        [[nodiscard]] bool Ok() const;
        [[nodiscard]] const std::string& Error() const;
        [[nodiscard]] const std::string& LastRuntimeError() const;

        void SetRenderBridge(RenderBridge bridge);
        void SetEventForward(EventForward forward);
        void SetGuidSource(const std::function<std::uint64_t()>& source) override;
        void SetTraceSink(ITraceSink* sink) override;
        void SetRandom(std::shared_ptr<GPP::SimulationRandom> random) override;
        [[nodiscard]] PendingWrites ExecuteStartEvent(const GPP::Scene& scene) override;
        [[nodiscard]] PendingWrites ExecuteTickEvent(const GPP::Scene& scene, float deltaTime) override;
        void PostEvent(std::string name, std::vector<GPP::LuauValue> args) override;
        [[nodiscard]] std::vector<GPP::ScriptError> TakeScriptErrors() override;
        [[nodiscard]] std::vector<GPP::ScriptStatus> ScriptStatuses() const override;
        [[nodiscard]] std::size_t ActiveTasks() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
