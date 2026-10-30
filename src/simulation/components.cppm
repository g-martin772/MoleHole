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
        return MakeKerrNewmanParams(static_cast<std::int32_t>(MetricKind::KerrNewman), 1.0, spin, hole.Charge);
    }

    // a* with its sign: a negative a* reverses the spin axis, the metric only sees |a*|. Near-extremal holes spin forward.
    [[nodiscard]] inline double BlackHoleSignedSpin(const BlackHoleComponent& hole)
    {
        return hole.SpinRange == BlackHoleSpinRange::NearExtremal ? 1.0 : hole.Spin;
    }

    struct SphereComponent
    {
        double Radius{1.0}; // m
        float Spin{0.0f};
        glm::vec3 Color{1.0f, 1.0f, 1.0f};
        glm::vec3 SpinAxis{0.0f, 1.0f, 0.0f};
        std::string TexturePath; // albedo of a sphere without a Surface
    };

    // Appearance of a sphere: equirectangular maps (u = east longitude from -180°, v = 0 at the north pole along
    // SpinAxis) and rotation about the sphere's SpinAxis. See docs/notes/textured_bodies.md
    struct SurfaceComponent
    {
        std::string Albedo;    // sRGB colour
        std::string Normal;    // tangent space (x east, y north, z up), linear
        std::string Roughness; // linear, red channel
        std::string Emissive;  // sRGB colour, e.g. city lights on the night side
        glm::vec3 Tint{1.0f, 1.0f, 1.0f};
        float RoughnessFactor{0.9f};    // multiplies the map; the whole surface without one
        bool RoughnessIsSpecular{false}; // the map is a specular mask: roughness = mix(factor, GlossyRoughness, map)
        // Wind-roughened ocean seen from orbit: Cox & Munk (1954) slope variance 0.003 + 5.12e-3 U at U = 7 m/s gives an
        // rms slope of about 0.2 = GGX alpha, i.e. roughness sqrt(0.2) ≈ 0.45
        float GlossyRoughness{0.45f};
        float NormalStrength{1.0f};
        float EmissiveStrength{1.0f};
        double RotationPeriod{0.0}; // s of the body's proper time per turn, 0 = not rotating, negative = retrograde
        float PrimeMeridian{0.0f};   // degrees east of the sphere's reference direction at rotation angle 0

        double RotationAngle{0.0}; // runtime, rad: advanced by BodyRotationModule, not saved
    };

    // Scattering atmosphere around a sphere (Hillaire 2020): Rayleigh and Mie scattering with exponential density,
    // ozone absorption in a tent profile. Defaults are Earth's (Hillaire 2020 Table 1); coefficients are per metre.
    // See docs/notes/atmospheres.md and docs/notes/interstellar_planets.md for the other planets' values.
    struct AtmosphereComponent
    {
        double Height{100000.0}; // m above the surface where the density is taken as zero
        glm::dvec3 RayleighScattering{5.802e-6, 13.558e-6, 33.1e-6}; // 1/m at 680, 550, 440 nm
        double RayleighScaleHeight{8000.0};
        double MieScattering{3.996e-6};
        double MieAbsorption{4.40e-6};
        double MieScaleHeight{1200.0};
        float MieAnisotropy{0.8f}; // Cornette–Shanks g
        glm::dvec3 OzoneAbsorption{0.650e-6, 1.881e-6, 0.085e-6};
        double OzoneCenter{25000.0}; // m, peak of the tent profile
        double OzoneWidth{30000.0};  // m, full width of the tent
    };

    // A thin cloud shell: coverage map (white = overcast), drawn at Altitude, casting shadows on the surface.
    struct CloudLayerComponent
    {
        std::string Coverage;     // equirectangular, red channel, same mapping as the Surface maps
        double Altitude{4000.0};  // m above the surface
        float Opacity{0.9f};      // of full coverage
        glm::vec3 Color{1.0f, 1.0f, 1.0f};
        double RotationPeriod{0.0}; // s per turn relative to the surface, 0 = turns with it
        double RotationAngle{0.0};  // runtime, rad: advanced by BodyRotationModule, not saved
    };

    // Relief on a sphere: an equirectangular height map (same mapping as the Surface maps) between HeightMin and
    // HeightMax, plus procedural detail. Ray-marched when the camera is in the body's local frame, shaded as relief
    // (normals only) from farther away. With Ocean, everything below sea level (the sphere's radius) is water.
    // See docs/notes/terrain.md
    struct TerrainComponent
    {
        std::string Height;          // black = HeightMin, white = HeightMax
        double HeightMin{0.0};       // m relative to the sphere's radius
        double HeightMax{6400.0};
        bool Ocean{true};
        double DetailAmplitude{0.0}; // m of fBm detail added on top
        double DetailScale{2000.0};  // m, wavelength of the largest detail octave
        int DetailOctaves{5};
        int Seed{1};
    };

    // A flat ring system in the sphere's equatorial plane (perpendicular to SpinAxis). The texture is a radial strip:
    // u runs from InnerRadius to OuterRadius, colour in rgb and coverage in alpha. Defaults: Saturn's C ring inner
    // edge to A ring outer edge (NASA Saturnian Rings Fact Sheet, docs/notes/interstellar_planets.md §5).
    struct RingComponent
    {
        std::string Texture;
        double InnerRadius{74658.0e3}; // m from the centre
        double OuterRadius{136780.0e3};
        float Opacity{1.0f};           // multiplies the texture's alpha
    };

    enum class BodyLightKind : std::int32_t { Directional = 0, Point = 1 };

    // A light that is not part of the traced scene, such as the Sun for Earth and Saturn. It lights Surfaces directly
    // along a straight line, without lensing, redshift or shadows from other bodies; emitters inside the scene (the
    // disk, stars, lit bodies) reach Surfaces lensed through their sky probes. See docs/notes/body_lighting.md
    struct LightComponent
    {
        BodyLightKind Kind{BodyLightKind::Directional};
        glm::vec3 Color{1.0f, 1.0f, 1.0f};
        // Irradiance in display units: π makes a white Lambertian surface facing the light show 1.0. A point light
        // has it at ReferenceDistance and falls off as the inverse square.
        float Intensity{3.14159265f};
        glm::vec3 Direction{1.0f, 0.3f, 0.5f};   // Directional: towards the light
        double ReferenceDistance{1.495978707e11}; // m, Point: 1 AU
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

    // A body of the Newtonian N-body system (src/simulation/nbody_module.cppm): it moves under the pull of every other
    // body with a GM, the 1PN corrections of general relativity and the J2 of the oblate ones. Position is the
    // entity's Transform (barycentric, metres); the rest is here. It needs no RigidBody and does not use PhysX.
    struct NBodyComponent
    {
        double GM{0.0};            // m^3/s^2, the gravitational parameter G M; 0 = a test particle that only feels the others
        glm::dvec3 Velocity{0.0};  // m/s, in the scene frame
        double J2{0.0};            // second zonal harmonic, about the Sphere's SpinAxis; 0 = a point mass
        double J2Radius{0.0};      // m, the reference radius of J2; 0 = the Sphere's Radius
    };

    // Legacy single-graph storage; read only to migrate old scenes into the scene Graphs section.
    struct AnimationGraphComponent
    {
        std::string GraphYaml;
    };

    struct EditorFlagsComponent
    {
        bool Hidden{false};
        bool Locked{false};
    };

    [[nodiscard]] inline bool IsHidden(const entt::registry& registry, const entt::entity entity)
    {
        const auto* flags = registry.try_get<EditorFlagsComponent>(entity);
        return flags && flags->Hidden;
    }

    [[nodiscard]] inline bool IsLocked(const entt::registry& registry, const entt::entity entity)
    {
        const auto* flags = registry.try_get<EditorFlagsComponent>(entity);
        return flags && flags->Locked;
    }

    constexpr const char* kAnimationGraphDataTypeTag = "__AnimationGraphData";

    constexpr const char* kSceneGraphsKey = "Graphs";
    constexpr int kSceneGraphsVersion = 3;
    constexpr const char* kRenderExtensionKey = "Render";
    constexpr int kRenderExtensionVersion = 1;
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

            auto& migrations = GPP::SceneMigrations::Instance();
            migrations.RegisterExtension(kSceneGraphsKey, kSceneGraphsVersion);
            migrations.RegisterExtension(kRenderExtensionKey, kRenderExtensionVersion);
            migrations.Register({.Extension = kSceneGraphsKey, .From = 1, .To = 2,
                                 .Description = "comments, reroutes and variables are optional additions"});
            migrations.Register({.Extension = kSceneGraphsKey, .From = 2, .To = 3,
                                 .Description = "functions and component graphs are optional additions"});

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
                               {.Label = "Texture", .Kind = GPP::FieldKind::AssetPath,
                                .Asset = GPP::AssetFieldKind::Texture}),
                }});

            GPP::RegisterComponent<SurfaceComponent>("Surface", GPP::ComponentDescription<SurfaceComponent>{
                .Fields = {
                    GPP::Field("Albedo", &SurfaceComponent::Albedo,
                               {.Label = "Albedo map", .Category = "Maps", .Kind = GPP::FieldKind::AssetPath,
                                .Asset = GPP::AssetFieldKind::Texture}),
                    GPP::Field("Normal", &SurfaceComponent::Normal,
                               {.Label = "Normal map", .Category = "Maps", .Kind = GPP::FieldKind::AssetPath,
                                .Asset = GPP::AssetFieldKind::Texture}),
                    GPP::Field("Roughness", &SurfaceComponent::Roughness,
                               {.Label = "Roughness map", .Category = "Maps", .Kind = GPP::FieldKind::AssetPath,
                                .Asset = GPP::AssetFieldKind::Texture}),
                    GPP::Field("Emissive", &SurfaceComponent::Emissive,
                               {.Label = "Emissive map", .Category = "Maps", .Kind = GPP::FieldKind::AssetPath,
                                .Asset = GPP::AssetFieldKind::Texture}),
                    GPP::Field("Tint", &SurfaceComponent::Tint,
                               {.Label = "Tint", .Category = "Material", .Kind = GPP::FieldKind::Color}),
                    GPP::Field("RoughnessFactor", &SurfaceComponent::RoughnessFactor,
                               {.Label = "Roughness", .Category = "Material", .Min = 0.02, .Max = 1.0, .Speed = 0.01}),
                    GPP::Field("RoughnessIsSpecular", &SurfaceComponent::RoughnessIsSpecular,
                               {.Label = "Map is a specular mask", .Category = "Material"}),
                    GPP::Field("GlossyRoughness", &SurfaceComponent::GlossyRoughness,
                               {.Label = "Glossy roughness", .Category = "Material", .Min = 0.02, .Max = 1.0, .Speed = 0.01}),
                    GPP::Field("NormalStrength", &SurfaceComponent::NormalStrength,
                               {.Label = "Normal strength", .Category = "Material", .Min = 0.0, .Max = 4.0, .Speed = 0.01}),
                    GPP::Field("EmissiveStrength", &SurfaceComponent::EmissiveStrength,
                               {.Label = "Emissive strength", .Category = "Material", .Min = 0.0, .Max = 100.0, .Speed = 0.01}),
                    GPP::Field("RotationPeriod", &SurfaceComponent::RotationPeriod,
                               {.Label = "Rotation period", .Category = "Rotation", .Speed = 60.0,
                                .Dim = GPP::Units::Dimension::Time}),
                    GPP::Field("PrimeMeridian", &SurfaceComponent::PrimeMeridian,
                               {.Label = "Prime meridian (deg)", .Category = "Rotation", .Min = -180.0, .Max = 180.0,
                                .Speed = 0.5}),
                }});

            GPP::RegisterComponent<AtmosphereComponent>("Atmosphere", GPP::ComponentDescription<AtmosphereComponent>{
                .Fields = {
                    GPP::Field("Height", &AtmosphereComponent::Height,
                               {.Label = "Height", .Min = 1.0, .Speed = 100.0, .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("RayleighScattering", &AtmosphereComponent::RayleighScattering,
                               {.Label = "Scattering (R, G, B) 1/m", .Category = "Rayleigh", .Min = 0.0, .Speed = 1e-7}),
                    GPP::Field("RayleighScaleHeight", &AtmosphereComponent::RayleighScaleHeight,
                               {.Label = "Scale height", .Category = "Rayleigh", .Min = 1.0, .Speed = 10.0,
                                .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("MieScattering", &AtmosphereComponent::MieScattering,
                               {.Label = "Scattering 1/m", .Category = "Mie (aerosols)", .Min = 0.0, .Speed = 1e-7,
                                .Format = "%.3e"}),
                    GPP::Field("MieAbsorption", &AtmosphereComponent::MieAbsorption,
                               {.Label = "Absorption 1/m", .Category = "Mie (aerosols)", .Min = 0.0, .Speed = 1e-7,
                                .Format = "%.3e"}),
                    GPP::Field("MieScaleHeight", &AtmosphereComponent::MieScaleHeight,
                               {.Label = "Scale height", .Category = "Mie (aerosols)", .Min = 1.0, .Speed = 10.0,
                                .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("MieAnisotropy", &AtmosphereComponent::MieAnisotropy,
                               {.Label = "Anisotropy g", .Category = "Mie (aerosols)", .Min = 0.0, .Max = 0.999, .Speed = 0.01}),
                    GPP::Field("OzoneAbsorption", &AtmosphereComponent::OzoneAbsorption,
                               {.Label = "Absorption (R, G, B) 1/m", .Category = "Ozone", .Min = 0.0, .Speed = 1e-8}),
                    GPP::Field("OzoneCenter", &AtmosphereComponent::OzoneCenter,
                               {.Label = "Peak altitude", .Category = "Ozone", .Min = 0.0, .Speed = 100.0,
                                .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("OzoneWidth", &AtmosphereComponent::OzoneWidth,
                               {.Label = "Width", .Category = "Ozone", .Min = 0.0, .Speed = 100.0,
                                .Dim = GPP::Units::Dimension::Length}),
                }});

            GPP::RegisterComponent<CloudLayerComponent>("CloudLayer", GPP::ComponentDescription<CloudLayerComponent>{
                .DisplayName = "Cloud Layer",
                .Fields = {
                    GPP::Field("Coverage", &CloudLayerComponent::Coverage,
                               {.Label = "Coverage map", .Kind = GPP::FieldKind::AssetPath,
                                .Asset = GPP::AssetFieldKind::Texture}),
                    GPP::Field("Altitude", &CloudLayerComponent::Altitude,
                               {.Label = "Altitude", .Min = 0.0, .Speed = 10.0, .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("Opacity", &CloudLayerComponent::Opacity,
                               {.Label = "Opacity", .Min = 0.0, .Max = 1.0, .Speed = 0.01}),
                    GPP::Field("Color", &CloudLayerComponent::Color, {.Label = "Colour", .Kind = GPP::FieldKind::Color}),
                    GPP::Field("RotationPeriod", &CloudLayerComponent::RotationPeriod,
                               {.Label = "Drift period", .Min = 0.0, .Speed = 60.0, .Dim = GPP::Units::Dimension::Time}),
                }});

            GPP::RegisterComponent<TerrainComponent>("Terrain", GPP::ComponentDescription<TerrainComponent>{
                .Fields = {
                    GPP::Field("Height", &TerrainComponent::Height, {.Label = "Height map", .Kind = GPP::FieldKind::AssetPath,
                                .Asset = GPP::AssetFieldKind::Texture}),
                    GPP::Field("HeightMin", &TerrainComponent::HeightMin,
                               {.Label = "Height of black", .Speed = 10.0, .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("HeightMax", &TerrainComponent::HeightMax,
                               {.Label = "Height of white", .Speed = 10.0, .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("Ocean", &TerrainComponent::Ocean, {.Label = "Ocean below sea level"}),
                    GPP::Field("DetailAmplitude", &TerrainComponent::DetailAmplitude,
                               {.Label = "Amplitude", .Category = "Detail", .Min = 0.0, .Speed = 1.0,
                                .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("DetailScale", &TerrainComponent::DetailScale,
                               {.Label = "Largest wavelength", .Category = "Detail", .Min = 1.0, .Speed = 10.0,
                                .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("DetailOctaves", &TerrainComponent::DetailOctaves,
                               {.Label = "Octaves", .Category = "Detail", .Min = 1, .Max = 10}),
                    GPP::Field("Seed", &TerrainComponent::Seed, {.Label = "Seed", .Category = "Detail", .Min = 0, .Max = 100000}),
                }});

            GPP::RegisterComponent<RingComponent>("Ring", GPP::ComponentDescription<RingComponent>{
                .Fields = {
                    GPP::Field("Texture", &RingComponent::Texture, {.Label = "Radial strip", .Kind = GPP::FieldKind::AssetPath,
                                .Asset = GPP::AssetFieldKind::Texture}),
                    GPP::Field("InnerRadius", &RingComponent::InnerRadius,
                               {.Label = "Inner radius", .Min = 0.0, .Speed = 1.0e5, .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("OuterRadius", &RingComponent::OuterRadius,
                               {.Label = "Outer radius", .Min = 0.0, .Speed = 1.0e5, .Dim = GPP::Units::Dimension::Length}),
                    GPP::Field("Opacity", &RingComponent::Opacity, {.Label = "Opacity", .Min = 0.0, .Max = 1.0, .Speed = 0.01}),
                }});

            GPP::RegisterComponent<LightComponent>("Light", GPP::ComponentDescription<LightComponent>{
                .Fields = {
                    GPP::Field("Kind", &LightComponent::Kind, {.Label = "Kind", .Options = {"Directional", "Point"}}),
                    GPP::Field("Color", &LightComponent::Color, {.Label = "Colour", .Kind = GPP::FieldKind::Color}),
                    GPP::Field("Intensity", &LightComponent::Intensity,
                               {.Label = "Irradiance", .Min = 0.0, .Max = 1000.0, .Speed = 0.05}),
                    GPP::Field("Direction", &LightComponent::Direction,
                               {.Label = "Towards the light", .Speed = 0.01, .Kind = GPP::FieldKind::Direction,
                                .VisibleField = "Kind", .VisibleMask = 1u << 0}),
                    GPP::Field("ReferenceDistance", &LightComponent::ReferenceDistance,
                               {.Label = "Reference distance", .Min = 1.0, .Speed = 1.0e9,
                                .VisibleField = "Kind", .VisibleMask = 1u << 1, .Dim = GPP::Units::Dimension::Length}),
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

            GPP::RegisterComponent<NBodyComponent>("NBody", GPP::ComponentDescription<NBodyComponent>{
                .DisplayName = "N-Body",
                .Fields = {
                    GPP::Field("GM", &NBodyComponent::GM,
                               {.Label = "GM", .Min = 0.0, .Speed = 1e6, .Format = "%.9g"}),
                    GPP::Field("Velocity", &NBodyComponent::Velocity,
                               {.Label = "Velocity (m/s)", .Speed = 10.0, .Format = "%.6f"}),
                    GPP::Field("J2", &NBodyComponent::J2,
                               {.Label = "J2", .Category = "Shape", .Min = 0.0, .Speed = 1e-5, .Format = "%.6g"}),
                    GPP::Field("J2Radius", &NBodyComponent::J2Radius,
                               {.Label = "J2 radius", .Category = "Shape", .Min = 0.0, .Speed = 1e3,
                                .Dim = GPP::Units::Dimension::Length}),
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

            GPP::RegisterComponent<EditorFlagsComponent>("EditorFlags", GPP::ComponentDescription<EditorFlagsComponent>{
                .DisplayName = "Editor Flags",
                .Inspectable = false,
                .GraphExposed = false,
                .Fields = {
                    GPP::Field("Hidden", &EditorFlagsComponent::Hidden),
                    GPP::Field("Locked", &EditorFlagsComponent::Locked),
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
