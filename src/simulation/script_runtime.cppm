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
        ScriptRuntime(const SceneGraphs& graphs, ScriptCache& cache, std::function<void(std::string)> onPrint = nullptr,
                      const TranspileOptions& options = {}, const GPP::LuauLimits& limits = {});
        ~ScriptRuntime() override;
        ScriptRuntime(const ScriptRuntime&) = delete;
        ScriptRuntime& operator=(const ScriptRuntime&) = delete;

        // False when a graph failed to compile or load; the caller should fall back to the interpreter.
        [[nodiscard]] bool Ok() const;
        [[nodiscard]] const std::string& Error() const;
        [[nodiscard]] const std::string& LastRuntimeError() const;

        void SetGuidSource(const std::function<std::uint64_t()>& source) override;
        void SetTraceSink(ITraceSink* sink) override;
        [[nodiscard]] PendingWrites ExecuteStartEvent(const GPP::Scene& scene) override;
        [[nodiscard]] PendingWrites ExecuteTickEvent(const GPP::Scene& scene, float deltaTime) override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
