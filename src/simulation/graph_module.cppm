export module MoleHole:Simulation.GraphModule;

import std;
import glm;
import GPP;
import :Simulation.AnimationGraph;
import :Simulation.SceneGraphs;
import :Simulation.GraphTrace;
import :Simulation.GraphTranspiler;
import :Simulation.AnimationGraphExecutor;
import :Simulation.ScriptRuntime;

export namespace MoleHole
{
    class GraphSimulationModule final : public GPP::ISimulationModule
    {
    public:
        struct Options
        {
            std::function<void(std::string)> OnPrint;
            RenderBridge Render;
            std::shared_ptr<TraceRecorder> Trace = std::make_shared<TraceRecorder>();
        };

        explicit GraphSimulationModule(Options options) : m_Options(std::move(options)) {}

        void SetRandom(std::shared_ptr<GPP::SimulationRandom> random) { m_Random = std::move(random); }
        void SetScripts(std::weak_ptr<GPP::ScriptSimulationModule> scripts) { m_Scripts = std::move(scripts); }
        void SetUseLuau(const bool useLuau) { m_UseLuau.store(useLuau, std::memory_order_relaxed); }
        [[nodiscard]] TraceRecorder& Trace() { return *m_Options.Trace; }

        void PostEvent(std::string name, std::vector<GPP::LuauValue> args)
        {
            if (!m_Running.load(std::memory_order_relaxed)) return;
            std::scoped_lock lock(m_Mutex);
            if (m_Posted.size() < 256) m_Posted.emplace_back(std::move(name), std::move(args));
        }

        [[nodiscard]] bool Running() const noexcept { return m_Running.load(std::memory_order_relaxed); }

        [[nodiscard]] std::string Note() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Note;
        }

        [[nodiscard]] std::vector<GPP::ScriptStatus> Statuses() const
        {
            std::scoped_lock lock(m_Mutex);
            return m_Statuses;
        }

        [[nodiscard]] std::vector<GPP::ScriptError> TakeErrors()
        {
            std::scoped_lock lock(m_Mutex);
            return std::exchange(m_Errors, {});
        }

        void OnTick(GPP::Scene& scene, const float deltaTime) override
        {
            const bool useLuau = m_UseLuau.load(std::memory_order_relaxed);
            const bool fresh = !m_Executor;
            if (fresh || scene.ExtensionRevision() != m_SeenRevision || useLuau != m_BuiltLuau)
            {
                m_SeenRevision = scene.ExtensionRevision();
                if (fresh)
                {
                    m_Options.Trace->ClearRuntime();
                    m_Options.Trace->Continue();
                }
                Sync(scene, useLuau);
                if (fresh) Apply(scene, m_Executor->ExecuteStartEvent(scene));
            }
            Run(scene, deltaTime);
        }

        void Stop(GPP::Scene&)
        {
            m_Executor.reset();
            m_Fingerprint = 0;
            m_Running.store(false, std::memory_order_relaxed);
            m_Options.Trace->Continue();
            std::scoped_lock lock(m_Mutex);
            m_Posted.clear();
            m_Statuses.clear();
        }

    private:
        void Run(GPP::Scene& scene, const float deltaTime)
        {
            std::vector<std::pair<std::string, std::vector<GPP::LuauValue>>> posted;
            {
                std::scoped_lock lock(m_Mutex);
                posted.swap(m_Posted);
            }
            if (!m_Executor || !m_Options.Trace->ShouldRunTick()) return;
            for (auto& [name, args] : posted) m_Executor->PostEvent(std::move(name), std::move(args));
            Apply(scene, m_Executor->ExecuteTickEvent(scene, deltaTime));
            Publish();
        }

        static void Apply(GPP::Scene& scene, const PendingWrites& writes)
        {
            for (const auto& write : writes) write(scene);
        }

        void Publish()
        {
            auto errors = m_Executor->TakeScriptErrors();
            auto statuses = m_Executor->ScriptStatuses();
            std::scoped_lock lock(m_Mutex);
            m_Errors.insert(m_Errors.end(), std::make_move_iterator(errors.begin()), std::make_move_iterator(errors.end()));
            m_Statuses = std::move(statuses);
        }

        [[nodiscard]] static bool AnyLuauOnly(const SceneGraphs& graphs)
        {
            return std::ranges::any_of(graphs.Items, [](const NamedGraph& item)
            {
                return item.Enabled && std::ranges::any_of(item.Graph.Nodes, [](const Node& n) { return IsLuauOnly(n); });
            });
        }

        [[nodiscard]] std::uint64_t Fingerprint(const SceneGraphs& graphs, const bool useLuau)
        {
            std::uint64_t hash = useLuau ? 1469598103934665603ull : 7;
            const auto mix = [&](const std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
            for (const auto& item : graphs.Items)
            {
                mix(std::hash<std::string>{}(item.Name));
                mix((item.Enabled ? 1u : 0u) | (item.IsFunction ? 2u : 0u) | (item.IsComponent ? 4u : 0u));
                mix(item.EntityGuid);
                if (!item.Enabled || item.IsFunction) continue;
                const auto script = m_Cache.Get(graphs, item);
                mix(std::hash<std::string>{}(script->Ok() ? script->Source : script->Error));
            }
            return hash;
        }

        bool Sync(GPP::Scene& scene, const bool useLuau)
        {
            const auto graphs = LoadSceneGraphs(scene);
            const auto fingerprint = Fingerprint(graphs, useLuau);
            if (m_Executor && fingerprint == m_Fingerprint) return false;
            m_Fingerprint = fingerprint;
            m_BuiltLuau = useLuau;
            Build(graphs, useLuau);
            std::set<std::string> components;
            for (const auto& item : graphs.Items)
            {
                if (item.Enabled && !item.IsFunction && item.IsComponent) components.insert(item.Name);
            }
            if (const auto scripts = m_Scripts.lock()) scripts->SetIgnored(std::move(components));
            m_Running.store(true, std::memory_order_relaxed);
            return true;
        }

        void Build(const SceneGraphs& graphs, const bool useLuau)
        {
            std::string note;
            m_Executor.reset();
            if (useLuau)
            {
                auto script = std::make_unique<ScriptRuntime>(graphs, m_Cache, m_Options.OnPrint);
                if (script->Ok())
                {
                    script->SetRenderBridge(m_Options.Render);
                    script->SetEventForward([scripts = m_Scripts](const std::string& name, const std::vector<GPP::LuauValue>& args)
                    {
                        if (const auto module = scripts.lock()) module->PostEvent(name, args);
                    });
                    m_Executor = std::move(script);
                }
                else note = "Luau runtime unavailable, using the interpreter: " + script->Error();
            }
            if (!m_Executor) m_Executor = std::make_unique<GraphSetExecutor>(graphs, m_Options.OnPrint);
            if (dynamic_cast<ScriptRuntime*>(m_Executor.get()) == nullptr && AnyLuauOnly(graphs))
            {
                const std::string text = "The interpreter cannot run latent, event or flow-control nodes marked Luau runtime only; reaching one reports an error.";
                note = note.empty() ? text : note + "\n" + text;
            }
            m_Executor->SetTraceSink(m_Options.Trace.get());
            m_Executor->SetRandom(m_Random);
            if (m_Random)
            {
                m_Executor->SetGuidSource([random = m_Random]
                {
                    const auto guid = random->NextU64();
                    return guid != 0 ? guid : 1;
                });
            }
            std::scoped_lock lock(m_Mutex);
            m_Note = std::move(note);
        }

        Options m_Options;
        std::shared_ptr<GPP::SimulationRandom> m_Random;
        std::weak_ptr<GPP::ScriptSimulationModule> m_Scripts;
        ScriptCache m_Cache;
        std::unique_ptr<IGraphRuntime> m_Executor;
        std::uint64_t m_Fingerprint{0};
        std::uint64_t m_SeenRevision{0};
        bool m_BuiltLuau{true};
        std::atomic<bool> m_UseLuau{true};
        std::atomic<bool> m_Running{false};
        mutable std::mutex m_Mutex;
        std::vector<std::pair<std::string, std::vector<GPP::LuauValue>>> m_Posted;
        std::string m_Note;
        std::vector<GPP::ScriptStatus> m_Statuses;
        std::vector<GPP::ScriptError> m_Errors;
    };
}
