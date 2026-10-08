module MoleHole;

import :Simulation.AnimationGraphExecutor;
import :Simulation.AnimationGraph;
import :Simulation.AnimationGraphProperties;
import std;
import glm;
import GPP;

namespace MoleHole
{
    namespace
    {
        constexpr int kMaxForIterations = 100000;
    }

    GraphExecutor::GraphExecutor(const AnimationGraphData& graph, std::function<void(std::string)> onPrint)
        : m_Graph(graph), m_OnPrint(std::move(onPrint))
    {
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
        for (const auto& link : m_Graph.Links)
        {
            if (link.StartPinId == pinId)
            {
                if (const Node* target = m_Graph.FindNodeByInputPin(link.EndPinId))
                {
                    ExecuteNode(target, ctx);
                }
            }
        }
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

        const PropertyCategory* category = FindPropertyCategory(node->SubType);
        if (!category) { return; }

        for (std::size_t i = 0; i < node->Outputs.size() && i < category->Properties.size(); ++i)
        {
            m_PinValues[node->Outputs[i].Id] = category->Properties[i].Get(ctx.Scene, entity);
        }
    }

    Value GraphExecutor::ExecuteSceneGetter(const Node* node, ExecutionContext& ctx)
    {
        const entt::entity entity = ctx.Scene.FindByGuid(node->TargetGuid);
        if (ctx.Scene.IsValid(entity)) { return node->TargetGuid; }
        return std::monostate{};
    }

    void GraphExecutor::ExecuteSetter(const Node* node, ExecutionContext& ctx)
    {
        if (node->Inputs.size() < 2) { return; }

        const Value entityVal = EvaluatePinValue(node->Inputs[1].Id, ctx);
        if (!std::holds_alternative<std::uint64_t>(entityVal)) { return; }
        const std::uint64_t guid = std::get<std::uint64_t>(entityVal);

        const entt::entity entity = ctx.Scene.FindByGuid(guid);
        if (!ctx.Scene.IsValid(entity)) { return; }

        const PropertyCategory* category = FindPropertyCategory(node->SubType);
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

    void GraphExecutor::ExecuteControlFlow(const Node* node, ExecutionContext& ctx)
    {
        if (node->SubType == NodeSubType::Branch)
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
            for (int i = start; i < end; ++i)
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
