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

namespace MoleHole
{
    export void RegisterComponents()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            GPP::RegisterBaseComponents();

            GPP::RegisterComponent<BlackHoleComponent>("BlackHole", GPP::ComponentDescription<BlackHoleComponent>{
                .DisplayName = "Black Hole",
                .Fields = {
                    GPP::Field("Mass", &BlackHoleComponent::Mass,
                               {.Label = "Mass (solar)", .Min = 0.0f, .Max = 1000.0f, .Speed = 0.01f}),
                    GPP::Field("Spin", &BlackHoleComponent::Spin, {.Min = 0.0f, .Max = 1.0f, .Speed = 0.01f}),
                    GPP::Field("Charge", &BlackHoleComponent::Charge, {.Min = 0.0f, .Max = 1.0f, .Speed = 0.01f}),
                    GPP::Field("SpinAxis", &BlackHoleComponent::SpinAxis,
                               {.Label = "Spin Axis", .Speed = 0.01f, .Kind = GPP::FieldKind::Direction}),
                }});

            GPP::RegisterComponent<SphereComponent>("Sphere", GPP::ComponentDescription<SphereComponent>{
                .Fields = {
                    GPP::Field("Radius", &SphereComponent::Radius, {.Min = 0.01f, .Max = 100.0f, .Speed = 0.01f}),
                    GPP::Field("Spin", &SphereComponent::Spin, {.Min = 0.0f, .Max = 1.0f, .Speed = 0.01f}),
                    GPP::Field("Color", &SphereComponent::Color, {.Kind = GPP::FieldKind::Color}),
                    GPP::Field("SpinAxis", &SphereComponent::SpinAxis,
                               {.Label = "Spin Axis", .Speed = 0.01f, .Kind = GPP::FieldKind::Direction}),
                    GPP::Field("TexturePath", &SphereComponent::TexturePath,
                               {.Label = "Texture", .Kind = GPP::FieldKind::AssetPath}),
                }});

            GPP::RegisterComponent<AnimationGraphComponent>("AnimationGraph",
                GPP::ComponentDescription<AnimationGraphComponent>{
                    .DisplayName = "Animation Graph",
                    .Inspectable = false,
                    .GraphExposed = false,
                    .Fields = {
                        GPP::Field("GraphYaml", &AnimationGraphComponent::GraphYaml,
                                   {.Kind = GPP::FieldKind::Multiline}),
                    }});
        });
    }
}
