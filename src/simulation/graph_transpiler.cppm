export module MoleHole:Simulation.GraphTranspiler;

import std;
import GPP;
import :Simulation.AnimationGraph;
import :Simulation.SceneGraphs;
import :Simulation.GraphIr;

export namespace MoleHole
{
    constexpr int kSourceMapGraphStride = 1 << 20;

    struct TranspileOptions
    {
        // Single-use stable pure nodes become nested expressions instead of memoized thunks (no per-pin trace values).
        bool InlinePure{false};
        bool operator==(const TranspileOptions&) const = default;
    };

    struct SourceLocation
    {
        std::string Graph;
        int NodeId{0};
    };

    struct TranspiledScript
    {
        std::string Source;
        std::vector<std::string> Graphs;
        GPP::LuauSourceMap Map;
        std::string Error;
        std::uint64_t Hash{0};

        [[nodiscard]] bool Ok() const { return Error.empty(); }
        [[nodiscard]] SourceLocation Locate(int line) const;
    };

    // Luau runtime library (vector math with monostate semantics, memoization, call stack) loaded before sealing the VM.
    [[nodiscard]] std::string_view GraphRuntimePrelude();

    [[nodiscard]] TranspiledScript TranspileGraph(const SceneGraphs& graphs, const NamedGraph& main, IrCache& cache,
                                                  const TranspileOptions& options = {});

    // Re-transpiles only when the graph or a function it calls changed.
    class ScriptCache
    {
    public:
        [[nodiscard]] std::shared_ptr<const TranspiledScript> Get(const SceneGraphs& graphs, const NamedGraph& main,
                                                                  const TranspileOptions& options = {});
        [[nodiscard]] IrCache& Ir() { return m_Ir; }
        [[nodiscard]] std::size_t Transpilations() const { return m_Transpilations; }

    private:
        struct Entry
        {
            std::uint64_t Key{0};
            std::shared_ptr<const TranspiledScript> Script;
        };
        IrCache m_Ir;
        std::unordered_map<std::string, Entry> m_Entries;
        std::size_t m_Transpilations{0};
    };
}
