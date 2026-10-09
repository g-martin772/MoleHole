module MoleHole;

import :Simulation.AnimationGraphExecutor;
import :Simulation.AnimationGraph;
import :Simulation.AnimationGraphProperties;
import :Simulation.SceneGraphs;
import :Simulation.GraphFunctions;
import :Simulation.CameraMath;
import :Simulation.EntityPresets;
import :Simulation.Components;
import std;
import glm;
import GPP;

namespace MoleHole
{
    namespace
    {
        constexpr int kMaxForIterations = 100000;
        constexpr int kMaxCallDepth = 16;
        constexpr int kMaxFlowDepth = 512;
    }

    GraphExecutor::GraphExecutor(const AnimationGraphData& graph, std::function<void(std::string)> onPrint,
                                 const std::uint64_t selfGuid)
        : m_Graph(graph), m_OnPrint(std::move(onPrint)), m_SelfGuid(selfGuid), m_GuidSource(&GPP::GenerateGuid)
    {
        for (const auto& variable : graph.Variables)
        {
            if (!std::holds_alternative<std::monostate>(variable.Default)) { m_Variables[variable.Name] = variable.Default; }
        }
    }

    GraphSetExecutor::GraphSetExecutor(const SceneGraphs& graphs, std::function<void(std::string)> onPrint)
        : m_Functions(std::make_unique<FunctionLibrary>(BuildFunctionLibrary(graphs)))
    {
        for (const auto& item : graphs.Items)
        {
            if (!item.Enabled || item.IsFunction) continue;
            m_Executors.push_back(std::make_unique<GraphExecutor>(item.Graph, onPrint, item.EntityGuid));
            m_Executors.back()->SetFunctionLibrary(m_Functions.get());
        }
    }

    void GraphSetExecutor::SetGuidSource(const std::function<std::uint64_t()>& source)
    {
        for (auto& executor : m_Executors) executor->SetGuidSource(source);
    }

    PendingWrites GraphSetExecutor::ExecuteStartEvent(const GPP::Scene& scene)
    {
        PendingWrites writes;
        for (auto& executor : m_Executors)
        {
            auto result = executor->ExecuteStartEvent(scene);
            std::ranges::move(result, std::back_inserter(writes));
        }
        return writes;
    }

    PendingWrites GraphSetExecutor::ExecuteTickEvent(const GPP::Scene& scene, const float deltaTime)
    {
        PendingWrites writes;
        for (auto& executor : m_Executors)
        {
            auto result = executor->ExecuteTickEvent(scene, deltaTime);
            std::ranges::move(result, std::back_inserter(writes));
        }
        return writes;
    }

    PendingWrites GraphExecutor::ExecuteStartEvent(const GPP::Scene& scene)
    {
        m_PinValues.clear();
        ExecutionContext ctx{.Scene = scene, .DeltaTime = 0.0f, .Writes = {}};

        for (const auto& node : m_Graph.Nodes)
        {
            if (node.Type == NodeType::Event && node.SubType == NodeSubType::Start && !node.Outputs.empty())
            {
                ExecuteFlowFromPin(node.Outputs[0].Id, ctx);
            }
        }

        return std::move(ctx.Writes);
    }

    PendingWrites GraphExecutor::ExecuteTickEvent(const GPP::Scene& scene, const float deltaTime)
    {
        m_PinValues.clear();
        ExecutionContext ctx{.Scene = scene, .DeltaTime = deltaTime, .Writes = {}};

        for (const auto& node : m_Graph.Nodes)
        {
            if (node.Type == NodeType::Event && node.SubType == NodeSubType::Tick && node.Outputs.size() > 1)
            {
                m_PinValues[node.Outputs[1].Id] = deltaTime;
                ExecuteFlowFromPin(node.Outputs[0].Id, ctx);
            }
        }

        return std::move(ctx.Writes);
    }

    void GraphExecutor::ExecuteFlowFromPin(const int pinId, ExecutionContext& ctx)
    {
        if (m_FlowDepth >= kMaxFlowDepth) { return; }
        ++m_FlowDepth;
        for (const auto& link : m_Graph.Links)
        {
            if (m_Returned) { break; }
            if (link.StartPinId == pinId)
            {
                if (const Node* target = m_Graph.FindNodeByInputPin(link.EndPinId))
                {
                    ExecuteNode(target, ctx);
                }
            }
        }
        --m_FlowDepth;
    }

    void GraphExecutor::ExecuteNode(const Node* node, ExecutionContext& ctx)
    {
        switch (node->Type)
        {
        case NodeType::Print:
            ExecutePrint(node, ctx);
            if (!node->Outputs.empty()) { ExecuteFlowFromPin(node->Outputs[0].Id, ctx); }
            break;
        case NodeType::Control:
            ExecuteControlFlow(node, ctx);
            break;
        case NodeType::Reroute:
            if (!node->Outputs.empty()) { ExecuteFlowFromPin(node->Outputs[0].Id, ctx); }
            break;
        case NodeType::Call:
            ExecuteCall(node, ctx);
            if (!node->Outputs.empty() && node->Outputs[0].Type == PinType::Flow) { ExecuteFlowFromPin(node->Outputs[0].Id, ctx); }
            break;
        case NodeType::Entity:
            ExecuteEntityNode(node, ctx);
            if (!node->Outputs.empty()) { ExecuteFlowFromPin(node->Outputs[0].Id, ctx); }
            break;
        case NodeType::Setter:
            ExecuteSetter(node, ctx);
            if (!node->Outputs.empty()) { ExecuteFlowFromPin(node->Outputs[0].Id, ctx); }
            break;
        case NodeType::Variable:
            if (node->SubType == NodeSubType::VariableSet)
            {
                ExecuteVariableSet(node, ctx);
                if (!node->Outputs.empty()) { ExecuteFlowFromPin(node->Outputs[0].Id, ctx); }
            }
            break;
        default:
            break;
        }
    }

    Value GraphExecutor::EvaluatePinValue(const int pinId, ExecutionContext& ctx)
    {
        if (const auto it = m_PinValues.find(pinId); it != m_PinValues.end())
        {
            return it->second;
        }

        int outputPinId = 0;
        for (const auto& link : m_Graph.Links)
        {
            if (link.EndPinId == pinId)
            {
                outputPinId = link.StartPinId;
                break;
            }
        }
        if (outputPinId == 0) { return std::monostate{}; }

        if (const auto it = m_PinValues.find(outputPinId); it != m_PinValues.end())
        {
            return it->second;
        }

        const Node* sourceNode = m_Graph.FindNodeByOutputPin(outputPinId);
        if (!sourceNode) { return std::monostate{}; }

        if (!m_Evaluating.insert(outputPinId).second) { return std::monostate{}; }
        struct Release
        {
            std::unordered_set<int>& Set;
            int Pin;
            ~Release() { Set.erase(Pin); }
        } release{m_Evaluating, outputPinId};

        if (sourceNode->Type == NodeType::Call)
        {
            if (!sourceNode->Outputs.empty() && sourceNode->Outputs[0].Type != PinType::Flow) { ExecuteCall(sourceNode, ctx); }
            const auto it = m_PinValues.find(outputPinId);
            return it != m_PinValues.end() ? it->second : Value{std::monostate{}};
        }

        if (sourceNode->Type == NodeType::Decomposer)
        {
            ExecuteDecomposer(sourceNode, ctx);
            if (const auto it = m_PinValues.find(outputPinId); it != m_PinValues.end())
            {
                return it->second;
            }
            return std::monostate{};
        }

        Value result = EvaluateNode(sourceNode, ctx);
        m_PinValues[outputPinId] = result;
        return result;
    }

    Value GraphExecutor::EvaluateNode(const Node* node, ExecutionContext& ctx)
    {
        switch (node->Type)
        {
        case NodeType::Constant:
            return ExecuteConstant(node);
        case NodeType::Function:
            return ExecuteMathOperation(node, ctx);
        case NodeType::Decomposer:
            ExecuteDecomposer(node, ctx);
            return std::monostate{};
        case NodeType::Getter:
            return ExecuteSceneGetter(node, ctx);
        case NodeType::Reroute:
            return node->Inputs.empty() ? Value{std::monostate{}} : EvaluatePinValue(node->Inputs[0].Id, ctx);
        case NodeType::Variable:
            if (node->SubType == NodeSubType::VariableGet) { return ExecuteVariableGet(node); }
            break;
        default:
            break;
        }
        return std::monostate{};
    }

    Value GraphExecutor::ExecuteConstant(const Node* node)
    {
        return node->ConstantValue;
    }

    Value GraphExecutor::ExecuteMathOperation(const Node* node, ExecutionContext& ctx)
    {
        const auto& in = node->Inputs;

        switch (node->SubType)
        {
        case NodeSubType::Add:
        {
            const Value a = EvaluatePinValue(in[0].Id, ctx);
            const Value b = EvaluatePinValue(in[1].Id, ctx);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) { return std::get<float>(a) + std::get<float>(b); }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) { return std::get<glm::vec2>(a) + std::get<glm::vec2>(b); }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) { return std::get<glm::vec3>(a) + std::get<glm::vec3>(b); }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) { return std::get<glm::vec4>(a) + std::get<glm::vec4>(b); }
            break;
        }
        case NodeSubType::Sub:
        {
            const Value a = EvaluatePinValue(in[0].Id, ctx);
            const Value b = EvaluatePinValue(in[1].Id, ctx);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) { return std::get<float>(a) - std::get<float>(b); }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) { return std::get<glm::vec2>(a) - std::get<glm::vec2>(b); }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) { return std::get<glm::vec3>(a) - std::get<glm::vec3>(b); }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) { return std::get<glm::vec4>(a) - std::get<glm::vec4>(b); }
            break;
        }
        case NodeSubType::Mul:
        {
            const Value a = EvaluatePinValue(in[0].Id, ctx);
            const Value b = EvaluatePinValue(in[1].Id, ctx);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) { return std::get<float>(a) * std::get<float>(b); }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) { return std::get<glm::vec2>(a) * std::get<glm::vec2>(b); }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) { return std::get<glm::vec3>(a) * std::get<glm::vec3>(b); }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) { return std::get<glm::vec4>(a) * std::get<glm::vec4>(b); }
            break;
        }
        case NodeSubType::Div:
        {
            const Value a = EvaluatePinValue(in[0].Id, ctx);
            const Value b = EvaluatePinValue(in[1].Id, ctx);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b))
            {
                const float divisor = std::get<float>(b);
                return divisor != 0.0f ? std::get<float>(a) / divisor : 0.0f;
            }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) { return std::get<glm::vec2>(a) / std::get<glm::vec2>(b); }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) { return std::get<glm::vec3>(a) / std::get<glm::vec3>(b); }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) { return std::get<glm::vec4>(a) / std::get<glm::vec4>(b); }
            break;
        }
        case NodeSubType::Min:
        {
            const Value a = EvaluatePinValue(in[0].Id, ctx);
            const Value b = EvaluatePinValue(in[1].Id, ctx);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) { return std::min(std::get<float>(a), std::get<float>(b)); }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) { return glm::min(std::get<glm::vec2>(a), std::get<glm::vec2>(b)); }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) { return glm::min(std::get<glm::vec3>(a), std::get<glm::vec3>(b)); }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) { return glm::min(std::get<glm::vec4>(a), std::get<glm::vec4>(b)); }
            break;
        }
        case NodeSubType::Max:
        {
            const Value a = EvaluatePinValue(in[0].Id, ctx);
            const Value b = EvaluatePinValue(in[1].Id, ctx);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) { return std::max(std::get<float>(a), std::get<float>(b)); }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) { return glm::max(std::get<glm::vec2>(a), std::get<glm::vec2>(b)); }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) { return glm::max(std::get<glm::vec3>(a), std::get<glm::vec3>(b)); }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) { return glm::max(std::get<glm::vec4>(a), std::get<glm::vec4>(b)); }
            break;
        }
        case NodeSubType::Sin:
        {
            const Value val = EvaluatePinValue(in[0].Id, ctx);
            if (std::holds_alternative<float>(val)) { return std::sin(std::get<float>(val)); }
            break;
        }
        case NodeSubType::Cos:
        {
            const Value val = EvaluatePinValue(in[0].Id, ctx);
            if (std::holds_alternative<float>(val)) { return std::cos(std::get<float>(val)); }
            break;
        }
        case NodeSubType::Tan:
        {
            const Value val = EvaluatePinValue(in[0].Id, ctx);
            if (std::holds_alternative<float>(val)) { return std::tan(std::get<float>(val)); }
            break;
        }
        case NodeSubType::Sqrt:
        {
            const Value val = EvaluatePinValue(in[0].Id, ctx);
            if (std::holds_alternative<float>(val)) { return std::sqrt(std::max(0.0f, std::get<float>(val))); }
            if (std::holds_alternative<glm::vec2>(val)) { return glm::sqrt(glm::max(glm::vec2(0.0f), std::get<glm::vec2>(val))); }
            if (std::holds_alternative<glm::vec3>(val)) { return glm::sqrt(glm::max(glm::vec3(0.0f), std::get<glm::vec3>(val))); }
            if (std::holds_alternative<glm::vec4>(val)) { return glm::sqrt(glm::max(glm::vec4(0.0f), std::get<glm::vec4>(val))); }
            break;
        }
        case NodeSubType::Negate:
        {
            const Value val = EvaluatePinValue(in[0].Id, ctx);
            if (std::holds_alternative<float>(val)) { return -std::get<float>(val); }
            if (std::holds_alternative<glm::vec2>(val)) { return -std::get<glm::vec2>(val); }
            if (std::holds_alternative<glm::vec3>(val)) { return -std::get<glm::vec3>(val); }
            if (std::holds_alternative<glm::vec4>(val)) { return -std::get<glm::vec4>(val); }
            break;
        }
        case NodeSubType::Length:
        {
            const Value val = EvaluatePinValue(in[0].Id, ctx);
            if (std::holds_alternative<glm::vec2>(val)) { return glm::length(std::get<glm::vec2>(val)); }
            if (std::holds_alternative<glm::vec3>(val)) { return glm::length(std::get<glm::vec3>(val)); }
            if (std::holds_alternative<glm::vec4>(val)) { return glm::length(std::get<glm::vec4>(val)); }
            break;
        }
        case NodeSubType::Distance:
        {
            const Value a = EvaluatePinValue(in[0].Id, ctx);
            const Value b = EvaluatePinValue(in[1].Id, ctx);
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) { return glm::distance(std::get<glm::vec2>(a), std::get<glm::vec2>(b)); }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) { return glm::distance(std::get<glm::vec3>(a), std::get<glm::vec3>(b)); }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) { return glm::distance(std::get<glm::vec4>(a), std::get<glm::vec4>(b)); }
            break;
        }
        case NodeSubType::Lerp:
        {
            const Value a = EvaluatePinValue(in[0].Id, ctx);
            const Value b = EvaluatePinValue(in[1].Id, ctx);
            const float t = GetValueAs<float>(EvaluatePinValue(in[2].Id, ctx), 0.0f);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) { return glm::mix(std::get<float>(a), std::get<float>(b), t); }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) { return glm::mix(std::get<glm::vec2>(a), std::get<glm::vec2>(b), t); }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) { return glm::mix(std::get<glm::vec3>(a), std::get<glm::vec3>(b), t); }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) { return glm::mix(std::get<glm::vec4>(a), std::get<glm::vec4>(b), t); }
            break;
        }
        case NodeSubType::Clamp:
        {
            const Value val = EvaluatePinValue(in[0].Id, ctx);
            const Value minVal = EvaluatePinValue(in[1].Id, ctx);
            const Value maxVal = EvaluatePinValue(in[2].Id, ctx);
            if (std::holds_alternative<float>(val) && std::holds_alternative<float>(minVal) && std::holds_alternative<float>(maxVal))
            {
                return glm::clamp(std::get<float>(val), std::get<float>(minVal), std::get<float>(maxVal));
            }
            if (std::holds_alternative<glm::vec2>(val) && std::holds_alternative<glm::vec2>(minVal) && std::holds_alternative<glm::vec2>(maxVal))
            {
                return glm::clamp(std::get<glm::vec2>(val), std::get<glm::vec2>(minVal), std::get<glm::vec2>(maxVal));
            }
            if (std::holds_alternative<glm::vec3>(val) && std::holds_alternative<glm::vec3>(minVal) && std::holds_alternative<glm::vec3>(maxVal))
            {
                return glm::clamp(std::get<glm::vec3>(val), std::get<glm::vec3>(minVal), std::get<glm::vec3>(maxVal));
            }
            if (std::holds_alternative<glm::vec4>(val) && std::holds_alternative<glm::vec4>(minVal) && std::holds_alternative<glm::vec4>(maxVal))
            {
                return glm::clamp(std::get<glm::vec4>(val), std::get<glm::vec4>(minVal), std::get<glm::vec4>(maxVal));
            }
            break;
        }
        case NodeSubType::LookAt:
        {
            const Value from = EvaluatePinValue(in[0].Id, ctx);
            const Value target = EvaluatePinValue(in[1].Id, ctx);
            if (std::holds_alternative<glm::vec3>(from) && std::holds_alternative<glm::vec3>(target))
            {
                return LookAtEulerDegrees(std::get<glm::vec3>(from), std::get<glm::vec3>(target));
            }
            break;
        }
        case NodeSubType::And:
        {
            const bool a = GetValueAs<bool>(EvaluatePinValue(in[0].Id, ctx), false);
            const bool b = GetValueAs<bool>(EvaluatePinValue(in[1].Id, ctx), false);
            return a && b;
        }
        case NodeSubType::Or:
        {
            const bool a = GetValueAs<bool>(EvaluatePinValue(in[0].Id, ctx), false);
            const bool b = GetValueAs<bool>(EvaluatePinValue(in[1].Id, ctx), false);
            return a || b;
        }
        default:
            break;
        }
        return std::monostate{};
    }

    void GraphExecutor::ExecuteDecomposer(const Node* node, ExecutionContext& ctx)
    {
        if (node->Inputs.empty()) { return; }

        const Value inputVal = EvaluatePinValue(node->Inputs[0].Id, ctx);
        if (!std::holds_alternative<std::uint64_t>(inputVal)) { return; }

        const std::uint64_t guid = std::get<std::uint64_t>(inputVal);
        const entt::entity entity = ctx.Scene.FindByGuid(guid);
        if (!ctx.Scene.IsValid(entity)) { return; }

        const PropertyCategory* category = FindPropertyCategory(node->Component);
        if (!category) { return; }

        for (std::size_t i = 0; i < node->Outputs.size() && i < category->Properties.size(); ++i)
        {
            m_PinValues[node->Outputs[i].Id] = category->Properties[i].Get(ctx.Scene, entity);
        }
    }

    Value GraphExecutor::ExecuteSceneGetter(const Node* node, ExecutionContext& ctx)
    {
        const std::uint64_t guid = node->TargetGuid != 0 ? node->TargetGuid : m_SelfGuid;
        const entt::entity entity = ctx.Scene.FindByGuid(guid);
        if (ctx.Scene.IsValid(entity)) { return guid; }
        return std::monostate{};
    }

    void GraphExecutor::ExecuteSetter(const Node* node, ExecutionContext& ctx)
    {
        if (node->Inputs.size() < 2) { return; }

        const Value entityVal = EvaluatePinValue(node->Inputs[1].Id, ctx);
        if (!std::holds_alternative<std::uint64_t>(entityVal)) { return; }
        const std::uint64_t guid = std::get<std::uint64_t>(entityVal);

        if (!EntityKnown(ctx, guid)) { return; }

        const PropertyCategory* category = FindPropertyCategory(node->Component);
        if (!category) { return; }

        for (std::size_t i = 2; i < node->Inputs.size(); ++i)
        {
            const std::size_t propertyIndex = i - 2;
            if (propertyIndex >= category->Properties.size()) { break; }

            const Value val = EvaluatePinValue(node->Inputs[i].Id, ctx);
            if (std::holds_alternative<std::monostate>(val))
            {
                continue;
            }

            auto setFn = category->Properties[propertyIndex].Set;
            ctx.Writes.push_back([guid, val, setFn](GPP::Scene& liveScene)
            {
                const entt::entity liveEntity = liveScene.FindByGuid(guid);
                if (liveScene.IsValid(liveEntity)) { setFn(liveScene, liveEntity, val); }
            });
        }

        if (node->Outputs.size() > 1)
        {
            m_PinValues[node->Outputs[1].Id] = guid;
        }
    }

    bool GraphExecutor::EntityKnown(const ExecutionContext& ctx, const std::uint64_t guid)
    {
        return ctx.Spawned.contains(guid) || ctx.Scene.IsValid(ctx.Scene.FindByGuid(guid));
    }

    void GraphExecutor::ExecuteEntityNode(const Node* node, ExecutionContext& ctx)
    {
        const auto asGuid = [](const Value& value) -> std::uint64_t
        {
            return std::holds_alternative<std::uint64_t>(value) ? std::get<std::uint64_t>(value) : 0;
        };
        const auto outputPin = [node](const std::size_t index) { return node->Outputs.size() > index ? node->Outputs[index].Id : 0; };

        if (node->SubType == NodeSubType::SpawnEntity && node->Inputs.size() >= 4)
        {
            const Value presetValue = EvaluatePinValue(node->Inputs[1].Id, ctx);
            const std::string preset = std::holds_alternative<std::string>(presetValue)
                                           ? std::get<std::string>(presetValue)
                                           : std::string("Empty");
            const glm::vec3 position = GetValueAs<glm::vec3>(EvaluatePinValue(node->Inputs[2].Id, ctx), glm::vec3(0.0f));
            const Value nameValue = EvaluatePinValue(node->Inputs[3].Id, ctx);
            const std::string name = std::holds_alternative<std::string>(nameValue) ? std::get<std::string>(nameValue) : "";

            const std::uint64_t guid = m_GuidSource();
            if (FindEntityPreset(preset))
            {
                ctx.Writes.push_back([guid, preset, position, name](GPP::Scene& live)
                {
                    SpawnPreset(live, guid, preset, position, name);
                });
            }
            else
            {
                std::uint64_t sourceGuid = 0;
                for (auto [entity, metadata] : ctx.Scene.Registry().view<const GPP::MetadataComponent>().each())
                {
                    if (metadata.Name == preset) { sourceGuid = metadata.Guid; break; }
                }
                if (sourceGuid == 0) { return; }
                ctx.Writes.push_back([guid, sourceGuid, position, name](GPP::Scene& live)
                {
                    const auto source = live.FindByGuid(sourceGuid);
                    const auto copy = live.CloneEntity(source, guid);
                    if (!live.IsValid(copy)) { return; }
                    if (auto* transform = live.Registry().try_get<GPP::TransformComponent>(copy)) { transform->Position = position; }
                    if (!name.empty()) { live.Registry().get<GPP::MetadataComponent>(copy).Name = name; }
                });
            }
            ctx.Spawned.insert(guid);
            if (const int pin = outputPin(1)) { m_PinValues[pin] = guid; }
        }
        else if (node->SubType == NodeSubType::DestroyEntity && node->Inputs.size() >= 2)
        {
            const std::uint64_t guid = asGuid(EvaluatePinValue(node->Inputs[1].Id, ctx));
            if (guid == 0) { return; }
            ctx.Spawned.erase(guid);
            ctx.Writes.push_back([guid](GPP::Scene& live) { live.DestroyEntity(live.FindByGuid(guid)); });
        }
        else if (node->SubType == NodeSubType::CloneEntity && node->Inputs.size() >= 3)
        {
            const std::uint64_t sourceGuid = asGuid(EvaluatePinValue(node->Inputs[1].Id, ctx));
            if (sourceGuid == 0 || !EntityKnown(ctx, sourceGuid)) { return; }
            const Value positionValue = EvaluatePinValue(node->Inputs[2].Id, ctx);
            const std::optional<glm::vec3> position = std::holds_alternative<glm::vec3>(positionValue)
                                                          ? std::optional(std::get<glm::vec3>(positionValue))
                                                          : std::nullopt;
            const std::uint64_t guid = m_GuidSource();
            ctx.Writes.push_back([guid, sourceGuid, position](GPP::Scene& live)
            {
                const auto copy = live.CloneEntity(live.FindByGuid(sourceGuid), guid);
                if (!live.IsValid(copy) || !position) { return; }
                if (auto* transform = live.Registry().try_get<GPP::TransformComponent>(copy)) { transform->Position = *position; }
            });
            ctx.Spawned.insert(guid);
            if (const int pin = outputPin(1)) { m_PinValues[pin] = guid; }
        }
    }

    void GraphExecutor::ExecuteControlFlow(const Node* node, ExecutionContext& ctx)
    {
        if (node->SubType == NodeSubType::FunctionReturn)
        {
            m_ReturnValues.clear();
            for (const auto& pin : node->Inputs)
            {
                if (pin.Type != PinType::Flow) { m_ReturnValues.push_back(EvaluatePinValue(pin.Id, ctx)); }
            }
            m_Returned = true;
        }
        else if (node->SubType == NodeSubType::Branch)
        {
            if (node->Inputs.size() < 2) { return; }
            const bool condition = GetValueAs<bool>(EvaluatePinValue(node->Inputs[1].Id, ctx), false);
            if (condition && !node->Outputs.empty())
            {
                ExecuteFlowFromPin(node->Outputs[0].Id, ctx);
            }
            else if (!condition && node->Outputs.size() > 1)
            {
                ExecuteFlowFromPin(node->Outputs[1].Id, ctx);
            }
        }
        else if (node->SubType == NodeSubType::For)
        {
            if (node->Inputs.size() < 3) { return; }
            const int start = GetValueAs<int>(EvaluatePinValue(node->Inputs[1].Id, ctx), 0);
            const int end = GetValueAs<int>(EvaluatePinValue(node->Inputs[2].Id, ctx), 0);

            int iterations = 0;
            for (int i = start; i < end && !m_Returned; ++i)
            {
                if (++iterations > kMaxForIterations)
                {
                    if (m_OnPrint) { m_OnPrint("[GraphExecutor] For loop aborted: exceeded max iteration cap"); }
                    break;
                }

                std::erase_if(m_PinValues, [this](const auto& entry)
                {
                    const Node* owner = m_Graph.FindNodeByOutputPin(entry.first);
                    return !owner || owner->Type != NodeType::Event;
                });

                if (node->Outputs.size() > 1) { m_PinValues[node->Outputs[1].Id] = static_cast<float>(i); }
                if (!node->Outputs.empty()) { ExecuteFlowFromPin(node->Outputs[0].Id, ctx); }
            }

            if (node->Outputs.size() > 2) { ExecuteFlowFromPin(node->Outputs[2].Id, ctx); }
        }
    }

    void GraphExecutor::ExecutePrint(const Node* node, ExecutionContext& ctx)
    {
        if (node->Inputs.size() < 2) { return; }
        const Value val = EvaluatePinValue(node->Inputs[1].Id, ctx);
        if (m_OnPrint) { m_OnPrint(ValueToString(val)); }
    }

    std::vector<Value> GraphExecutor::RunFunction(const std::string& name, const FunctionDefinition& definition,
                                                  const std::vector<Value>& args, ExecutionContext& ctx)
    {
        GraphExecutor callee(*definition.Graph, m_OnPrint, m_SelfGuid);
        callee.m_GuidSource = m_GuidSource;
        callee.m_Functions = m_Functions;
        callee.m_CallStack = m_CallStack;
        callee.m_CallStack.push_back(name);

        const Node* entry = nullptr;
        const Node* ret = nullptr;
        for (const auto& node : definition.Graph->Nodes)
        {
            if (!entry && node.SubType == NodeSubType::FunctionEntry) { entry = &node; }
            if (!ret && node.SubType == NodeSubType::FunctionReturn) { ret = &node; }
        }

        std::vector<Value> results;
        for (const auto& param : definition.Signature->Outputs) { results.push_back(DefaultValueFor(param.Type)); }
        if (!entry) { return results; }

        std::size_t argIndex = 0;
        for (const auto& pin : entry->Outputs)
        {
            if (pin.Type == PinType::Flow) { continue; }
            if (argIndex < args.size()) { callee.m_PinValues[pin.Id] = args[argIndex]; }
            ++argIndex;
        }

        if (definition.Signature->Pure)
        {
            if (!ret) { return results; }
            std::size_t index = 0;
            for (const auto& pin : ret->Inputs)
            {
                if (pin.Type == PinType::Flow) { continue; }
                if (index < results.size()) { results[index] = callee.EvaluatePinValue(pin.Id, ctx); }
                ++index;
            }
            return results;
        }

        if (!entry->Outputs.empty() && entry->Outputs[0].Type == PinType::Flow)
        {
            callee.ExecuteFlowFromPin(entry->Outputs[0].Id, ctx);
        }
        for (std::size_t i = 0; i < results.size() && i < callee.m_ReturnValues.size(); ++i)
        {
            if (!std::holds_alternative<std::monostate>(callee.m_ReturnValues[i])) { results[i] = callee.m_ReturnValues[i]; }
        }
        return results;
    }

    void GraphExecutor::ExecuteCall(const Node* node, ExecutionContext& ctx)
    {
        if (!m_Functions) { return; }
        const auto it = m_Functions->find(node->FunctionName);
        if (it == m_Functions->end()) { return; }
        if (static_cast<int>(m_CallStack.size()) >= kMaxCallDepth ||
            std::ranges::contains(m_CallStack, node->FunctionName))
        {
            return;
        }

        std::vector<Value> args;
        for (const auto& pin : node->Inputs)
        {
            if (pin.Type != PinType::Flow) { args.push_back(EvaluatePinValue(pin.Id, ctx)); }
        }
        const auto results = RunFunction(node->FunctionName, it->second, args, ctx);

        std::size_t index = 0;
        for (const auto& pin : node->Outputs)
        {
            if (pin.Type == PinType::Flow) { continue; }
            if (index < results.size()) { m_PinValues[pin.Id] = results[index]; }
            ++index;
        }
    }

    Value GraphExecutor::ExecuteVariableGet(const Node* node)
    {
        if (const auto it = m_Variables.find(node->VariableName); it != m_Variables.end())
        {
            return it->second;
        }
        return std::monostate{};
    }

    void GraphExecutor::ExecuteVariableSet(const Node* node, ExecutionContext& ctx)
    {
        if (node->Inputs.size() < 2) { return; }
        m_Variables[node->VariableName] = EvaluatePinValue(node->Inputs[1].Id, ctx);
    }
}
