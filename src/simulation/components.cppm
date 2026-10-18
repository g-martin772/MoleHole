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

    // Thermal sphere emitter: luminosity, colour and surface radiance follow from Teff and Radius (docs/notes/lighting-materials.md).
    struct StarComponent
    {
        double Teff{GPP::Units::kSolarEffectiveTemperature}; // K
        double Radius{GPP::Units::kSolarRadius};             // m
        double Mass{GPP::Units::kSolarMass};                 // kg
        float LimbDarkening{0.6f};                           // u in I(mu) = I0 (1 - u (1 - mu))
    };

    // L = 4 pi R^2 sigma T^4 [W]
    [[nodiscard]] inline double StarLuminosity(const StarComponent& star)
    {
        const double t2 = star.Teff * star.Teff;
        return 4.0 * static_cast<double>(GPP::Units::Pi()) * star.Radius * star.Radius
               * GPP::Units::kStefanBoltzmann * t2 * t2;
    }

    // Bolometric surface radiance averaged over the disc, sigma T^4 / pi [W m^-2 sr^-1]
    [[nodiscard]] inline double StarSurfaceRadiance(const StarComponent& star)
    {
        const double t2 = star.Teff * star.Teff;
        return GPP::Units::kStefanBoltzmann * t2 * t2 / static_cast<double>(GPP::Units::Pi());
    }

    // Linear sRGB of the Planck spectrum at Teff, relative luminance 1
    [[nodiscard]] inline glm::vec3 StarColor(const StarComponent& star)
    {
        return GPP::Radiometry::BlackbodyColor(star.Teff);
    }

    // Fraction of the bolometric power that counts as luminance in the 683 lm/W convention of the light buffer:
    // K(T) / K_cd with K(T) the luminous efficacy of the blackbody. 1 would treat all power as 555 nm light.
    [[nodiscard]] inline double StarLuminousFraction(const StarComponent& star)
    {
        return GPP::Radiometry::BlackbodyLuminousEfficacy(star.Teff) / GPP::Radiometry::kLuminousEfficacyMax;
    }

    // Disc-averaged radiance over I0 for I(mu) = I0 (1 - u (1 - mu)): 1 - u / 3
    [[nodiscard]] inline double StarLimbMean(const StarComponent& star)
    {
        return 1.0 - static_cast<double>(std::clamp(star.LimbDarkening, 0.0f, 1.0f)) / 3.0;
    }

    // Radiance at the disc centre in image units (value 1 = the 203 cd/m^2 reference white) of the disc the tracer draws:
    // the luminance of sigma T^4 / pi, with the limb darkening profile normalised to the same disc mean.
    [[nodiscard]] inline double StarImageRadiance(const StarComponent& star)
    {
        return StarSurfaceRadiance(star) * StarLuminousFraction(star) * GPP::Radiometry::kLuminousEfficacyMax
               / GPP::Radiometry::kHdrReferenceWhite / StarLimbMean(star);
    }

    // Legacy single-graph storage; read only to migrate old scenes into the scene Graphs section.
    struct AnimationGraphComponent
    {
        std::string GraphYaml;
    };

    constexpr const char* kAnimationGraphDataTypeTag = "__AnimationGraphData";
}

namespace MoleHole
{
    export void RegisterAccretionDiskComponent();

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
                               {.Label = "Spin a*", .Min = -1.0, .Max = 1.0, .Speed = 0.01}),
                    GPP::Field("Charge", &BlackHoleComponent::Charge,
                               {.Label = "Charge q*", .Min = 0.0, .Max = 1.0, .Speed = 0.01}),
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

            GPP::RegisterComponent<StarComponent>("Star", GPP::ComponentDescription<StarComponent>{
                .DisplayName = "Star",
                .Note = "Thermal sphere emitter: drawn by the tracer and lights the meshes (E = L / 4 pi d^2).",
                .Fields = {
                    GPP::Field("Teff", &StarComponent::Teff,
                               {.Label = "T eff", .Min = 500.0, .Max = 1.0e6, .Speed = 50.0,
                                .Dim = GPP::Units::Dimension::Temperature}),
                    GPP::Field("Radius", &StarComponent::Radius,
                               {.Min = 1.0, .Speed = 1.0e6, .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("Mass", &StarComponent::Mass,
                               {.Min = 1.0, .Speed = 1.0e28, .Dim = GPP::Units::Dimension::Mass}),
                    GPP::Field("LimbDarkening", &StarComponent::LimbDarkening,
                               {.Label = "Limb darkening u", .Min = 0.0f, .Max = 1.0f, .Speed = 0.01f}),
                    GPP::Computed<StarComponent, double>(
                        "Luminosity", [](const StarComponent& c) { return StarLuminosity(c); }, nullptr,
                        {.Label = "Luminosity", .Dim = GPP::Units::Dimension::Power}),
                    GPP::Computed<StarComponent, glm::vec3>(
                        "Colour", [](const StarComponent& c) { return StarColor(c); }, nullptr,
                        {.Label = "Colour (Planck)", .Kind = GPP::FieldKind::Color}),
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

            RegisterAccretionDiskComponent();
        });
    }
}
