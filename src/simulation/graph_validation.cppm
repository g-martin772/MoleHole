export module MoleHole:Simulation.GraphValidation;

import std;
import :Simulation.AnimationGraph;
import :Simulation.SceneGraphs;
import :Simulation.GraphEdit;
import :Simulation.GraphFunctions;
import :Simulation.GraphTrace;

export namespace MoleHole
{
    enum class DiagnosticCode
    {
        BrokenLink, TypeMismatch, UnconnectedInput, NeverRuns, DataCycle, FlowCycle, DanglingCall, StaleCall,
        RecursiveCall, UnassignedVariable, UnknownVariable, MissingReturn, MisplacedFunctionNode,
    };

    struct Diagnostic
    {
        TraceSeverity Severity{TraceSeverity::Warning};
        DiagnosticCode Code{DiagnosticCode::BrokenLink};
        int NodeId{0};
        int PinId{0};
        int LinkId{0};
        std::string Message;
    };

    namespace ValidationDetail
    {
        inline bool HasFlowPin(const Node& node)
        {
            return std::ranges::any_of(node.Inputs, [](const Pin& p) { return p.Type == PinType::Flow; }) ||
                   std::ranges::any_of(node.Outputs, [](const Pin& p) { return p.Type == PinType::Flow; });
        }

        // Nodes whose outputs are computed by pulling their inputs, so a cycle among them recurses forever.
        inline bool IsPullNode(const Node& node)
        {
            switch (node.Type)
            {
            case NodeType::Function:
            case NodeType::Reroute:
            case NodeType::Decomposer: return node.Type != NodeType::Reroute || !HasFlowPin(node);
            case NodeType::Call: return !HasFlowPin(node);
            default: return false;
            }
        }

        inline TraceSeverity RequiredSeverity(const Node& node, const std::size_t inputIndex)
        {
            switch (node.Type)
            {
            case NodeType::Function: return TraceSeverity::Error;
            case NodeType::Decomposer: return TraceSeverity::Error;
            case NodeType::Setter: return inputIndex == 1 ? TraceSeverity::Error : TraceSeverity::Info;
            case NodeType::Entity:
                return node.SubType == NodeSubType::SpawnEntity ? TraceSeverity::Info : TraceSeverity::Error;
            case NodeType::Control:
            case NodeType::Print:
            case NodeType::Variable:
            case NodeType::Call:
            case NodeType::Reroute: return TraceSeverity::Warning;
            default: return TraceSeverity::Info;
            }
        }

        // Marks every node taking part in a cycle of the directed graph.
        inline std::set<int> NodesInCycles(const std::map<int, std::vector<int>>& edges)
        {
            std::set<int> inCycle;
            std::map<int, int> state;
            std::vector<int> stack;
            const auto visit = [&](auto&& self, const int node) -> void
            {
                state[node] = 1;
                stack.push_back(node);
                if (const auto it = edges.find(node); it != edges.end())
                {
                    for (const int next : it->second)
                    {
                        if (state[next] == 1)
                        {
                            for (auto at = std::ranges::find(stack, next); at != stack.end(); ++at) inCycle.insert(*at);
                        }
                        else if (state[next] == 0) self(self, next);
                    }
                }
                stack.pop_back();
                state[node] = 2;
            };
            for (const auto& [node, _] : edges)
            {
                if (state[node] == 0) visit(visit, node);
            }
            return inCycle;
        }

        inline bool Reaches(const std::map<std::string, std::set<std::string>>& calls, const std::string& from,
                            const std::string& goal)
        {
            std::set<std::string> seen;
            std::vector<std::string> pending{from};
            while (!pending.empty())
            {
                const std::string current = std::move(pending.back());
                pending.pop_back();
                if (!seen.insert(current).second) continue;
                if (current == goal) return true;
                if (const auto it = calls.find(current); it != calls.end())
                {
                    pending.insert(pending.end(), it->second.begin(), it->second.end());
                }
            }
            return false;
        }
    }

    // Static checks that need no scene: link types, required pins, cycles, variables, function calls and function shape.
    [[nodiscard]] inline std::vector<Diagnostic> ValidateGraph(const AnimationGraphData& graph, const SceneGraphs* scene = nullptr,
                                                               const NamedGraph* self = nullptr)
    {
        using namespace ValidationDetail;
        std::vector<Diagnostic> out;
        const auto add = [&out](const TraceSeverity severity, const DiagnosticCode code, const int node, const int pin,
                                const int link, std::string message)
        {
            out.push_back({severity, code, node, pin, link, std::move(message)});
        };

        std::set<int> linkedPins;
        for (const auto& link : graph.Links)
        {
            const Node* from = graph.FindNodeByOutputPin(link.StartPinId);
            const Node* to = graph.FindNodeByInputPin(link.EndPinId);
            if (!from || !to)
            {
                add(TraceSeverity::Error, DiagnosticCode::BrokenLink, to ? to->Id : (from ? from->Id : 0), link.EndPinId,
                    link.Id, "Link connects to a pin that no longer exists");
                continue;
            }
            linkedPins.insert(link.StartPinId);
            linkedPins.insert(link.EndPinId);
            const Pin* outPin = FindPin(graph, link.StartPinId);
            const Pin* inPin = FindPin(graph, link.EndPinId);
            if (!PinsLinkable(*from, *outPin, *to, *inPin))
            {
                add(TraceSeverity::Error, DiagnosticCode::TypeMismatch, to->Id, inPin->Id, link.Id,
                    std::string("Cannot connect ") + PinTypeToText(outPin->Type) + " to " + PinTypeToText(inPin->Type));
            }
        }

        for (const auto& node : graph.Nodes)
        {
            const bool inUse = std::ranges::any_of(node.Inputs, [&](const Pin& p) { return linkedPins.contains(p.Id); }) ||
                               std::ranges::any_of(node.Outputs, [&](const Pin& p) { return linkedPins.contains(p.Id); });
            if (inUse)
            {
                for (std::size_t i = 0; i < node.Inputs.size(); ++i)
                {
                    const Pin& pin = node.Inputs[i];
                    if (linkedPins.contains(pin.Id)) continue;
                    if (pin.Type == PinType::Flow)
                    {
                        if (node.Type != NodeType::Event)
                        {
                            add(TraceSeverity::Info, DiagnosticCode::NeverRuns, node.Id, pin.Id, 0,
                                "Never runs: its flow input is not connected");
                        }
                        continue;
                    }
                    const auto severity = RequiredSeverity(node, i);
                    if (severity == TraceSeverity::Info) continue;
                    const std::string label = pin.Name.empty() ? "Input" : "Input '" + pin.Name + "'";
                    add(severity, DiagnosticCode::UnconnectedInput, node.Id, pin.Id, 0, label + " is not connected");
                }
            }

            if (node.Type == NodeType::Variable)
            {
                if (node.VariableName.empty())
                {
                    add(TraceSeverity::Error, DiagnosticCode::UnassignedVariable, node.Id, 0, 0, "No variable assigned");
                }
                else if (!std::ranges::contains(graph.Variables, node.VariableName, &Variable::Name))
                {
                    add(TraceSeverity::Error, DiagnosticCode::UnknownVariable, node.Id, 0, 0,
                        "Variable '" + node.VariableName + "' does not exist");
                }
            }

            const bool functionNode = node.SubType == NodeSubType::FunctionEntry || node.SubType == NodeSubType::FunctionReturn;
            if (functionNode && self && !self->IsFunction)
            {
                add(TraceSeverity::Warning, DiagnosticCode::MisplacedFunctionNode, node.Id, 0, 0,
                    "Function input/output nodes only work inside a function graph");
            }
        }

        std::map<int, std::vector<int>> dataEdges;
        std::map<int, std::vector<int>> flowEdges;
        for (const auto& link : graph.Links)
        {
            const Node* from = graph.FindNodeByOutputPin(link.StartPinId);
            const Node* to = graph.FindNodeByInputPin(link.EndPinId);
            const Pin* outPin = FindPin(graph, link.StartPinId);
            if (!from || !to || !outPin) continue;
            if (outPin->Type == PinType::Flow) flowEdges[from->Id].push_back(to->Id);
            else if (IsPullNode(*from) && IsPullNode(*to)) dataEdges[from->Id].push_back(to->Id);
        }
        for (const int id : NodesInCycles(dataEdges))
        {
            add(TraceSeverity::Error, DiagnosticCode::DataCycle, id, 0, 0, "Data cycle: this value depends on itself");
        }
        for (const int id : NodesInCycles(flowEdges))
        {
            add(TraceSeverity::Error, DiagnosticCode::FlowCycle, id, 0, 0, "Flow loop: execution would never finish");
        }

        std::map<std::string, std::set<std::string>> callGraph;
        if (scene)
        {
            for (const auto& item : scene->Items)
            {
                for (const auto& node : item.Graph.Nodes)
                {
                    if (node.SubType == NodeSubType::FunctionCall) callGraph[item.Name].insert(node.FunctionName);
                }
            }
        }
        for (const auto& node : graph.Nodes)
        {
            if (node.SubType != NodeSubType::FunctionCall) continue;
            const NamedGraph* target = nullptr;
            if (scene)
            {
                const auto it = std::ranges::find_if(scene->Items, [&](const NamedGraph& g)
                {
                    return g.IsFunction && g.Name == node.FunctionName;
                });
                if (it != scene->Items.end()) target = &*it;
            }
            if (!target)
            {
                add(TraceSeverity::Error, DiagnosticCode::DanglingCall, node.Id, 0, 0,
                    "Function '" + node.FunctionName + "' does not exist");
                continue;
            }
            if (!CallMatchesSignature(node, target->Signature))
            {
                add(TraceSeverity::Warning, DiagnosticCode::StaleCall, node.Id, 0, 0,
                    "Pins no longer match the function's signature");
            }
            if (self && Reaches(callGraph, node.FunctionName, self->Name))
            {
                add(TraceSeverity::Warning, DiagnosticCode::RecursiveCall, node.Id, 0, 0,
                    "Recursive call: the runtime will skip it when it loops back");
            }
        }

        if (self && self->IsFunction)
        {
            const bool hasReturn = std::ranges::contains(graph.Nodes, NodeSubType::FunctionReturn, &Node::SubType);
            if (!hasReturn && (!self->Signature.Pure || !self->Signature.Outputs.empty()))
            {
                add(TraceSeverity::Warning, DiagnosticCode::MissingReturn, 0, 0, 0,
                    "The function has no Output node, so it never returns values");
            }
        }
        return out;
    }

    [[nodiscard]] inline std::map<std::string, std::vector<Diagnostic>> ValidateScene(const SceneGraphs& graphs)
    {
        std::map<std::string, std::vector<Diagnostic>> result;
        for (const auto& item : graphs.Items) result[item.Name] = ValidateGraph(item.Graph, &graphs, &item);
        return result;
    }

    [[nodiscard]] inline std::size_t CountSeverity(const std::vector<Diagnostic>& list, const TraceSeverity severity)
    {
        return static_cast<std::size_t>(std::ranges::count(list, severity, &Diagnostic::Severity));
    }
}
