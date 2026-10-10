module;
#include <yaml-cpp/yaml.h>
module MoleHole;

import :Simulation.GraphIr;
import :Simulation.AnimationGraph;
import :Simulation.SceneGraphs;
import :Simulation.GraphValidation;
import std;

namespace MoleHole
{
    namespace
    {
        bool HasFlowOutput(const Node& node)
        {
            return !node.Outputs.empty() && node.Outputs[0].Type == PinType::Flow;
        }

        bool IsPureNode(const Node& node)
        {
            switch (node.Type)
            {
            case NodeType::Constant:
            case NodeType::Function:
            case NodeType::Getter:
            case NodeType::Decomposer: return true;
            case NodeType::Variable: return node.SubType == NodeSubType::VariableGet;
            case NodeType::Reroute:
            case NodeType::Call: return !node.Outputs.empty() && !HasFlowOutput(node);
            default: return false;
            }
        }
    }

    bool IrGraph::HasErrors() const
    {
        return std::ranges::any_of(Diagnostics, [](const Diagnostic& d) { return d.Severity == TraceSeverity::Error; });
    }

    std::uint64_t HashGraph(const NamedGraph& graph)
    {
        YAML::Emitter out;
        out << GraphToNode(graph.Graph);
        std::string text = out.c_str();
        text += graph.IsFunction ? "|fn|" : (graph.IsComponent ? "|cmp|" : "|ev|");
        if (graph.IsFunction)
        {
            YAML::Emitter sig;
            sig << SignatureToNode(graph.Signature);
            text += sig.c_str();
        }
        text += "|" + graph.Name;
        return std::hash<std::string>{}(text);
    }

    IrGraph CompileIr(const NamedGraph& named, const SceneGraphs* scene)
    {
        const AnimationGraphData& graph = named.Graph;
        IrGraph ir;
        ir.Name = named.Name;
        ir.IsFunction = named.IsFunction;
        ir.IsComponent = named.IsComponent;
        ir.Signature = named.Signature;
        ir.Variables = graph.Variables;
        ir.Hash = HashGraph(named);
        if (scene) ir.Diagnostics = ValidateGraph(graph, scene, &named);

        std::unordered_map<int, std::pair<int, int>> outputPins;
        std::unordered_map<int, std::pair<int, int>> inputPins;
        ir.Nodes.reserve(graph.Nodes.size());
        for (const auto& node : graph.Nodes)
        {
            const int index = static_cast<int>(ir.Nodes.size());
            IrNode& out = ir.Nodes.emplace_back();
            out.Id = node.Id;
            out.Type = node.Type;
            out.SubType = node.SubType;
            out.Constant = node.ConstantValue;
            out.Variable = node.VariableName;
            out.Component = node.Component;
            out.Function = node.FunctionName;
            out.Label = node.Label;
            out.TargetGuid = node.TargetGuid;
            out.Pure = IsPureNode(node);
            out.Inputs.resize(node.Inputs.size());
            out.Flow.resize(node.Outputs.size());
            for (std::size_t i = 0; i < node.Inputs.size(); ++i)
            {
                out.Inputs[i].Type = node.Inputs[i].Type;
                inputPins.emplace(node.Inputs[i].Id, std::pair{index, static_cast<int>(i)});
            }
            for (std::size_t i = 0; i < node.Outputs.size(); ++i)
            {
                out.OutputPinIds.push_back(node.Outputs[i].Id);
                out.OutputTypes.push_back(node.Outputs[i].Type);
                outputPins.emplace(node.Outputs[i].Id, std::pair{index, static_cast<int>(i)});
            }
            if (node.Type == NodeType::Event)
            {
                if (node.SubType == NodeSubType::Start && !node.Outputs.empty()) ir.StartEvents.push_back(index);
                if (node.SubType == NodeSubType::Tick && node.Outputs.size() > 1) ir.TickEvents.push_back(index);
                if (node.SubType == NodeSubType::FunctionEntry && ir.Entry < 0) ir.Entry = index;
            }
            if (node.SubType == NodeSubType::FunctionReturn && ir.Return < 0) ir.Return = index;
            if (!EventNameOf(node).empty() && !node.Outputs.empty() && node.Type == NodeType::Event) ir.EventHandlers.push_back(index);
            if (IsLatentNode(node)) ir.HasLatent = true;
        }

        std::vector<bool> inputTaken;
        for (const auto& link : graph.Links)
        {
            const auto from = outputPins.find(link.StartPinId);
            const auto to = inputPins.find(link.EndPinId);
            if (from == outputPins.end() || to == inputPins.end()) continue;
            const auto [fromNode, fromPin] = from->second;
            const auto [toNode, toPin] = to->second;
            IrNode& source = ir.Nodes[fromNode];
            if (source.OutputTypes[fromPin] == PinType::Flow)
            {
                source.Flow[fromPin].push_back(IrFlowEdge{link.Id, toNode, toPin, false});
                continue;
            }
            IrInput& input = ir.Nodes[toNode].Inputs[toPin];
            if (input.Source >= 0) continue;
            input.Source = fromNode;
            input.SourcePin = fromPin;
            input.Type = source.OutputTypes[fromPin];
            ++source.DataConsumers;
        }

        // Cut data cycles among pull nodes, producing a topological order of the pure nodes.
        std::vector<int> state(ir.Nodes.size(), 0);
        const auto visit = [&](auto&& self, const int index) -> void
        {
            state[index] = 1;
            for (auto& input : ir.Nodes[index].Inputs)
            {
                if (input.Source < 0 || !ir.Nodes[input.Source].Pure) continue;
                if (state[input.Source] == 1)
                {
                    --ir.Nodes[input.Source].DataConsumers;
                    ir.Diagnostics.push_back({TraceSeverity::Error, DiagnosticCode::DataCycle, ir.Nodes[input.Source].Id, 0, 0,
                                              "Data cycle: this value depends on itself"});
                    input.Source = -1;
                    continue;
                }
                if (state[input.Source] == 0) self(self, input.Source);
            }
            state[index] = 2;
            if (ir.Nodes[index].Pure) ir.PureOrder.push_back(index);
        };
        for (std::size_t i = 0; i < ir.Nodes.size(); ++i)
        {
            if (state[i] == 0) visit(visit, static_cast<int>(i));
        }

        for (const int index : ir.PureOrder)
        {
            IrNode& node = ir.Nodes[index];
            bool stable = node.Type != NodeType::Variable && node.Type != NodeType::Call;
            for (const auto& input : node.Inputs)
            {
                if (input.Source < 0) continue;
                const IrNode& source = ir.Nodes[input.Source];
                if (source.Type == NodeType::Event) continue;
                if (!source.Pure || !source.Stable) stable = false;
            }
            node.Stable = stable;
        }

        // Cut flow cycles reachable from the entry points.
        std::fill(state.begin(), state.end(), 0);
        const auto flowVisit = [&](auto&& self, const int index) -> void
        {
            state[index] = 1;
            for (auto& edges : ir.Nodes[index].Flow)
            {
                for (auto& edge : edges)
                {
                    if (state[edge.Target] == 1)
                    {
                        edge.Cut = true;
                        ir.Diagnostics.push_back({TraceSeverity::Error, DiagnosticCode::FlowCycle, ir.Nodes[index].Id, 0,
                                                  edge.LinkId, "Flow loop: execution would never finish"});
                    }
                    else if (state[edge.Target] == 0) self(self, edge.Target);
                }
            }
            state[index] = 2;
        };
        for (const auto& roots : {ir.StartEvents, ir.TickEvents, ir.EventHandlers})
        {
            for (const int root : roots) if (state[root] == 0) flowVisit(flowVisit, root);
        }
        if (ir.Entry >= 0 && state[ir.Entry] == 0) flowVisit(flowVisit, ir.Entry);
        return ir;
    }

    std::shared_ptr<const IrGraph> IrCache::Get(const NamedGraph& graph, const SceneGraphs* scene)
    {
        const std::uint64_t hash = HashGraph(graph);
        auto& slot = m_Entries[graph.Name];
        if (!slot || slot->Hash != hash)
        {
            slot = std::make_shared<const IrGraph>(CompileIr(graph, scene));
            ++m_Compilations;
        }
        return slot;
    }
}
