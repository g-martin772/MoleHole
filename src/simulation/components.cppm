module;
#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>
export module MoleHole:Simulation.Components;

import std;
import GPP;

export namespace MoleHole
{
    struct BlackHoleComponent
    {
        float Mass{1.0f};
        float Spin{0.0f};
        float Charge{0.0f};
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f};
    };

    struct SphereComponent
    {
        float Radius{1.0f};
        float Spin{0.0f};
        glm::vec3 Color{1.0f, 1.0f, 1.0f};
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f};
        std::string TexturePath;
    };

    // Opaque storage for the animation-graph visual-scripting feature: the whole graph is kept as a
    // single YAML string produced/consumed by animation_graph.cppm's own SerializeToYaml/
    // DeserializeFromYaml (round-trip-tested in isolation there). Deliberately NOT decomposed into a
    // richer YAML shape here -- doing so would mean teaching GPP's generic scene serializer about an
    // app-specific graph structure, which this migration's framework/app layering explicitly avoids.
    // A plain string field round-trips through this exact same YAML::convert mechanism every other
    // string field (e.g. SphereComponent::TexturePath above) already uses.
    struct AnimationGraphComponent
    {
        std::string GraphYaml;
    };

    // TypeTag for the single, lazily-created, hidden bookkeeping entity that carries
    // AnimationGraphComponent. Not a real scene object -- UI panels that list ordinary entities
    // (e.g. SceneWindowLayer's Entities list) filter it out by this tag so it never shows up as a
    // mystery unnamed row or becomes selectable/deletable through normal entity UI.
    constexpr const char* kAnimationGraphDataTypeTag = "__AnimationGraphData";
}

namespace YAML
{
    template <>
    struct convert<MoleHole::BlackHoleComponent>
    {
        static Node encode(const MoleHole::BlackHoleComponent& value)
        {
            Node node;
            node["Mass"] = value.Mass;
            node["Spin"] = value.Spin;
            node["Charge"] = value.Charge;
            node["SpinAxis"] = value.SpinAxis;
            return node;
        }

        static bool decode(const Node& node, MoleHole::BlackHoleComponent& out)
        {
            if (node["Mass"]) out.Mass = node["Mass"].as<float>();
            if (node["Spin"]) out.Spin = node["Spin"].as<float>();
            if (node["Charge"]) out.Charge = node["Charge"].as<float>();
            if (node["SpinAxis"]) out.SpinAxis = node["SpinAxis"].as<glm::vec3>();
            return true;
        }
    };

    template <>
    struct convert<MoleHole::SphereComponent>
    {
        static Node encode(const MoleHole::SphereComponent& value)
        {
            Node node;
            node["Radius"] = value.Radius;
            node["Spin"] = value.Spin;
            node["Color"] = value.Color;
            node["SpinAxis"] = value.SpinAxis;
            node["TexturePath"] = value.TexturePath;
            return node;
        }

        static bool decode(const Node& node, MoleHole::SphereComponent& out)
        {
            if (node["Radius"]) out.Radius = node["Radius"].as<float>();
            if (node["Spin"]) out.Spin = node["Spin"].as<float>();
            if (node["Color"]) out.Color = node["Color"].as<glm::vec3>();
            if (node["SpinAxis"]) out.SpinAxis = node["SpinAxis"].as<glm::vec3>();
            if (node["TexturePath"]) out.TexturePath = node["TexturePath"].as<std::string>();
            return true;
        }
    };

    template <>
    struct convert<MoleHole::AnimationGraphComponent>
    {
        static Node encode(const MoleHole::AnimationGraphComponent& value)
        {
            Node node;
            node["GraphYaml"] = value.GraphYaml;
            return node;
        }

        static bool decode(const Node& node, MoleHole::AnimationGraphComponent& out)
        {
            if (node["GraphYaml"]) out.GraphYaml = node["GraphYaml"].as<std::string>();
            return true;
        }
    };
}

namespace MoleHole
{
    export void RegisterComponents()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            GPP::RegisterBaseComponents();
            GPP::RegisterComponent<BlackHoleComponent>("BlackHole");
            GPP::RegisterComponent<SphereComponent>("Sphere");
            GPP::RegisterComponent<AnimationGraphComponent>("AnimationGraph");
        });
    }
}
