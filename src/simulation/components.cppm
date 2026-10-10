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

    // Legacy single-graph storage; read only to migrate old scenes into the scene Graphs section.
    struct AnimationGraphComponent
    {
        std::string GraphYaml;
    };

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
            GPP::RegisterScriptComponents();

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
