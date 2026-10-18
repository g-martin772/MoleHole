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
        double Mass{GPP::Units::kSolarMass}; // kg
        double Spin{0.0};                    // a* = cJ/(GM^2), dimensionless
        double Charge{0.0};                  // q*: charge in geometrized units over r_g, dimensionless
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f};
    };

    struct SphereComponent
    {
        double Radius{1.0}; // m
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
                               {.Label = "Mass", .Min = 1.0, .Speed = 1.0e27, .Dim = GPP::Units::Dimension::Mass,
                                .LegacyScale = GPP::Units::Legacy::kBlackHoleMassKg}),
                    GPP::Field("Spin", &BlackHoleComponent::Spin,
                               {.Label = "Spin a*", .Min = -1.5, .Max = 1.5, .Speed = 0.01}),
                    GPP::Field("Charge", &BlackHoleComponent::Charge,
                               {.Label = "Charge q*", .Min = 0.0, .Max = 1.5, .Speed = 0.01}),
                    GPP::Field("SpinAxis", &BlackHoleComponent::SpinAxis,
                               {.Label = "Spin Axis", .Speed = 0.01, .Kind = GPP::FieldKind::Direction}),
                    GPP::Computed<BlackHoleComponent, std::string>(
                        "Status",
                        [](const BlackHoleComponent& c) -> std::string
                        {
                            return GPP::Units::HasHorizon(c.Spin, c.Charge) ? "Black hole"
                                                                            : "Naked singularity";
                        },
                        nullptr, {.Label = "Status"}),
                    GPP::Computed<BlackHoleComponent, double>(
                        "GravitationalRadius",
                        [](const BlackHoleComponent& c) { return GPP::Units::GravitationalRadius(c.Mass); }, nullptr,
                        {.Label = "r_g = GM/c\xC2\xB2", .Dim = GPP::Units::Dimension::Length}),
                    GPP::Computed<BlackHoleComponent, double>(
                        "HorizonRadius",
                        [](const BlackHoleComponent& c)
                        {
                            return GPP::Units::GravitationalRadius(c.Mass)
                                   * GPP::Units::HorizonRadiusInGravitationalRadii(c.Spin, c.Charge);
                        },
                        nullptr, {.Label = "Horizon r+", .Dim = GPP::Units::Dimension::Length}),
                    GPP::Computed<BlackHoleComponent, double>(
                        "IscoRadius",
                        [](const BlackHoleComponent& c)
                        {
                            return GPP::Units::GravitationalRadius(c.Mass) * GPP::Units::IscoInGravitationalRadii(c.Spin);
                        },
                        nullptr, {.Label = "ISCO (prograde)", .Dim = GPP::Units::Dimension::Length}),
                }});

            GPP::RegisterComponent<SphereComponent>("Sphere", GPP::ComponentDescription<SphereComponent>{
                .Fields = {
                    GPP::Field("Radius", &SphereComponent::Radius, {.Min = 1e-3, .Speed = 0.1, .Dim = GPP::Units::Dimension::Length,
                                .LegacyScale = GPP::Units::Legacy::kLengthM}),
                    GPP::Field("Spin", &SphereComponent::Spin, {.Min = 0.0, .Max = 1.0, .Speed = 0.01}),
                    GPP::Field("Color", &SphereComponent::Color, {.Kind = GPP::FieldKind::Color}),
                    GPP::Field("SpinAxis", &SphereComponent::SpinAxis,
                               {.Label = "Spin Axis", .Speed = 0.01, .Kind = GPP::FieldKind::Direction}),
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
