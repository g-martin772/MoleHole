module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Simulation.GraphClipboard;

import std;
import glm;
import :Simulation.AnimationGraph;

export namespace MoleHole
{
    constexpr const char* kGraphClipboardFormat = "MoleHoleGraphClipboard";

    struct GraphPasteResult
    {
        std::vector<int> NodeIds;
    };

    [[nodiscard]] inline std::string CopyGraphSelection(const AnimationGraphData& graph, const std::vector<int>& nodeIds)
    {
        AnimationGraphData subset;
        std::unordered_set<int> pins;
        for (const auto& node : graph.Nodes)
        {
            if (!std::ranges::contains(nodeIds, node.Id)) continue;
            subset.Nodes.push_back(node);
            for (const auto& pin : node.Inputs) pins.insert(pin.Id);
            for (const auto& pin : node.Outputs) pins.insert(pin.Id);
            if (!node.VariableName.empty() && !std::ranges::contains(subset.Variables, node.VariableName, &Variable::Name))
            {
                if (const auto it = std::ranges::find(graph.Variables, node.VariableName, &Variable::Name);
                    it != graph.Variables.end())
                {
                    subset.Variables.push_back(*it);
                }
            }
        }
        for (const auto& link : graph.Links)
        {
            if (pins.contains(link.StartPinId) && pins.contains(link.EndPinId)) subset.Links.push_back(link);
        }
        subset.NextId = graph.NextId;

        YAML::Node root;
        root["Format"] = kGraphClipboardFormat;
        root["Version"] = 1;
        root["Graph"] = GraphToNode(subset);
        std::stringstream stream;
        stream << root;
        return stream.str();
    }

    [[nodiscard]] inline bool IsGraphClipboardText(const std::string_view text)
    {
        return text.find(kGraphClipboardFormat) != std::string_view::npos;
    }

    // Pasted nodes get fresh ids; the selection's top-left corner lands on `anchor`.
    inline GraphPasteResult PasteGraphSelection(AnimationGraphData& graph, const std::string& text, const glm::vec2 anchor)
    {
        GraphPasteResult result;
        if (!IsGraphClipboardText(text)) return result;

        AnimationGraphData source;
        try
        {
            const auto root = YAML::Load(text);
            if (!root.IsMap() || !root["Format"] || root["Format"].as<std::string>() != kGraphClipboardFormat ||
                !root["Graph"])
            {
                return result;
            }
            source = GraphFromNode(root["Graph"]);
        }
        catch (const YAML::Exception&)
        {
            return result;
        }
        if (source.Nodes.empty()) return result;

        glm::vec2 minPos = source.Nodes.front().Position;
        for (const auto& node : source.Nodes) minPos = glm::min(minPos, node.Position);

        std::unordered_map<int, int> pinMap;
        for (auto node : source.Nodes)
        {
            const int oldId = node.Id;
            const int newId = graph.AllocateId();
            const auto remap = [&](Pin& pin)
            {
                const int fresh = newId * kPinIdStride + (pin.Id - oldId * kPinIdStride);
                pinMap[pin.Id] = fresh;
                pin.Id = fresh;
            };
            for (auto& pin : node.Inputs) remap(pin);
            for (auto& pin : node.Outputs) remap(pin);
            node.Id = newId;
            node.Position = node.Position - minPos + anchor;
            graph.Nodes.push_back(std::move(node));
            result.NodeIds.push_back(newId);
        }

        for (const auto& link : source.Links)
        {
            const auto from = pinMap.find(link.StartPinId);
            const auto to = pinMap.find(link.EndPinId);
            if (from == pinMap.end() || to == pinMap.end()) continue;
            graph.Links.push_back(Link{graph.AllocateId(), from->second, to->second});
        }

        for (const auto& variable : source.Variables)
        {
            if (!std::ranges::contains(graph.Variables, variable.Name, &Variable::Name)) graph.Variables.push_back(variable);
        }
        return result;
    }
}
