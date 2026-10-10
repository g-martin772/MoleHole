export module MoleHole:Simulation.GraphFunctions;

import std;
import glm;
import :Simulation.AnimationGraph;
import :Simulation.SceneGraphs;
import :Simulation.GraphEdit;
import :Simulation.NodeRegistry;

export namespace MoleHole
{
    constexpr const char* kFunctionsCategory = "Functions";
    constexpr int kMaxFunctionParams = 400;
    constexpr int kFunctionOutputPinBase = 500;

    struct FunctionDefinition
    {
        const AnimationGraphData* Graph{nullptr};
        const FunctionSignature* Signature{nullptr};
    };

    using FunctionLibrary = std::unordered_map<std::string, FunctionDefinition>;

    [[nodiscard]] inline FunctionLibrary BuildFunctionLibrary(const SceneGraphs& graphs)
    {
        FunctionLibrary library;
        for (const auto& item : graphs.Items)
        {
            if (item.IsFunction) library[item.Name] = FunctionDefinition{&item.Graph, &item.Signature};
        }
        return library;
    }

    enum class FunctionNodeRole { Entry, Return, Call };

    // Pin index 0 is the flow pin; parameter pins use Key + 1 so ids survive reordering and renames.
    [[nodiscard]] inline std::pair<std::vector<Pin>, std::vector<Pin>> FunctionPins(
        const int nodeId, const FunctionSignature& signature, const FunctionNodeRole role)
    {
        std::vector<Pin> inputs;
        std::vector<Pin> outputs;
        const auto inputId = [nodeId](const int index) { return nodeId * kPinIdStride + 1 + index; };
        const auto outputId = [nodeId](const int index) { return nodeId * kPinIdStride + kFunctionOutputPinBase + index; };

        const bool takesParams = role == FunctionNodeRole::Call;
        const auto& produced = role == FunctionNodeRole::Entry ? signature.Inputs : signature.Outputs;
        if (!signature.Pure)
        {
            if (role != FunctionNodeRole::Entry) inputs.push_back(Pin{inputId(0), role == FunctionNodeRole::Return ? "Return" : "In", PinType::Flow, true});
            if (role != FunctionNodeRole::Return) outputs.push_back(Pin{outputId(0), role == FunctionNodeRole::Entry ? "Start" : "Out", PinType::Flow, false});
        }
        if (takesParams)
        {
            for (const auto& p : signature.Inputs) inputs.push_back(Pin{inputId(p.Key + 1), p.Name, p.Type, true});
            for (const auto& p : signature.Outputs) outputs.push_back(Pin{outputId(p.Key + 1), p.Name, p.Type, false});
        }
        else if (role == FunctionNodeRole::Entry)
        {
            for (const auto& p : produced) outputs.push_back(Pin{outputId(p.Key + 1), p.Name, p.Type, false});
        }
        else
        {
            for (const auto& p : produced) inputs.push_back(Pin{inputId(p.Key + 1), p.Name, p.Type, true});
        }
        return {std::move(inputs), std::move(outputs)};
    }

    [[nodiscard]] inline Node CreateFunctionEntryNode(const int id, const FunctionSignature& signature)
    {
        Node node;
        node.Id = id;
        node.Name = "Function Input";
        node.Type = NodeType::Event;
        node.SubType = NodeSubType::FunctionEntry;
        std::tie(node.Inputs, node.Outputs) = FunctionPins(id, signature, FunctionNodeRole::Entry);
        return node;
    }

    [[nodiscard]] inline Node CreateFunctionReturnNode(const int id, const FunctionSignature& signature)
    {
        Node node;
        node.Id = id;
        node.Name = "Function Output";
        node.Type = NodeType::Control;
        node.SubType = NodeSubType::FunctionReturn;
        std::tie(node.Inputs, node.Outputs) = FunctionPins(id, signature, FunctionNodeRole::Return);
        return node;
    }

    [[nodiscard]] inline Node CreateFunctionCallNode(const int id, const std::string& function, const FunctionSignature& signature)
    {
        Node node;
        node.Id = id;
        node.Name = "Call " + function;
        node.Type = NodeType::Call;
        node.SubType = NodeSubType::FunctionCall;
        node.FunctionName = function;
        std::tie(node.Inputs, node.Outputs) = FunctionPins(id, signature, FunctionNodeRole::Call);
        return node;
    }

    // Replaces a node's pins and drops every link that no longer fits.
    inline void ReplaceNodePins(AnimationGraphData& graph, Node& node, std::vector<Pin> inputs, std::vector<Pin> outputs)
    {
        node.Inputs = std::move(inputs);
        node.Outputs = std::move(outputs);
        std::erase_if(graph.Links, [&](const Link& link)
        {
            const Node* from = graph.FindNodeByOutputPin(link.StartPinId);
            const Node* to = graph.FindNodeByInputPin(link.EndPinId);
            if (from != &node && to != &node) return false;
            if (!from || !to) return true;
            const Pin* out = FindPin(graph, link.StartPinId);
            const Pin* in = FindPin(graph, link.EndPinId);
            return !out || !in || !PinsLinkable(*from, *out, *to, *in);
        });
    }

    // Brings the function's entry/return nodes and every call node in the scene in line with its signature.
    inline void SyncFunction(SceneGraphs& graphs, const std::string& name)
    {
        const NamedGraph* definition = graphs.Find(name);
        if (!definition || !definition->IsFunction) return;
        const FunctionSignature signature = definition->Signature;

        for (auto& item : graphs.Items)
        {
            for (auto& node : item.Graph.Nodes)
            {
                std::optional<FunctionNodeRole> role;
                if (&item == definition && node.SubType == NodeSubType::FunctionEntry) role = FunctionNodeRole::Entry;
                else if (&item == definition && node.SubType == NodeSubType::FunctionReturn) role = FunctionNodeRole::Return;
                else if (node.SubType == NodeSubType::FunctionCall && node.FunctionName == name) role = FunctionNodeRole::Call;
                if (!role) continue;
                auto [inputs, outputs] = FunctionPins(node.Id, signature, *role);
                ReplaceNodePins(item.Graph, node, std::move(inputs), std::move(outputs));
            }
        }
    }

    [[nodiscard]] inline bool CallMatchesSignature(const Node& call, const FunctionSignature& signature)
    {
        const auto [inputs, outputs] = FunctionPins(call.Id, signature, FunctionNodeRole::Call);
        const auto same = [](const std::vector<Pin>& a, const std::vector<Pin>& b)
        {
            return std::ranges::equal(a, b, [](const Pin& x, const Pin& y) { return x.Id == y.Id && x.Type == y.Type && x.Name == y.Name; });
        };
        return same(call.Inputs, inputs) && same(call.Outputs, outputs);
    }

    inline std::size_t AddFunctionGraph(SceneGraphs& graphs, const std::string& baseName = "Function")
    {
        NamedGraph named;
        named.Name = graphs.UniqueName(baseName);
        named.IsFunction = true;
        named.Graph.Nodes.push_back(CreateFunctionEntryNode(named.Graph.AllocateId(), named.Signature));
        named.Graph.Nodes.back().Position = glm::vec2(0.0f, 0.0f);
        named.Graph.Nodes.push_back(CreateFunctionReturnNode(named.Graph.AllocateId(), named.Signature));
        named.Graph.Nodes.back().Position = glm::vec2(420.0f, 0.0f);
        TryLink(named.Graph, named.Graph.Nodes[0].Outputs[0].Id, named.Graph.Nodes[1].Inputs[0].Id);
        graphs.Items.push_back(std::move(named));
        return graphs.Items.size() - 1;
    }

    inline bool RenameFunction(SceneGraphs& graphs, const std::string& from, const std::string& to)
    {
        if (to.empty() || from == to || graphs.Find(to)) return false;
        NamedGraph* target = graphs.Find(from);
        if (!target || !target->IsFunction) return false;
        target->Name = to;
        for (auto& item : graphs.Items)
        {
            for (auto& node : item.Graph.Nodes)
            {
                if (node.SubType != NodeSubType::FunctionCall || node.FunctionName != from) continue;
                if (node.Name == "Call " + from) node.Name = "Call " + to;
                node.FunctionName = to;
            }
        }
        return true;
    }

    [[nodiscard]] inline std::string UniqueParamName(const std::vector<FunctionParam>& params, const std::string& base)
    {
        const auto taken = [&](const std::string& n) { return std::ranges::contains(params, n, &FunctionParam::Name); };
        if (!taken(base)) return base;
        for (int i = 2;; ++i)
        {
            auto candidate = base + std::to_string(i);
            if (!taken(candidate)) return candidate;
        }
    }

    inline FunctionParam* AddParam(FunctionSignature& signature, const bool output, const std::string& name, const PinType type)
    {
        if (type == PinType::Flow || signature.NextKey >= kMaxFunctionParams) return nullptr;
        auto& list = output ? signature.Outputs : signature.Inputs;
        list.push_back(FunctionParam{signature.NextKey++, UniqueParamName(list, name), type});
        return &list.back();
    }

    inline bool RemoveParam(FunctionSignature& signature, const bool output, const int key)
    {
        auto& list = output ? signature.Outputs : signature.Inputs;
        return std::erase_if(list, [key](const FunctionParam& p) { return p.Key == key; }) > 0;
    }

    inline bool RenameParam(FunctionSignature& signature, const bool output, const int key, const std::string& name)
    {
        auto& list = output ? signature.Outputs : signature.Inputs;
        const auto it = std::ranges::find(list, key, &FunctionParam::Key);
        if (it == list.end() || name.empty() || it->Name == name || std::ranges::contains(list, name, &FunctionParam::Name)) return false;
        it->Name = name;
        return true;
    }

    inline bool RetypeParam(FunctionSignature& signature, const bool output, const int key, const PinType type)
    {
        auto& list = output ? signature.Outputs : signature.Inputs;
        const auto it = std::ranges::find(list, key, &FunctionParam::Key);
        if (it == list.end() || type == PinType::Flow || it->Type == type) return false;
        it->Type = type;
        return true;
    }

    // Callable functions for the palette; `current` (when it is a function) also offers extra Return nodes.
    [[nodiscard]] inline std::vector<NodeEntry> FunctionNodeEntries(const SceneGraphs& graphs, const NamedGraph* current = nullptr)
    {
        std::vector<NodeEntry> entries;
        for (const auto& item : graphs.Items)
        {
            if (!item.IsFunction) continue;
            entries.push_back(SingleNodeEntry("Call " + item.Name, kFunctionsCategory,
                                              item.Signature.Pure ? "Calls a pure function; evaluated on demand."
                                                                  : "Runs a function and continues when it returns.",
                                              {"function", "call", "subgraph", item.Name},
                                              [name = item.Name, signature = item.Signature](const int id)
                                              {
                                                  return CreateFunctionCallNode(id, name, signature);
                                              }));
        }
        if (current && current->IsFunction)
        {
            entries.push_back(SingleNodeEntry("Return", kFunctionsCategory, "Ends the function and hands back its outputs.",
                                              {"function", "return", "output", "exit"},
                                              [signature = current->Signature](const int id)
                                              {
                                                  return CreateFunctionReturnNode(id, signature);
                                              }));
        }
        return entries;
    }
}
