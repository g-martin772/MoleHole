export module MoleHole:Simulation.GraphEdit;

import std;
import glm;
import :Simulation.AnimationGraph;

export namespace MoleHole
{
    [[nodiscard]] inline const Pin* FindPin(const AnimationGraphData& graph, const int pinId)
    {
        for (const auto& node : graph.Nodes)
        {
            for (const auto& pin : node.Inputs) if (pin.Id == pinId) return &pin;
            for (const auto& pin : node.Outputs) if (pin.Id == pinId) return &pin;
        }
        return nullptr;
    }

    [[nodiscard]] inline bool CanLink(const AnimationGraphData& graph, const int outputPinId, const int inputPinId)
    {
        const Node* from = graph.FindNodeByOutputPin(outputPinId);
        const Node* to = graph.FindNodeByInputPin(inputPinId);
        if (!from || !to || from == to) return false;
        const Pin* out = FindPin(graph, outputPinId);
        const Pin* in = FindPin(graph, inputPinId);
        if (!out || !in || out->IsInput || !in->IsInput || !ArePinsCompatible(out->Type, in->Type)) return false;
        return std::ranges::none_of(graph.Links, [inputPinId](const Link& l) { return l.EndPinId == inputPinId; });
    }

    inline bool TryLink(AnimationGraphData& graph, const int outputPinId, const int inputPinId)
    {
        if (!CanLink(graph, outputPinId, inputPinId)) return false;
        graph.Links.push_back(Link{graph.AllocateId(), outputPinId, inputPinId});
        return true;
    }

    // First pin on the opposite side of the dragged pin that it could be wired to.
    [[nodiscard]] inline const Pin* FirstCompatiblePin(const Node& node, const PinType dragged, const bool draggedIsOutput)
    {
        const auto& pins = draggedIsOutput ? node.Inputs : node.Outputs;
        const auto it = std::ranges::find_if(pins, [dragged](const Pin& p) { return ArePinsCompatible(dragged, p.Type); });
        return it != pins.end() ? &*it : nullptr;
    }

    // Wires a freshly created node to the pin a link was dragged from. Dragging from a used input replaces its link.
    inline bool ConnectNewNode(AnimationGraphData& graph, const int nodeId, const int draggedPinId)
    {
        const Node* node = graph.FindNode(nodeId);
        const Pin* dragged = FindPin(graph, draggedPinId);
        if (!node || !dragged) return false;

        const Pin* target = FirstCompatiblePin(*node, dragged->Type, !dragged->IsInput);
        if (!target) return false;
        if (dragged->IsInput)
        {
            std::erase_if(graph.Links, [draggedPinId](const Link& l) { return l.EndPinId == draggedPinId; });
            return TryLink(graph, target->Id, draggedPinId);
        }
        return TryLink(graph, draggedPinId, target->Id);
    }

    inline void RemoveNodes(AnimationGraphData& graph, const std::vector<int>& nodeIds)
    {
        for (const int id : nodeIds) graph.RemoveNode(id);
    }
}
