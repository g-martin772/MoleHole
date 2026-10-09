module MoleHole;

import :Simulation.ScriptRuntime;
import :Simulation.AnimationGraph;
import :Simulation.AnimationGraphProperties;
import :Simulation.SceneGraphs;
import :Simulation.GraphTrace;
import :Simulation.GraphIr;
import :Simulation.GraphTranspiler;
import :Simulation.AnimationGraphExecutor;
import :Simulation.CameraMath;
import :Simulation.EntityPresets;
import std;
import glm;
import GPP;

namespace MoleHole
{
    namespace
    {
        std::optional<GPP::LuauType> ToLuau(const PinType type)
        {
            switch (type)
            {
            case PinType::Bool: return GPP::LuauType::Bool;
            case PinType::Float: return GPP::LuauType::Float;
            case PinType::Int: return GPP::LuauType::Int;
            case PinType::Vec2: return GPP::LuauType::Vec2;
            case PinType::Vec3: return GPP::LuauType::Vec3;
            case PinType::Vec4: return GPP::LuauType::Vec4;
            case PinType::String: return GPP::LuauType::String;
            case PinType::Object: return GPP::LuauType::Entity;
            case PinType::Flow: default: return std::nullopt;
            }
        }

        Value Convert(const GPP::LuauNativeCall& call, const int index, const PinType type)
        {
            return call.ToValueAuto(index, type == PinType::Int);
        }

        std::uint64_t ParseGuid(const std::string& text)
        {
            std::uint64_t guid = 0;
            std::from_chars(text.data(), text.data() + text.size(), guid);
            return guid;
        }
    }

    struct ScriptRuntime::Impl
    {
        struct Script
        {
            std::string Name;
            std::uint64_t SelfGuid{0};
            int Handle{0};
            std::shared_ptr<const TranspiledScript> Source;
            std::unique_ptr<GPP::LuauScheduler> Sched;
            // Component graph instances only.
            bool Component{false};
            bool Started{false};
            bool Seen{false};
            std::size_t Index{0};
            std::string LastError;
            std::vector<std::pair<std::string, GPP::FieldValue>> Props;
            bool Failed{false};
        };

        struct PendingEvent
        {
            std::string Name;
            std::vector<GPP::LuauValue> Args;
        };

        GPP::LuauVm Vm;
        std::shared_ptr<GPP::LuauTaskErrors> TaskErrors = std::make_shared<GPP::LuauTaskErrors>();
        std::shared_ptr<GPP::SceneBindings> Bindings = std::make_shared<GPP::SceneBindings>();
        std::shared_ptr<GPP::ScriptHostContext> HostContext;
        std::unique_ptr<GPP::ScriptHost> Host;
        std::vector<Script> Scripts;
        std::map<std::string, std::pair<std::shared_ptr<const TranspiledScript>, GPP::ScriptDescriptor>> ComponentGraphs;
        std::vector<GPP::ScriptError> ComponentErrors;
        std::mutex EventMutex;
        std::vector<PendingEvent> Events;
        std::vector<PendingEvent> Delivering;
        PendingEvent Current;
        ITraceSink* Sink{nullptr};
        std::function<void(std::string)> OnPrint;
        std::function<std::uint64_t()> GuidSource{&GPP::GenerateGuid};
        std::shared_ptr<GPP::SimulationRandom> Random;
        const GPP::Scene* Scene{nullptr};
        PendingWrites* Writes{nullptr};
        std::unordered_set<std::uint64_t> Spawned;
        std::vector<std::string> Stack;
        std::uint64_t Self{0};
        bool Aborted{false};
        std::string Error;
        std::string LastRuntimeError;

        const std::string& Graph() const { return Stack.back(); }

        void Report(const int node, const TraceSeverity severity, const std::string& message)
        {
            if (Sink) Sink->OnDiagnostic(Graph(), node, severity, message);
        }

        bool EntityKnown(const std::uint64_t guid) const
        {
            return Spawned.contains(guid) || Scene->IsValid(Scene->FindByGuid(guid));
        }

        void RegisterNatives()
        {
            auto& vm = Vm;
            vm.Register("host", "enter", [this](GPP::LuauNativeCall& call)
            {
                const int id = static_cast<int>(call.Number(1));
                if (Aborted) { call.PushBoolean(false); return 1; }
                if (Sink)
                {
                    if (Sink->ShouldBreakBefore(Graph(), id))
                    {
                        Aborted = true;
                        call.PushBoolean(false);
                        return 1;
                    }
                    Sink->OnNodeExecuted(Graph(), id);
                }
                call.PushBoolean(true);
                return 1;
            });
            vm.Register("host", "link", [this](GPP::LuauNativeCall& call)
            {
                if (!Aborted && Sink) Sink->OnLinkTraversed(Graph(), static_cast<int>(call.Number(1)));
                call.PushBoolean(!Aborted);
                return 1;
            });
            vm.Register("host", "alive", [this](GPP::LuauNativeCall& call)
            {
                call.PushBoolean(!Aborted);
                return 1;
            });
            vm.Register("host", "diag", [this](GPP::LuauNativeCall& call)
            {
                Report(static_cast<int>(call.Number(1)), static_cast<TraceSeverity>(static_cast<int>(call.Number(2))), call.String(3));
                return 0;
            });
            vm.Register("host", "pin", [this](GPP::LuauNativeCall& call)
            {
                if (Sink)
                {
                    Sink->OnPinEvaluated(Graph(), static_cast<int>(call.Number(1)),
                                         Convert(call, 3, static_cast<PinType>(static_cast<int>(call.Number(2)))));
                }
                return 0;
            });
            vm.Register("host", "log", [this](GPP::LuauNativeCall& call)
            {
                if (OnPrint) OnPrint(call.String(1));
                return 0;
            });
            vm.Register("host", "push", [this](GPP::LuauNativeCall& call)
            {
                Stack.push_back(call.String(1));
                return 0;
            });
            vm.Register("host", "pop", [this](GPP::LuauNativeCall&)
            {
                if (Stack.size() > 1) Stack.pop_back();
                return 0;
            });
            vm.Register("host", "ent", [](GPP::LuauNativeCall& call)
            {
                call.PushEntity(ParseGuid(call.String(1)));
                return 1;
            });
            vm.Register("host", "tostr", [](GPP::LuauNativeCall& call)
            {
                call.PushString(ValueToString(Convert(call, 1, static_cast<PinType>(static_cast<int>(call.Number(2))))));
                return 1;
            });
            vm.Register("host", "lookat", [](GPP::LuauNativeCall& call)
            {
                const Value from = call.ToValue(1, GPP::LuauType::Vec3);
                const Value target = call.ToValue(2, GPP::LuauType::Vec3);
                if (!std::holds_alternative<glm::vec3>(from) || !std::holds_alternative<glm::vec3>(target)) return 0;
                call.PushValue(GPP::LookAtEulerDegrees(std::get<glm::vec3>(from), std::get<glm::vec3>(target)));
                return 1;
            });
            vm.Register("host", "entity", [this](GPP::LuauNativeCall& call)
            {
                const int node = static_cast<int>(call.Number(1));
                const std::uint64_t guid = call.IsNil(2) ? Self : ParseGuid(call.String(2));
                if (Scene->IsValid(Scene->FindByGuid(guid)))
                {
                    call.PushEntity(guid);
                    return 1;
                }
                Report(node, TraceSeverity::Warning, guid == 0 ? "No target entity and the graph is not bound to one"
                                                              : "Target entity not found in the scene");
                return 0;
            });
            vm.Register("host", "decompose", [this](GPP::LuauNativeCall& call)
            {
                const int node = static_cast<int>(call.Number(1));
                const std::vector<Value> none;
                if (!call.IsEntity(3))
                {
                    Report(node, TraceSeverity::Warning, "Entity input is empty");
                    call.PushValues(none);
                    return 1;
                }
                const std::uint64_t guid = call.Entity(3);
                const entt::entity entity = Scene->FindByGuid(guid);
                if (!Scene->IsValid(entity))
                {
                    Report(node, TraceSeverity::Warning, "Entity " + std::to_string(guid) + " not found in the scene");
                    call.PushValues(none);
                    return 1;
                }
                const std::string component = call.String(2);
                const PropertyCategory* category = FindPropertyCategory(component);
                if (!category)
                {
                    Report(node, TraceSeverity::Error, "Unknown component '" + component + "'");
                    call.PushValues(none);
                    return 1;
                }
                std::vector<Value> values;
                for (const auto& property : category->Properties) values.push_back(property.Get(*Scene, entity));
                call.PushValues(values);
                return 1;
            });
            vm.Register("host", "setter", [this](GPP::LuauNativeCall& call)
            {
                const int node = static_cast<int>(call.Number(1));
                if (!call.IsEntity(3))
                {
                    Report(node, TraceSeverity::Warning, "Entity input is empty");
                    call.PushBoolean(false);
                    return 1;
                }
                const std::uint64_t guid = call.Entity(3);
                if (!EntityKnown(guid))
                {
                    Report(node, TraceSeverity::Warning, "Entity " + std::to_string(guid) + " not found in the scene");
                    call.PushBoolean(false);
                    return 1;
                }
                const std::string component = call.String(2);
                if (!FindPropertyCategory(component))
                {
                    Report(node, TraceSeverity::Error, "Unknown component '" + component + "'");
                    call.PushBoolean(false);
                    return 1;
                }
                call.PushBoolean(true);
                return 1;
            });
            vm.Register("host", "setfield", [this](GPP::LuauNativeCall& call)
            {
                const std::string component = call.String(1);
                const std::string field = call.String(3);
                const auto* info = GPP::FindComponentField(component, field);
                if (!info || call.IsNil(4)) return 0;
                Value value = call.ToValue(4, GPP::ToLuauType(info->Type));
                if (std::holds_alternative<std::monostate>(value)) return 0;
                Writes->push_back(GPP::SetFieldCommand{call.Entity(2), component, field, std::move(value)});
                return 0;
            });
            vm.Register("host", "spawn", [this](GPP::LuauNativeCall& call)
            {
                const Value presetValue = call.ToValue(2, GPP::LuauType::String);
                const Value positionValue = call.ToValue(3, GPP::LuauType::Vec3);
                const Value nameValue = call.ToValue(4, GPP::LuauType::String);
                return PushGuid(call, Spawn(static_cast<int>(call.Number(1)),
                                            std::holds_alternative<std::string>(presetValue) ? std::get<std::string>(presetValue) : "Empty",
                                            std::holds_alternative<glm::vec3>(positionValue) ? std::get<glm::vec3>(positionValue) : glm::vec3(0.0f),
                                            std::holds_alternative<std::string>(nameValue) ? std::get<std::string>(nameValue) : ""));
            });
            vm.Register("host", "destroy", [this](GPP::LuauNativeCall& call)
            {
                Destroy(static_cast<int>(call.Number(1)), call.Entity(2));
                return 0;
            });
            vm.Register("host", "clone", [this](GPP::LuauNativeCall& call)
            {
                const Value positionValue = call.ToValue(3, GPP::LuauType::Vec3);
                return PushGuid(call, Clone(static_cast<int>(call.Number(1)), call.Entity(2),
                                            std::holds_alternative<glm::vec3>(positionValue)
                                                ? std::optional(std::get<glm::vec3>(positionValue)) : std::nullopt));
            });
            vm.Register("host", "event", [this](GPP::LuauNativeCall& call)
            {
                call.PushString(Current.Name);
                for (const auto& arg : Current.Args) call.PushValue(arg);
                return static_cast<int>(1 + Current.Args.size());
            });
            vm.Register("host", "props", [this](GPP::LuauNativeCall& call)
            {
                call.PushMap(HostContext->Props);
                return 1;
            });

            // Script components use the same entity operations through the scene table.
            vm.Register("scene", "spawn", [this](GPP::LuauNativeCall& call)
            {
                const Value presetValue = call.ToValue(1, GPP::LuauType::String);
                const Value positionValue = call.ToValue(2, GPP::LuauType::Vec3);
                const Value nameValue = call.ToValue(3, GPP::LuauType::String);
                return PushGuid(call, Spawn(0, std::holds_alternative<std::string>(presetValue) ? std::get<std::string>(presetValue) : "Empty",
                                            std::holds_alternative<glm::vec3>(positionValue) ? std::get<glm::vec3>(positionValue) : glm::vec3(0.0f),
                                            std::holds_alternative<std::string>(nameValue) ? std::get<std::string>(nameValue) : ""));
            });
            vm.Register("scene", "destroy", [this](GPP::LuauNativeCall& call)
            {
                Destroy(0, call.Entity(1));
                return 0;
            });
            vm.Register("scene", "clone", [this](GPP::LuauNativeCall& call)
            {
                const Value positionValue = call.ToValue(2, GPP::LuauType::Vec3);
                return PushGuid(call, Clone(0, call.Entity(1),
                                            std::holds_alternative<glm::vec3>(positionValue)
                                                ? std::optional(std::get<glm::vec3>(positionValue)) : std::nullopt));
            });
        }

        static int PushGuid(GPP::LuauNativeCall& call, const std::uint64_t guid)
        {
            if (guid == 0) return 0;
            call.PushEntity(guid);
            return 1;
        }

        std::uint64_t Spawn(const int node, const std::string& preset, const glm::vec3& position, const std::string& name)
        {
            const std::uint64_t guid = GuidSource();
            if (FindEntityPreset(preset))
            {
                Writes->push_back([guid, preset, position, name](GPP::Scene& live)
                {
                    SpawnPreset(live, guid, preset, position, name);
                });
            }
            else
            {
                std::uint64_t sourceGuid = 0;
                for (auto [entity, metadata] : Scene->Registry().view<const GPP::MetadataComponent>().each())
                {
                    if (metadata.Name == preset) { sourceGuid = metadata.Guid; break; }
                }
                if (sourceGuid == 0)
                {
                    Report(node, TraceSeverity::Warning, "No preset or entity named '" + preset + "'");
                    return 0;
                }
                Writes->push_back(GPP::CloneEntityCommand{sourceGuid, guid});
                Writes->push_back(GPP::SetFieldCommand{guid, "Transform", "Position", position});
                if (!name.empty()) { Writes->push_back(GPP::SetFieldCommand{guid, "Metadata", "Name", name}); }
            }
            Spawned.insert(guid);
            return guid;
        }

        void Destroy(const int node, const std::uint64_t guid)
        {
            if (guid == 0)
            {
                Report(node, TraceSeverity::Warning, "Entity input is empty");
                return;
            }
            Spawned.erase(guid);
            Writes->push_back(GPP::DestroyEntityCommand{guid});
        }

        std::uint64_t Clone(const int node, const std::uint64_t sourceGuid, const std::optional<glm::vec3>& position)
        {
            if (sourceGuid == 0 || !EntityKnown(sourceGuid))
            {
                Report(node, TraceSeverity::Warning, "Source entity is empty or not found");
                return 0;
            }
            const std::uint64_t guid = GuidSource();
            Writes->push_back(GPP::CloneEntityCommand{sourceGuid, guid});
            if (position) { Writes->push_back(GPP::SetFieldCommand{guid, "Transform", "Position", *position}); }
            Spawned.insert(guid);
            return guid;
        }

        void ReportError(Script& script, const GPP::LuauError& error)
        {
            const SourceLocation where = script.Source->Locate(error.Line);
            LastRuntimeError = (where.Graph.empty() ? script.Name : where.Graph) + ": " + error.Message;
            if (Sink)
            {
                Sink->OnDiagnostic(where.Graph.empty() ? script.Name : where.Graph, where.NodeId, TraceSeverity::Error, error.Message);
            }
            script.Failed = true;
            if (script.Component && script.LastError != error.Message)
            {
                script.LastError = error.Message;
                ComponentErrors.push_back(GPP::ScriptError{script.SelfGuid, script.Index, script.Name, error.Message, error.Line});
            }
        }

        void DrainTaskErrors(Script& script)
        {
            for (const auto& error : TaskErrors->Take()) ReportError(script, error);
        }

        void BindScheduler(Script& script)
        {
            if (!script.Source->UsesTasks) return;
            script.Sched = std::make_unique<GPP::LuauScheduler>(Vm, TaskErrors);
            if (!script.Sched->Ok()) { script.Sched.reset(); return; }
            const std::array<int, 1> refs{script.Sched->Handle()};
            if (auto bound = Vm.CallWith(script.Handle, "bind", refs); !bound) ReportError(script, bound.error());
        }

        // Starts component graph instances for entities whose Scripts list names a component graph; drops stale ones.
        void SyncComponentGraphs(const GPP::Scene& scene)
        {
            if (ComponentGraphs.empty()) return;
            for (auto& script : Scripts) script.Seen = !script.Component;
            for (auto [entity, scripts, metadata] : scene.Registry().view<const GPP::ScriptsComponent, const GPP::MetadataComponent>().each())
            {
                for (std::size_t i = 0; i < scripts.Entries.size(); ++i)
                {
                    const auto& entry = scripts.Entries[i];
                    const auto graph = ComponentGraphs.find(entry.Name);
                    if (graph == ComponentGraphs.end()) continue;
                    auto existing = std::ranges::find_if(Scripts, [&](const Script& s)
                    {
                        return s.Component && s.SelfGuid == metadata.Guid && s.Index == i && s.Name == entry.Name;
                    });
                    if (existing == Scripts.end())
                    {
                        auto handle = Vm.Load(graph->second.first->Source, "=graph");
                        if (!handle)
                        {
                            LastRuntimeError = entry.Name + ": " + handle.error().Message;
                            continue;
                        }
                        Script created;
                        created.Name = entry.Name;
                        created.SelfGuid = metadata.Guid;
                        created.Handle = *handle;
                        created.Source = graph->second.first;
                        created.Component = true;
                        created.Index = i;
                        BindScheduler(created);
                        Scripts.push_back(std::move(created));
                        existing = Scripts.end() - 1;
                    }
                    existing->Seen = true;
                    existing->Props = GPP::ResolveProps(graph->second.second, entry);
                }
            }
            std::erase_if(Scripts, [this](const Script& script)
            {
                if (script.Seen) return false;
                Vm.Release(script.Handle);
                return true;
            });
        }

        void DeliverEvents(Script& script)
        {
            if (!script.Source->HandlesEvents || !script.Sched) return;
            for (auto& event : Delivering)
            {
                Current = event;
                if (auto result = Vm.Call(script.Handle, "dispatch"); !result) ReportError(script, result.error());
                DrainTaskErrors(script);
                if (Aborted) break;
            }
        }

        PendingWrites Run(const char* event, const bool tick, const GPP::Scene& scene, const float deltaTime)
        {
            PendingWrites writes;
            if (Sink) Sink->OnTickBegin(event);
            Scene = &scene;
            Writes = &writes;
            Bindings->Read = &scene;
            Bindings->Write = [&writes](std::function<void(GPP::Scene&)> write) { writes.push_back(std::move(write)); };
            Bindings->WriteCommand = [&writes](GPP::Command command) { writes.push_back(std::move(command)); };
            SyncComponentGraphs(scene);
            if (tick)
            {
                std::scoped_lock lock(EventMutex);
                Delivering = std::exchange(Events, {});
            }

            const std::array<double, 1> args{deltaTime};
            for (std::size_t i = 0; i < Scripts.size(); ++i)
            {
                auto& script = Scripts[i];
                Spawned.clear();
                Stack.assign(1, script.Name);
                Aborted = false;
                Self = script.SelfGuid;
                script.Failed = false;
                HostContext->Entity = script.SelfGuid;
                HostContext->Props = script.Props;
                if (!tick || (script.Component && !script.Started))
                {
                    script.Started = true;
                    if (auto result = Vm.Call(script.Handle, "start"); !result) ReportError(script, result.error());
                    DrainTaskErrors(script);
                }
                if (tick && !Aborted)
                {
                    if (script.Sched) script.Sched->Tick(deltaTime);
                    DrainTaskErrors(script);
                    DeliverEvents(script);
                    if (auto result = Vm.Call(script.Handle, "tick", args); !result) ReportError(script, result.error());
                    DrainTaskErrors(script);
                    if (!script.Failed) script.LastError.clear();
                }
                if (Aborted) break;
            }

            if (Host && !Aborted)
            {
                Stack.assign(1, "Scripts");
                tick ? Host->Tick(scene, deltaTime) : Host->Start(scene);
                for (auto& error : Host->TakeErrors())
                {
                    ComponentErrors.push_back(error);
                    LastRuntimeError = error.Script + ": " + error.Message;
                    if (Sink) Sink->OnDiagnostic(error.Script, 0, TraceSeverity::Error, error.Message);
                }
            }
            Delivering.clear();
            Scene = nullptr;
            Writes = nullptr;
            Bindings->Read = nullptr;
            Bindings->Write = nullptr;
            Bindings->WriteCommand = nullptr;
            if (Sink) Sink->OnTickEnd();
            return writes;
        }
    };

    ScriptRuntime::ScriptRuntime(const SceneGraphs& graphs, ScriptCache& cache, std::function<void(std::string)> onPrint,
                                 const TranspileOptions& options, const GPP::LuauLimits& limits,
                                 const GPP::AssetDirectories* assets)
        : m_Impl(std::make_unique<Impl>())
    {
        auto& impl = *m_Impl;
        impl.OnPrint = std::move(onPrint);
        impl.Vm.SetLimits(limits);
        try
        {
            GPP::RegisterMathBindings(impl.Vm);
            GPP::RegisterRandomBindings(impl.Vm, [&impl] { return impl.Random.get(); });
            GPP::RegisterSceneBindings(impl.Vm, impl.Bindings);
            GPP::RegisterTaskBindings(impl.Vm, impl.TaskErrors);
            impl.HostContext = GPP::RegisterScriptHostBindings(impl.Vm);
        }
        catch (const std::exception& e)
        {
            impl.Error = e.what();
            return;
        }
        impl.RegisterNatives();
        if (auto prelude = impl.Vm.RunTrusted(GraphRuntimePrelude(), "=graph_runtime"); !prelude)
        {
            impl.Error = "graph runtime prelude failed: " + prelude.error().Message;
            return;
        }
        impl.Vm.Seal();

        for (const auto& item : graphs.Items)
        {
            if (!item.Enabled || item.IsFunction) continue;
            auto source = cache.Get(graphs, item, options);
            if (!source->Ok())
            {
                impl.Error = "graph '" + item.Name + "': " + source->Error;
                return;
            }
            if (item.IsComponent)
            {
                impl.ComponentGraphs[item.Name] = {std::move(source), DescribeComponentGraph(item)};
                continue;
            }
            auto handle = impl.Vm.Load(source->Source, "=graph");
            if (!handle)
            {
                impl.Error = "graph '" + item.Name + "' failed to load: " + handle.error().Message;
                return;
            }
            Impl::Script script;
            script.Name = item.Name;
            script.SelfGuid = item.EntityGuid;
            script.Handle = *handle;
            script.Source = std::move(source);
            impl.BindScheduler(script);
            impl.Scripts.push_back(std::move(script));
        }

        if (assets)
        {
            impl.Host = std::make_unique<GPP::ScriptHost>(impl.Vm, impl.HostContext, impl.TaskErrors, *assets);
            impl.Host->SetIgnored([&impl](const std::string& name) { return impl.ComponentGraphs.contains(name); });
        }
    }

    ScriptRuntime::~ScriptRuntime() = default;

    bool ScriptRuntime::Ok() const { return m_Impl->Error.empty(); }
    const std::string& ScriptRuntime::Error() const { return m_Impl->Error; }
    const std::string& ScriptRuntime::LastRuntimeError() const { return m_Impl->LastRuntimeError; }

    void ScriptRuntime::SetGuidSource(const std::function<std::uint64_t()>& source) { m_Impl->GuidSource = source; }
    void ScriptRuntime::SetTraceSink(ITraceSink* sink) { m_Impl->Sink = sink; }

    void ScriptRuntime::SetRandom(std::shared_ptr<GPP::SimulationRandom> random)
    {
        m_Impl->Random = std::move(random);
    }

    PendingWrites ScriptRuntime::ExecuteStartEvent(const GPP::Scene& scene)
    {
        return m_Impl->Run("Start", false, scene, 0.0f);
    }

    PendingWrites ScriptRuntime::ExecuteTickEvent(const GPP::Scene& scene, const float deltaTime)
    {
        return m_Impl->Run("Tick", true, scene, deltaTime);
    }

    void ScriptRuntime::PostEvent(std::string name, std::vector<GPP::LuauValue> args)
    {
        std::scoped_lock lock(m_Impl->EventMutex);
        m_Impl->Events.push_back(Impl::PendingEvent{std::move(name), std::move(args)});
    }

    std::vector<GPP::ScriptError> ScriptRuntime::TakeScriptErrors() { return std::exchange(m_Impl->ComponentErrors, {}); }

    std::vector<GPP::ScriptStatus> ScriptRuntime::ScriptStatuses() const
    {
        std::vector<GPP::ScriptStatus> statuses;
        if (m_Impl->Host) statuses = m_Impl->Host->Statuses();
        for (const auto& script : m_Impl->Scripts)
        {
            if (script.Component) statuses.push_back({script.SelfGuid, script.Index, script.Name, script.LastError});
        }
        return statuses;
    }

    std::size_t ScriptRuntime::ActiveTasks() const
    {
        std::size_t count = 0;
        for (const auto& script : m_Impl->Scripts)
        {
            if (script.Sched) count += script.Sched->Active();
        }
        return count;
    }
}
