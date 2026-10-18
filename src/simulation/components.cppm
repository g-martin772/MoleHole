module;
#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>
export module MoleHole:Simulation.Components;

import std;
import GPP;
import :Physics.Geodesic;
import :Rendering.Metric;

export namespace MoleHole
{
    // ThorneLimit: a* ≤ 0.998, what disk accretion can spin a hole up to. NearExtremal: a* = 1 - SpinGap, beyond
    // that limit, for Interstellar's Gargantua (1 - 1.3e-14). See docs/notes/near_extremal.md.
    enum class BlackHoleSpinRange : std::int32_t { ThorneLimit = 0, NearExtremal = 1 };

    struct BlackHoleComponent
    {
        double Mass{GPP::Units::kSolarMass}; // kg
        double Spin{0.0};                    // a* = cJ/(GM^2), dimensionless; used with SpinRange = ThorneLimit
        double Charge{0.0};                  // q*: charge in geometrized units over r_g, dimensionless
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f};
        BlackHoleSpinRange SpinRange{BlackHoleSpinRange::ThorneLimit};
        double SpinGap{1.3e-14}; // 1 - a*, used with SpinRange = NearExtremal
    };

    // The dimensionless spin and charge the physics uses (render and simulation alike), in geometric units of the
    // hole's own mass: √(a*² + q*²) clamped to the Thorne limit, or to 1 - kMinExtremalGap for a near-extremal hole.
    [[nodiscard]] inline KerrNewmanParams BlackHoleMetric(const BlackHoleComponent& hole)
    {
        const bool extremal = hole.SpinRange == BlackHoleSpinRange::NearExtremal;
        const double spin = extremal ? 1.0 - std::clamp(hole.SpinGap, kMinExtremalGap, 1.0 - kMaxHorizonParameter)
                                     : hole.Spin;
        return MakeKerrNewmanParams(static_cast<std::int32_t>(MetricKind::KerrNewman), 1.0, spin, hole.Charge,
                                    extremal ? kExtremalHorizonParameter : kMaxHorizonParameter);
    }

    struct SphereComponent
    {
        double Radius{1.0}; // m
        float Spin{0.0f};
        glm::vec3 Color{1.0f, 1.0f, 1.0f};
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f};
        std::string TexturePath;
    };

    // How a geodesic body starts moving; the first four mirror ObserverKind (src/physics/observer.cppm).
    enum class GeodesicLaunch : std::int32_t { Zamo = 0, Static = 1, Orbiting = 2, Raindrop = 3, Velocity = 4 };
    enum class GeodesicBodyStatus : std::int32_t { NotLaunched = 0, FreeFall = 1, Captured = 2, CannotLaunch = 3, NoBlackHole = 4 };

    // A test particle in free fall in the exact metric of one black hole (src/simulation/geodesic_module.cppm).
    // It does not gravitate itself. The launch fields are authored; everything below them is runtime state that
    // is not saved and is re-launched from the transform whenever the body is moved from outside.
    struct GeodesicBodyComponent
    {
        std::uint64_t BlackHole{0}; // guid of the hole whose metric it follows; 0 = the first black hole
        GeodesicLaunch Launch{GeodesicLaunch::Orbiting};
        bool Retrograde{false};          // for Launch = Orbiting
        glm::dvec3 LaunchVelocity{0.0};  // c, relative to the local ZAMO along (r̂, θ̂, φ̂), for Launch = Velocity

        GeodesicState<double> State{};
        GeodesicBodyStatus Status{GeodesicBodyStatus::NotLaunched};
        double ProperTime{0.0}; // s since launch
        double ClockRate{1.0};  // dτ/dt against the clock of a distant static observer
        double Speed{0.0};      // relative to the local ZAMO, in c
        glm::dvec3 WrittenPosition{0.0};
        std::uint64_t LaunchedFrom{0}; // hash of the launch fields and hole it was launched with
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
                    GPP::Field("SpinRange", &BlackHoleComponent::SpinRange,
                               {.Label = "Spin range", .Options = {"ThorneLimit", "NearExtremal"}}),
                    GPP::Field("Spin", &BlackHoleComponent::Spin,
                               {.Label = "Spin a*", .Min = 0.0, .Max = 0.998, .Speed = 0.01, .VisibleField = "SpinRange",
                                .VisibleMask = 1u << 0}),
                    GPP::Field("SpinGap", &BlackHoleComponent::SpinGap,
                               {.Label = "1 - a*", .Min = kMinExtremalGap, .Max = 1.0 - kMaxHorizonParameter,
                                .Speed = 1e-15, .Format = "%.3e", .VisibleField = "SpinRange", .VisibleMask = 1u << 1}),
                    GPP::Field("Charge", &BlackHoleComponent::Charge,
                               {.Label = "Charge q*", .Min = 0.0, .Max = 1.0, .Speed = 0.01}),
                    GPP::Field("SpinAxis", &BlackHoleComponent::SpinAxis,
                               {.Label = "Spin Axis", .Speed = 0.01, .Kind = GPP::FieldKind::Direction}),
                    GPP::Computed<BlackHoleComponent, std::string>(
                        "Status",
                        [](const BlackHoleComponent& c) -> std::string
                        {
                            if (c.SpinRange == BlackHoleSpinRange::NearExtremal) return "Near-extremal (beyond the Thorne limit)";
                            return std::hypot(c.Spin, c.Charge) > kMaxHorizonParameter ? "Clamped to the Thorne limit"
                                                                                        : "Black hole";
                        },
                        nullptr, {.Label = "Status"}),
                    GPP::Computed<BlackHoleComponent, double>(
                        "EffectiveSpin", [](const BlackHoleComponent& c) { return BlackHoleMetric(c).SpinParameter; },
                        nullptr, {.Label = "Effective a*", .Format = "%.15f"}),
                    GPP::Computed<BlackHoleComponent, double>(
                        "GravitationalRadius",
                        [](const BlackHoleComponent& c) { return GPP::Units::GravitationalRadius(c.Mass); }, nullptr,
                        {.Label = "r_g = GM/c\xC2\xB2", .Dim = GPP::Units::Dimension::Length}),
                    GPP::Computed<BlackHoleComponent, double>(
                        "HorizonRadius",
                        [](const BlackHoleComponent& c)
                        { return GPP::Units::GravitationalRadius(c.Mass) * OuterHorizonRadius(BlackHoleMetric(c)); },
                        nullptr, {.Label = "Horizon r+", .Dim = GPP::Units::Dimension::Length}),
                    GPP::Computed<BlackHoleComponent, double>(
                        "IscoRadius",
                        [](const BlackHoleComponent& c)
                        { return GPP::Units::GravitationalRadius(c.Mass) * ProgradeIscoRadius(BlackHoleMetric(c)); },
                        nullptr, {.Label = "ISCO (prograde)", .Dim = GPP::Units::Dimension::Length}),
                    GPP::Computed<BlackHoleComponent, double>(
                        "IscoTimeDilation",
                        [](const BlackHoleComponent& c)
                        {
                            const auto metric = BlackHoleMetric(c);
                            return 1.0 / std::sqrt(CircularOrbitNorm(metric, ProgradeIscoRadius(metric), OrbitDirection::Prograde));
                        },
                        nullptr, {.Label = "dt/dτ at the ISCO", .Format = "%.1f"}),
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

            GPP::RegisterComponent<GeodesicBodyComponent>("GeodesicBody", GPP::ComponentDescription<GeodesicBodyComponent>{
                .DisplayName = "Geodesic Body",
                .Fields = {
                    GPP::Field("BlackHole", &GeodesicBodyComponent::BlackHole,
                               {.Label = "Black hole", .Kind = GPP::FieldKind::EntityRef}),
                    GPP::Field("Launch", &GeodesicBodyComponent::Launch,
                               {.Label = "Launch", .Options = {"Zamo", "Static", "Orbiting", "Raindrop", "Velocity"}}),
                    GPP::Field("Retrograde", &GeodesicBodyComponent::Retrograde,
                               {.Label = "Retrograde", .VisibleField = "Launch", .VisibleMask = 1u << 2}),
                    GPP::Field("LaunchVelocity", &GeodesicBodyComponent::LaunchVelocity,
                               {.Label = "Velocity (r, θ, φ) / c", .Speed = 0.001, .VisibleField = "Launch",
                                .VisibleMask = 1u << 4}),
                    GPP::Computed<GeodesicBodyComponent, std::string>(
                        "Status",
                        [](const GeodesicBodyComponent& c) -> std::string
                        {
                            switch (c.Status)
                            {
                            case GeodesicBodyStatus::NotLaunched: return "Not launched";
                            case GeodesicBodyStatus::FreeFall: return "Free fall";
                            case GeodesicBodyStatus::Captured: return "Crossed the horizon";
                            case GeodesicBodyStatus::CannotLaunch: return "Cannot launch here";
                            case GeodesicBodyStatus::NoBlackHole: return "No black hole";
                            }
                            return "";
                        },
                        nullptr, {.Label = "Status"}),
                    GPP::Computed<GeodesicBodyComponent, double>(
                        "ProperTime", [](const GeodesicBodyComponent& c) { return c.ProperTime; }, nullptr,
                        {.Label = "Proper time", .Dim = GPP::Units::Dimension::Time}),
                    GPP::Computed<GeodesicBodyComponent, double>(
                        "ClockRate", [](const GeodesicBodyComponent& c) { return c.ClockRate; }, nullptr,
                        {.Label = "Clock rate dτ/dt", .Format = "%.6f"}),
                    GPP::Computed<GeodesicBodyComponent, double>(
                        "Speed", [](const GeodesicBodyComponent& c) { return c.Speed; }, nullptr,
                        {.Label = "Speed vs ZAMO / c", .Format = "%.4f"}),
                    GPP::Computed<GeodesicBodyComponent, double>(
                        "Radius", [](const GeodesicBodyComponent& c) { return c.State.R; }, nullptr,
                        {.Label = "r / M", .Format = "%.4f"}),
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
