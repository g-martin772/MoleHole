module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Simulation.SceneGraphs;

import std;
import GPP;
import :Simulation.Components;
import :Simulation.AnimationGraph;

export namespace MoleHole
{
    constexpr int kSceneGraphsVersion = 1;
    constexpr const char* kSceneGraphsKey = "Graphs";
    constexpr const char* kMainGraphName = "Main";

    struct NamedGraph
    {
        std::string Name;
        bool Enabled{true};
        std::uint64_t EntityGuid{0};
        AnimationGraphData Graph;
    };

    struct SceneGraphs
    {
        int Version{kSceneGraphsVersion};
        std::vector<NamedGraph> Items;

        [[nodiscard]] NamedGraph* Find(const std::string& name)
        {
            const auto it = std::ranges::find(Items, name, &NamedGraph::Name);
            return it != Items.end() ? &*it : nullptr;
        }

        [[nodiscard]] std::string UniqueName(const std::string& base)
        {
            if (!Find(base)) return base;
            for (int i = 2;; ++i)
            {
                auto candidate = base + " " + std::to_string(i);
                if (!Find(candidate)) return candidate;
            }
        }
    };

    [[nodiscard]] inline YAML::Node SceneGraphsToNode(const SceneGraphs& graphs)
    {
        YAML::Node root;
        root["Version"] = graphs.Version;
        YAML::Node items(YAML::NodeType::Sequence);
        for (const auto& item : graphs.Items)
        {
            YAML::Node entry;
            entry["Name"] = item.Name;
            entry["Enabled"] = item.Enabled;
            entry["Entity"] = item.EntityGuid;
            entry["Graph"] = GraphToNode(item.Graph);
            items.push_back(entry);
        }
        root["Items"] = items;
        return root;
    }

    [[nodiscard]] inline SceneGraphs SceneGraphsFromNode(const YAML::Node& root)
    {
        SceneGraphs graphs;
        if (!root || !root.IsMap()) return graphs;
        if (root["Version"]) graphs.Version = root["Version"].as<int>();
        if (const auto items = root["Items"]; items && items.IsSequence())
        {
            for (const auto& entry : items)
            {
                NamedGraph graph;
                graph.Name = entry["Name"] ? entry["Name"].as<std::string>() : std::string(kMainGraphName);
                graph.Name = graphs.UniqueName(graph.Name);
                graph.Enabled = entry["Enabled"] ? entry["Enabled"].as<bool>() : true;
                graph.EntityGuid = entry["Entity"] ? entry["Entity"].as<std::uint64_t>() : std::uint64_t{0};
                if (entry["Graph"]) graph.Graph = GraphFromNode(entry["Graph"]);
                graphs.Items.push_back(std::move(graph));
            }
        }
        return graphs;
    }

    // Falls back to the legacy hidden AnimationGraph entity (YAML-in-a-string) as a graph named "Main".
    [[nodiscard]] inline SceneGraphs LoadSceneGraphs(const GPP::Scene& scene)
    {
        if (const auto* node = scene.FindExtension(kSceneGraphsKey)) return SceneGraphsFromNode(*node);

        SceneGraphs graphs;
        for (auto [entity, component] : scene.Registry().view<const AnimationGraphComponent>().each())
        {
            if (component.GraphYaml.empty()) continue;
            graphs.Items.push_back(NamedGraph{.Name = kMainGraphName, .Graph = DeserializeFromYaml(component.GraphYaml)});
            break;
        }
        return graphs;
    }

    inline void StoreSceneGraphs(GPP::Scene& scene, const SceneGraphs& graphs)
    {
        scene.SetExtension(kSceneGraphsKey, SceneGraphsToNode(graphs));
        std::vector<entt::entity> legacy;
        for (auto [entity, component] : scene.Registry().view<AnimationGraphComponent>().each()) legacy.push_back(entity);
        for (auto entity : legacy) scene.DestroyEntity(entity);
    }
}
