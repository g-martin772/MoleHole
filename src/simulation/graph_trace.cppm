export module MoleHole:Simulation.GraphTrace;

import std;
import :Simulation.AnimationGraph;

export namespace MoleHole
{
    enum class TraceSeverity { Info, Warning, Error };

    // Runtime-agnostic: any graph runtime (the interpreter now, a Luau transpile later) reports through this interface,
    // identifying graphs by name and nodes, pins and links by their graph ids (a source map for compiled runtimes).
    class ITraceSink
    {
    public:
        virtual ~ITraceSink() = default;
        virtual void OnTickBegin(std::string_view event) { (void)event; }
        virtual void OnTickEnd() {}
        virtual void OnNodeExecuted(std::string_view graph, int nodeId) { (void)graph; (void)nodeId; }
        virtual void OnLinkTraversed(std::string_view graph, int linkId) { (void)graph; (void)linkId; }
        virtual void OnPinEvaluated(std::string_view graph, int pinId, const Value& value)
        {
            (void)graph; (void)pinId; (void)value;
        }
        virtual void OnDiagnostic(std::string_view graph, int nodeId, TraceSeverity severity, std::string_view message)
        {
            (void)graph; (void)nodeId; (void)severity; (void)message;
        }
        // Returning true makes the runtime abort the remainder of the current tick before running the node.
        [[nodiscard]] virtual bool ShouldBreakBefore(std::string_view graph, int nodeId)
        {
            (void)graph; (void)nodeId;
            return false;
        }
    };

    enum class TraceKind { NodeExecuted, Diagnostic, Breakpoint };

    struct TraceEntry
    {
        std::uint64_t Tick{0};
        TraceKind Kind{TraceKind::NodeExecuted};
        TraceSeverity Severity{TraceSeverity::Info};
        std::string Graph;
        int NodeId{0};
        std::string Message;
    };

    struct NodeDiagnostic
    {
        TraceSeverity Severity{TraceSeverity::Warning};
        std::string Message;
        std::uint64_t Tick{0};
    };

    struct GraphNodeDiagnostic
    {
        std::string Graph;
        int NodeId{0};
        NodeDiagnostic Diagnostic;
    };

    struct BreakpointHit
    {
        std::string Graph;
        int NodeId{0};
    };

    // Breakpoint semantics: hitting a breakpoint aborts the rest of that tick and pauses graph execution (the simulation
    // keeps running); Continue resumes, Step runs one full tick with breakpoints ignored and pauses again.
    class TraceRecorder final : public ITraceSink
    {
    public:
        static constexpr std::size_t kMaxEntries = 400;

        void OnTickBegin(const std::string_view event) override
        {
            ++m_Tick;
            m_Event = std::string(event);
        }

        void OnTickEnd() override { m_IgnoreBreakpoints = false; }

        void OnNodeExecuted(const std::string_view graph, const int nodeId) override
        {
            m_NodeTicks[Key(graph, nodeId)] = m_Tick;
            Push({m_Tick, TraceKind::NodeExecuted, TraceSeverity::Info, std::string(graph), nodeId, m_Event});
        }

        void OnLinkTraversed(const std::string_view graph, const int linkId) override
        {
            m_LinkTicks[Key(graph, linkId)] = m_Tick;
        }

        void OnPinEvaluated(const std::string_view graph, const int pinId, const Value& value) override
        {
            m_PinValues[Key(graph, pinId)] = value;
        }

        void OnDiagnostic(const std::string_view graph, const int nodeId, const TraceSeverity severity,
                          const std::string_view message) override
        {
            auto& list = m_Diagnostics[Key(graph, nodeId)];
            const auto it = std::ranges::find(list, message, &NodeDiagnostic::Message);
            if (it != list.end())
            {
                it->Tick = m_Tick;
                it->Severity = severity;
                return;
            }
            list.push_back({severity, std::string(message), m_Tick});
            Push({m_Tick, TraceKind::Diagnostic, severity, std::string(graph), nodeId, std::string(message)});
        }

        [[nodiscard]] bool ShouldBreakBefore(const std::string_view graph, const int nodeId) override
        {
            if (m_IgnoreBreakpoints || !m_Breakpoints.contains(Key(graph, nodeId))) return false;
            m_Hit = BreakpointHit{std::string(graph), nodeId};
            m_Paused = true;
            Push({m_Tick, TraceKind::Breakpoint, TraceSeverity::Info, std::string(graph), nodeId, "Breakpoint hit"});
            return true;
        }

        // Called once per frame before ticking; false while paused unless a step was requested.
        [[nodiscard]] bool ShouldRunTick()
        {
            if (!m_Paused) return true;
            if (!m_StepPending) return false;
            m_StepPending = false;
            m_IgnoreBreakpoints = true;
            return true;
        }

        void Pause() { m_Paused = true; }
        void Continue()
        {
            m_Paused = false;
            m_StepPending = false;
            m_Hit.reset();
        }
        void Step()
        {
            if (m_Paused) m_StepPending = true;
        }

        [[nodiscard]] bool Paused() const { return m_Paused; }
        [[nodiscard]] const std::optional<BreakpointHit>& Hit() const { return m_Hit; }

        void ToggleBreakpoint(const std::string_view graph, const int nodeId)
        {
            const auto key = Key(graph, nodeId);
            if (!m_Breakpoints.erase(key)) m_Breakpoints.insert(key);
        }
        [[nodiscard]] bool HasBreakpoint(const std::string_view graph, const int nodeId) const
        {
            return m_Breakpoints.contains(Key(graph, nodeId));
        }

        [[nodiscard]] std::uint64_t Tick() const { return m_Tick; }
        [[nodiscard]] std::uint64_t NodeTick(const std::string_view graph, const int nodeId) const
        {
            return Lookup(m_NodeTicks, Key(graph, nodeId));
        }
        [[nodiscard]] std::uint64_t LinkTick(const std::string_view graph, const int linkId) const
        {
            return Lookup(m_LinkTicks, Key(graph, linkId));
        }
        [[nodiscard]] const Value* PinValue(const std::string_view graph, const int pinId) const
        {
            const auto it = m_PinValues.find(Key(graph, pinId));
            return it != m_PinValues.end() ? &it->second : nullptr;
        }

        // Diagnostics that were reported during the latest tick (so fixed problems disappear).
        [[nodiscard]] std::vector<NodeDiagnostic> ActiveDiagnostics(const std::string_view graph, const int nodeId) const
        {
            std::vector<NodeDiagnostic> result;
            const auto it = m_Diagnostics.find(Key(graph, nodeId));
            if (it == m_Diagnostics.end()) return result;
            for (const auto& d : it->second) if (d.Tick + 1 >= m_Tick) result.push_back(d);
            return result;
        }

        [[nodiscard]] std::vector<GraphNodeDiagnostic> AllActiveDiagnostics() const
        {
            std::vector<GraphNodeDiagnostic> result;
            for (const auto& [key, list] : m_Diagnostics)
            {
                for (const auto& d : list) if (d.Tick + 1 >= m_Tick) result.push_back({key.first, key.second, d});
            }
            return result;
        }

        [[nodiscard]] const std::deque<TraceEntry>& Entries() const { return m_Entries; }

        void ClearRuntime()
        {
            m_Entries.clear();
            m_NodeTicks.clear();
            m_LinkTicks.clear();
            m_PinValues.clear();
            m_Diagnostics.clear();
            m_Hit.reset();
            m_StepPending = false;
            m_IgnoreBreakpoints = false;
            m_Tick = 0;
        }

    private:
        using MapKey = std::pair<std::string, int>;

        static MapKey Key(const std::string_view graph, const int id) { return {std::string(graph), id}; }

        static std::uint64_t Lookup(const std::map<MapKey, std::uint64_t>& map, const MapKey& key)
        {
            const auto it = map.find(key);
            return it != map.end() ? it->second : 0;
        }

        void Push(TraceEntry entry)
        {
            m_Entries.push_back(std::move(entry));
            if (m_Entries.size() > kMaxEntries) m_Entries.pop_front();
        }

        std::uint64_t m_Tick{0};
        std::string m_Event;
        bool m_Paused{false};
        bool m_StepPending{false};
        bool m_IgnoreBreakpoints{false};
        std::optional<BreakpointHit> m_Hit;
        std::set<MapKey> m_Breakpoints;
        std::map<MapKey, std::uint64_t> m_NodeTicks;
        std::map<MapKey, std::uint64_t> m_LinkTicks;
        std::map<MapKey, Value> m_PinValues;
        std::map<MapKey, std::vector<NodeDiagnostic>> m_Diagnostics;
        std::deque<TraceEntry> m_Entries;
    };
}
