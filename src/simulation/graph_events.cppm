export module MoleHole:Simulation.GraphEvents;

import std;
import GPP;
import :Simulation.AnimationGraph;
import :Simulation.GraphEdit;
import :Simulation.GraphFunctions;
import :Simulation.NodeRegistry;

export namespace MoleHole
{
    constexpr int kMaxEventParams = 16;
    constexpr const char* kEventsCategory = "Events";

    struct KeyName
    {
        GPP::KeyCode Key;
        const char* Name;
    };

    [[nodiscard]] inline const std::vector<KeyName>& SupportedKeys()
    {
        static const std::vector<KeyName> keys = []
        {
            std::vector<KeyName> list;
            static constexpr const char* letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
            static constexpr const char* digits[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
            static const std::array<std::string, 26> letterNames = []
            {
                std::array<std::string, 26> names;
                for (int i = 0; i < 26; ++i) names[static_cast<std::size_t>(i)] = std::string(1, letters[i]);
                return names;
            }();
            for (int i = 0; i < 26; ++i)
            {
                list.push_back({static_cast<GPP::KeyCode>(0x61 + i), letterNames[static_cast<std::size_t>(i)].c_str()});
            }
            for (int i = 0; i < 10; ++i) list.push_back({static_cast<GPP::KeyCode>(0x30 + i), digits[i]});
            list.push_back({GPP::KeyCode::Space, "Space"});
            list.push_back({GPP::KeyCode::Return, "Enter"});
            list.push_back({GPP::KeyCode::Escape, "Escape"});
            list.push_back({GPP::KeyCode::Tab, "Tab"});
            list.push_back({GPP::KeyCode::Left, "Left"});
            list.push_back({GPP::KeyCode::Right, "Right"});
            list.push_back({GPP::KeyCode::Up, "Up"});
            list.push_back({GPP::KeyCode::Down, "Down"});
            return list;
        }();
        return keys;
    }

    [[nodiscard]] inline std::optional<std::string> KeyEventName(const GPP::KeyCode key)
    {
        const auto& keys = SupportedKeys();
        const auto it = std::ranges::find(keys, key, &KeyName::Key);
        if (it == keys.end()) return std::nullopt;
        return std::string(it->Name);
    }

    [[nodiscard]] inline std::vector<EventParam> EventParams(const Node& node)
    {
        std::vector<EventParam> params;
        const bool define = node.SubType == NodeSubType::CustomEvent;
        const auto& pins = define ? node.Outputs : node.Inputs;
        const int base = node.Id * kPinIdStride + (define ? kFunctionOutputPinBase : 1);
        for (std::size_t i = 1; i < pins.size(); ++i)
        {
            if (pins[i].Type == PinType::Flow) continue;
            params.push_back(EventParam{pins[i].Id - base - 1, pins[i].Name, pins[i].Type});
        }
        return params;
    }

    [[nodiscard]] inline Node* FindEventDefinition(AnimationGraphData& graph, const std::string& name)
    {
        for (auto& node : graph.Nodes)
        {
            if (node.SubType == NodeSubType::CustomEvent && node.Label == name) return &node;
        }
        return nullptr;
    }

    [[nodiscard]] inline const Node* FindEventDefinition(const AnimationGraphData& graph, const std::string& name)
    {
        return FindEventDefinition(const_cast<AnimationGraphData&>(graph), name);
    }

    [[nodiscard]] inline std::vector<std::string> CustomEventNames(const AnimationGraphData& graph)
    {
        std::vector<std::string> names;
        for (const auto& node : graph.Nodes)
        {
            if (node.SubType == NodeSubType::CustomEvent && !node.Label.empty() && !std::ranges::contains(names, node.Label))
            {
                names.push_back(node.Label);
            }
        }
        return names;
    }

    // Rebuilds every call node of the event so its pins match the definition; mismatching links are dropped.
    inline void SyncCustomEvent(AnimationGraphData& graph, const std::string& name)
    {
        const Node* definition = FindEventDefinition(graph, name);
        if (!definition) return;
        const auto params = EventParams(*definition);
        for (auto& node : graph.Nodes)
        {
            if (node.SubType != NodeSubType::CustomEventCall || node.Label != name) continue;
            const Node fresh = CreateCallEventNode(node.Id, name, params);
            ReplaceNodePins(graph, node, fresh.Inputs, fresh.Outputs);
        }
    }

    [[nodiscard]] inline bool CallMatchesEvent(const Node& call, const Node& definition)
    {
        const Node fresh = CreateCallEventNode(call.Id, definition.Label, EventParams(definition));
        return std::ranges::equal(call.Inputs, fresh.Inputs, [](const Pin& a, const Pin& b)
        {
            return a.Id == b.Id && a.Type == b.Type && a.Name == b.Name;
        });
    }

    inline bool AddEventParam(AnimationGraphData& graph, const int definitionId, const std::string& name, const PinType type)
    {
        Node* node = graph.FindNode(definitionId);
        if (!node || node->SubType != NodeSubType::CustomEvent || type == PinType::Flow) return false;
        const auto params = EventParams(*node);
        if (static_cast<int>(params.size()) >= kMaxEventParams) return false;
        int key = 0;
        for (const auto& param : params) key = std::max(key, param.Key + 1);
        std::string unique = name.empty() ? "Param" : name;
        for (int i = 2; std::ranges::contains(params, unique, &EventParam::Name); ++i) unique = name + std::to_string(i);
        node->Outputs.push_back(Pin{node->Id * kPinIdStride + kFunctionOutputPinBase + key + 1, unique, type, false});
        SyncCustomEvent(graph, node->Label);
        return true;
    }

    inline bool RemoveEventParam(AnimationGraphData& graph, const int definitionId, const int key)
    {
        Node* node = graph.FindNode(definitionId);
        if (!node || node->SubType != NodeSubType::CustomEvent) return false;
        const int pinId = node->Id * kPinIdStride + kFunctionOutputPinBase + key + 1;
        if (std::erase_if(node->Outputs, [pinId](const Pin& p) { return p.Id == pinId; }) == 0) return false;
        std::erase_if(graph.Links, [pinId](const Link& l) { return l.StartPinId == pinId; });
        SyncCustomEvent(graph, node->Label);
        return true;
    }

    inline bool RetypeEventParam(AnimationGraphData& graph, const int definitionId, const int key, const PinType type)
    {
        Node* node = graph.FindNode(definitionId);
        if (!node || node->SubType != NodeSubType::CustomEvent || type == PinType::Flow) return false;
        const int pinId = node->Id * kPinIdStride + kFunctionOutputPinBase + key + 1;
        const auto it = std::ranges::find(node->Outputs, pinId, &Pin::Id);
        if (it == node->Outputs.end() || it->Type == type) return false;
        it->Type = type;
        const std::string name = node->Label;
        std::erase_if(graph.Links, [&](const Link& l)
        {
            if (l.StartPinId != pinId) return false;
            const Node* to = graph.FindNodeByInputPin(l.EndPinId);
            const Pin* in = FindPin(graph, l.EndPinId);
            return !to || !in || !PinsLinkable(*graph.FindNode(definitionId), *it, *to, *in);
        });
        SyncCustomEvent(graph, name);
        return true;
    }

    inline bool RenameEventParam(AnimationGraphData& graph, const int definitionId, const int key, const std::string& name)
    {
        Node* node = graph.FindNode(definitionId);
        if (!node || node->SubType != NodeSubType::CustomEvent || name.empty()) return false;
        if (std::ranges::contains(EventParams(*node), name, &EventParam::Name)) return false;
        const int pinId = node->Id * kPinIdStride + kFunctionOutputPinBase + key + 1;
        const auto it = std::ranges::find(node->Outputs, pinId, &Pin::Id);
        if (it == node->Outputs.end()) return false;
        it->Name = name;
        SyncCustomEvent(graph, node->Label);
        return true;
    }

    // Renames the event on its definition(s) and on every call and wait node that uses it.
    inline bool RenameCustomEvent(AnimationGraphData& graph, const std::string& from, const std::string& to)
    {
        if (to.empty() || from == to || FindEventDefinition(graph, to)) return false;
        if (!FindEventDefinition(graph, from)) return false;
        for (auto& node : graph.Nodes)
        {
            const bool uses = node.SubType == NodeSubType::CustomEvent || node.SubType == NodeSubType::CustomEventCall ||
                              node.SubType == NodeSubType::WaitForEvent;
            if (uses && node.Label == from) node.Label = to;
        }
        return true;
    }

    // One "Call <event>" palette entry per event the graph defines, plus the generic define node.
    [[nodiscard]] inline std::vector<NodeEntry> CustomEventNodeEntries(const AnimationGraphData& graph)
    {
        std::vector<NodeEntry> entries;
        for (const auto& name : CustomEventNames(graph))
        {
            const auto params = EventParams(*FindEventDefinition(graph, name));
            entries.push_back(SingleNodeEntry("Call " + name, kEventsCategory,
                                              "Runs the custom event '" + name + "' and wakes anything waiting for it.",
                                              {"event", "call", "custom", "trigger", name},
                                              [name, params](const int id)
                                              {
                                                  Node node = CreateCallEventNode(id, name, params);
                                                  node.Name = "Call " + name;
                                                  return node;
                                              }));
            entries.push_back(SingleNodeEntry("Wait For " + name, "Latent", "Pauses until the custom event '" + name + "' is called.",
                                              {"event", "wait", "latent", name},
                                              [name](const int id) { return CreateWaitForEventNode(id, name); }));
        }
        return entries;
    }
}
