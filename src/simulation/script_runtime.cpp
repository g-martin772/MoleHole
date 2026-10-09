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
        };

        GPP::LuauVm Vm;
        std::vector<Script> Scripts;
        ITraceSink* Sink{nullptr};
        std::function<void(std::string)> OnPrint;
        std::function<std::uint64_t()> GuidSource{&GPP::GenerateGuid};
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
                call.PushValue(LookAtEulerDegrees(std::get<glm::vec3>(from), std::get<glm::vec3>(target)));
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
                Writes->push_back(GPP::MakeComponentFieldWrite(call.Entity(2), component, field, std::move(value)));
                return 0;
            });
            vm.Register("host", "spawn", [this](GPP::LuauNativeCall& call)
            {
                const int node = static_cast<int>(call.Number(1));
                const Value presetValue = call.ToValue(2, GPP::LuauType::String);
                const std::string preset = std::holds_alternative<std::string>(presetValue) ? std::get<std::string>(presetValue)
                                                                                           : std::string("Empty");
                const Value positionValue = call.ToValue(3, GPP::LuauType::Vec3);
                const glm::vec3 position = std::holds_alternative<glm::vec3>(positionValue) ? std::get<glm::vec3>(positionValue)
                                                                                            : glm::vec3(0.0f);
                const Value nameValue = call.ToValue(4, GPP::LuauType::String);
                const std::string name = std::holds_alternative<std::string>(nameValue) ? std::get<std::string>(nameValue) : "";

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
                    Writes->push_back([guid, sourceGuid, position, name](GPP::Scene& live)
                    {
                        const auto source = live.FindByGuid(sourceGuid);
                        const auto copy = live.CloneEntity(source, guid);
                        if (!live.IsValid(copy)) { return; }
                        if (auto* transform = live.Registry().try_get<GPP::TransformComponent>(copy)) { transform->Position = position; }
                        if (!name.empty()) { live.Registry().get<GPP::MetadataComponent>(copy).Name = name; }
                    });
                }
                Spawned.insert(guid);
                call.PushEntity(guid);
                return 1;
            });
            vm.Register("host", "destroy", [this](GPP::LuauNativeCall& call)
            {
                const std::uint64_t guid = call.Entity(2);
                if (guid == 0)
                {
                    Report(static_cast<int>(call.Number(1)), TraceSeverity::Warning, "Entity input is empty");
                    return 0;
                }
                Spawned.erase(guid);
                Writes->push_back([guid](GPP::Scene& live) { live.DestroyEntity(live.FindByGuid(guid)); });
                return 0;
            });
            vm.Register("host", "clone", [this](GPP::LuauNativeCall& call)
            {
                const std::uint64_t sourceGuid = call.Entity(2);
                if (sourceGuid == 0 || !EntityKnown(sourceGuid))
                {
                    Report(static_cast<int>(call.Number(1)), TraceSeverity::Warning, "Source entity is empty or not found");
                    return 0;
                }
                const Value positionValue = call.ToValue(3, GPP::LuauType::Vec3);
                const std::optional<glm::vec3> position = std::holds_alternative<glm::vec3>(positionValue)
                                                              ? std::optional(std::get<glm::vec3>(positionValue))
                                                              : std::nullopt;
                const std::uint64_t guid = GuidSource();
                Writes->push_back([guid, sourceGuid, position](GPP::Scene& live)
                {
                    const auto copy = live.CloneEntity(live.FindByGuid(sourceGuid), guid);
                    if (!live.IsValid(copy) || !position) { return; }
                    if (auto* transform = live.Registry().try_get<GPP::TransformComponent>(copy)) { transform->Position = *position; }
                });
                Spawned.insert(guid);
                call.PushEntity(guid);
                return 1;
            });
        }

        void ReportError(const Script& script, const GPP::LuauError& error)
        {
            const SourceLocation where = script.Source->Locate(error.Line);
            LastRuntimeError = (where.Graph.empty() ? script.Name : where.Graph) + ": " + error.Message;
            if (Sink)
            {
                Sink->OnDiagnostic(where.Graph.empty() ? script.Name : where.Graph, where.NodeId, TraceSeverity::Error, error.Message);
            }
        }

        PendingWrites Run(const char* event, const bool tick, const GPP::Scene& scene, const float deltaTime)
        {
            PendingWrites writes;
            if (Sink) Sink->OnTickBegin(event);
            const std::array<double, 1> args{deltaTime};
            for (auto& script : Scripts)
            {
                Scene = &scene;
                Writes = &writes;
                Spawned.clear();
                Stack.assign(1, script.Name);
                Aborted = false;
                Self = script.SelfGuid;
                auto result = tick ? Vm.Call(script.Handle, "tick", args) : Vm.Call(script.Handle, "start");
                if (!result) ReportError(script, result.error());
                if (Aborted) break;
            }
            Scene = nullptr;
            Writes = nullptr;
            if (Sink) Sink->OnTickEnd();
            return writes;
        }
    };

    ScriptRuntime::ScriptRuntime(const SceneGraphs& graphs, ScriptCache& cache, std::function<void(std::string)> onPrint,
                                 const TranspileOptions& options, const GPP::LuauLimits& limits)
        : m_Impl(std::make_unique<Impl>())
    {
        auto& impl = *m_Impl;
        impl.OnPrint = std::move(onPrint);
        impl.Vm.SetLimits(limits);
        try
        {
            GPP::RegisterMathBindings(impl.Vm);
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
            auto handle = impl.Vm.Load(source->Source, "=graph");
            if (!handle)
            {
                impl.Error = "graph '" + item.Name + "' failed to load: " + handle.error().Message;
                return;
            }
            impl.Scripts.push_back(Impl::Script{item.Name, item.EntityGuid, *handle, std::move(source)});
        }
    }

    ScriptRuntime::~ScriptRuntime() = default;

    bool ScriptRuntime::Ok() const { return m_Impl->Error.empty(); }
    const std::string& ScriptRuntime::Error() const { return m_Impl->Error; }
    const std::string& ScriptRuntime::LastRuntimeError() const { return m_Impl->LastRuntimeError; }

    void ScriptRuntime::SetGuidSource(const std::function<std::uint64_t()>& source) { m_Impl->GuidSource = source; }
    void ScriptRuntime::SetTraceSink(ITraceSink* sink) { m_Impl->Sink = sink; }

    PendingWrites ScriptRuntime::ExecuteStartEvent(const GPP::Scene& scene)
    {
        return m_Impl->Run("Start", false, scene, 0.0f);
    }

    PendingWrites ScriptRuntime::ExecuteTickEvent(const GPP::Scene& scene, const float deltaTime)
    {
        return m_Impl->Run("Tick", true, scene, deltaTime);
    }
}
