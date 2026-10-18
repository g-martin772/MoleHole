#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    entt::entity AddEntity(Scene& scene, const std::string& name, const glm::dvec3& position)
    {
        const auto entity = scene.CreateEntity(name);
        scene.Registry().emplace_or_replace<TransformComponent>(entity, TransformComponent{.Position = position});
        return entity;
    }

    RenderFrame UnitFrame()
    {
        RenderFrame frame;
        frame.LengthUnitM = 1.0;
        return frame;
    }

    float Luminance(const glm::vec3& rgb) { return glm::dot(rgb, glm::vec3(0.2126f, 0.7152f, 0.0722f)); }
}

TEST_CASE("The Sun as a Star has the solar luminosity, a warm Planck colour and a consistent surface radiance", "[star][lighting]")
{
    const StarComponent sun;
    CHECK(StarLuminosity(sun) == Approx(3.83e26).epsilon(0.01));
    CHECK(StarLuminosity(sun) == Approx(Units::kSolarLuminosity).epsilon(0.005));

    const auto colour = StarColor(sun);
    CHECK(Luminance(colour) == Approx(1.0f).epsilon(1e-3));
    CHECK(colour.r > colour.g);
    CHECK(colour.g > colour.b);
    CHECK(colour.b > 0.6f);

    // L = 4 pi R^2 * pi * (sigma T^4 / pi)
    const double pi = static_cast<double>(Units::Pi());
    CHECK(StarLuminosity(sun) == Approx(4.0 * pi * sun.Radius * sun.Radius * pi * StarSurfaceRadiance(sun)).epsilon(1e-12));

    CHECK(StarLimbMean(sun) == Approx(0.8));
    const double efficacy = Radiometry::BlackbodyLuminousEfficacy(sun.Teff);
    CHECK(efficacy > 85.0);
    CHECK(efficacy < 100.0);
    CHECK(StarLuminousFraction(sun) == Approx(efficacy / 683.0));
    // the disc mean of the drawn radiance, in image units, is the luminance of sigma T^4 / pi over 203 cd/m^2
    CHECK(StarImageRadiance(sun) * StarLimbMean(sun)
          == Approx(StarSurfaceRadiance(sun) * efficacy / Radiometry::kHdrReferenceWhite).epsilon(1e-9));
}

TEST_CASE("A star lights a surface with E = L / (4 pi d^2) in the light buffer", "[star][lighting]")
{
    RegisterComponents();
    Scene scene;
    const auto star = AddEntity(scene, "Sun", {Units::kAstronomicalUnit, 0.0, 0.0});
    scene.Registry().emplace<StarComponent>(star);

    DiskLightCache cache;
    RenderSettings settings;
    settings.DiskLight = false;
    const auto lighting = BuildMeshLighting(scene, UnitFrame(), settings, cache, false);
    REQUIRE(lighting.Stars == 1);
    REQUIRE(lighting.Lights.Count == 1);
    CHECK_FALSE(lighting.DefaultKeyLight);
    const auto& light = lighting.Lights.Lights[0];
    CHECK(light.Kind == static_cast<std::uint32_t>(LightKind::Point));
    CHECK(light.Flux == Approx(3.828e26).epsilon(0.005));
    CHECK(light.Radius == Approx(Units::kSolarRadius).epsilon(1e-6));

    // irradiance at the origin, back in bolometric terms: Y * 683 / K(T) = E
    const double distance = glm::length(glm::dvec3(light.Position));
    CHECK(distance == Approx(Units::kAstronomicalUnit).epsilon(1e-6));
    const double intensity = static_cast<double>(Luminance(light.Color));
    const double photometricIrradiance = intensity / (distance * distance) * Radiometry::kLuminousEfficacyMax;
    const double bolometric = photometricIrradiance / Radiometry::BlackbodyLuminousEfficacy(Units::kSolarEffectiveTemperature);
    CHECK(bolometric == Approx(1361.0).epsilon(0.01));
}

TEST_CASE("A scene without lights gets the default key light of one image unit", "[lighting]")
{
    RegisterComponents();
    Scene scene;
    DiskLightCache cache;
    RenderSettings settings;
    settings.DiskLight = false;
    const auto lighting = BuildMeshLighting(scene, UnitFrame(), settings, cache, true);
    REQUIRE(lighting.DefaultKeyLight);
    REQUIRE(lighting.Lights.Count == 1);
    CHECK(lighting.Lights.DirectionalCount == 1);
    const auto& key = lighting.Lights.Lights[0];
    CHECK(key.Kind == static_cast<std::uint32_t>(LightKind::Directional));
    CHECK(key.Color.x * lighting.Params.RadianceToImage == Approx(1.0f));
    CHECK(glm::length(key.Direction) == Approx(1.0f));
    CHECK(glm::dot(key.Direction, glm::normalize(glm::vec3(1.0f))) == Approx(-1.0f));
    CHECK(lighting.Params.ShadowsEnabled == 1);
    CHECK(lighting.Params.SkyRadiance * lighting.Params.RadianceToImage == Approx(0.03f));
    CHECK(DefaultKeyIrradiance() * RadianceToImage() == Approx(1.0));

    settings.Shadows = false;
    CHECK(BuildMeshLighting(scene, UnitFrame(), settings, cache, true).Params.ShadowsEnabled == 0);
    settings.Shadows = true;
    CHECK(BuildMeshLighting(scene, UnitFrame(), settings, cache, false).Params.ShadowsEnabled == 0);
}

TEST_CASE("Scene lights replace the default key light and keep the buffer order", "[lighting]")
{
    RegisterComponents();
    Scene scene;
    const auto lamp = AddEntity(scene, "Lamp", {0.0, 2.0, 0.0});
    scene.Registry().emplace<PointLightComponent>(lamp, PointLightComponent{.Intensity = 50.0});
    const auto star = AddEntity(scene, "Star", {0.0, 0.0, 1.0e9});
    scene.Registry().emplace<StarComponent>(star);

    DiskLightCache cache;
    RenderSettings settings;
    settings.DiskLight = false;
    auto frame = UnitFrame();
    frame.OriginM = {0.0, 1.0, 0.0};
    const auto lighting = BuildMeshLighting(scene, frame, settings, cache, false);
    CHECK_FALSE(lighting.DefaultKeyLight);
    REQUIRE(lighting.Lights.Count == 2);
    CHECK(lighting.Lights.Lights[0].Position == glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(lighting.Lights.Lights[0].Color.x == Approx(50.0f));
    CHECK(lighting.Lights.Lights[1].Position.z == Approx(1.0e9f));
}

TEST_CASE("The disk light radiates eta Mdot c^2 = ratio L_Edd, calibrated to the displayed disk", "[lighting][disk-light]")
{
    RegisterComponents();
    const double mass = 10.0 * Units::kSolarMass;
    Scene scene;
    const auto hole = AddEntity(scene, "BH", {0.0, 0.0, 0.0});
    scene.Registry().emplace<BlackHoleComponent>(hole, BlackHoleComponent{.Mass = mass, .Spin = 0.0});

    RenderSettings settings;
    settings.Solution = Spacetime::Schwarzschild;
    settings.DiskAccretionRate = 0.1f;
    settings.DiskDisplayTemperature = 0.0f;
    DiskLightCache cache;
    const auto info = ComputeDiskLight(scene.Registry().get<BlackHoleComponent>(hole), {0.0, 0.0, 0.0}, settings, cache);
    REQUIRE(info.Valid);

    CHECK(EddingtonLuminosity(Units::kSolarMass) == Approx(1.26e31).epsilon(0.01));
    CHECK(info.Efficiency == Approx(1.0 - std::sqrt(8.0 / 9.0)).epsilon(1e-9));
    CHECK(info.LuminosityW == Approx(static_cast<double>(settings.DiskAccretionRate) * EddingtonLuminosity(mass)).epsilon(1e-12));
    CHECK(info.AccretionRateKgPerS * info.Efficiency * Units::kSpeedOfLight * Units::kSpeedOfLight
          == Approx(info.LuminosityW).epsilon(1e-12));
    CHECK(DiskLuminosityW(0.1, 2.0) == Approx(0.2 * Units::kSpeedOfLight * Units::kSpeedOfLight));

    CHECK(info.PeakTemperatureK > 1.0e6);
    CHECK(info.InnerRadiusM == Approx(6.0 * Units::GravitationalRadius(mass)).epsilon(1e-6));
    CHECK(info.HorizonRadiusM == Approx(2.0 * Units::GravitationalRadius(mass)).epsilon(1e-6));
    const double peakRadiance = Units::kStefanBoltzmann * std::pow(info.PeakTemperatureK, 4.0) / static_cast<double>(Units::Pi());
    CHECK(info.Calibration * peakRadiance == Approx(kDiskReferenceLuminance).epsilon(1e-9));
    CHECK(info.ColorTemperatureK == Approx(info.PeakTemperatureK));

    // power doubles with the accretion rate; the profile is not recomputed for the same inputs
    const int misses = cache.Misses;
    CHECK(ComputeDiskLight(scene.Registry().get<BlackHoleComponent>(hole), {0.0, 0.0, 0.0}, settings, cache).Valid);
    CHECK(cache.Misses == misses);
    settings.DiskAccretionRate = 0.2f;
    const auto doubled = ComputeDiskLight(scene.Registry().get<BlackHoleComponent>(hole), {0.0, 0.0, 0.0}, settings, cache);
    CHECK(doubled.LuminosityW == Approx(2.0 * info.LuminosityW).epsilon(1e-6));

    settings.DiskDisplayTemperature = 6500.0f;
    CHECK(ComputeDiskLight(scene.Registry().get<BlackHoleComponent>(hole), {0.0, 0.0, 0.0}, settings, cache).ColorTemperatureK
          == Approx(6500.0));

    settings.DiskAccretionRate = 0.0f;
    CHECK_FALSE(ComputeDiskLight(scene.Registry().get<BlackHoleComponent>(hole), {0.0, 0.0, 0.0}, settings, cache).Valid);
}

TEST_CASE("The disk light is a Lambertian point at the primary hole and follows the setting", "[lighting][disk-light]")
{
    RegisterComponents();
    Scene scene;
    const auto light = AddEntity(scene, "BH", {100.0, 0.0, 0.0});
    scene.Registry().emplace<BlackHoleComponent>(light, BlackHoleComponent{.Mass = 10.0 * Units::kSolarMass, .Spin = 0.5,
                                                                          .SpinAxis = {0.0f, 0.0f, 1.0f}});
    const auto smaller = AddEntity(scene, "Small", {5.0, 0.0, 0.0});
    scene.Registry().emplace<BlackHoleComponent>(smaller, BlackHoleComponent{.Mass = 1.0 * Units::kSolarMass});

    DiskLightCache cache;
    RenderSettings settings;
    const auto lighting = BuildMeshLighting(scene, UnitFrame(), settings, cache, false);
    REQUIRE(lighting.Disk.Valid);
    REQUIRE(lighting.Lights.Count == 2); // default key + disk
    const auto& disk = lighting.Lights.Lights[1];
    CHECK((disk.Flags & kLightFlagDiskLobe) != 0);
    CHECK(disk.Position == glm::vec3(100.0f, 0.0f, 0.0f));
    CHECK(disk.Direction == glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(disk.Flux == Approx(static_cast<float>(lighting.Disk.LuminosityW)).epsilon(1e-5));
    CHECK(disk.HalfExtent.x == Approx(static_cast<float>(lighting.Disk.InnerRadiusM)));
    // peak intensity along the axis: I = L / 2 pi, shown through the disk calibration
    const double expected = lighting.Disk.LuminosityW / (2.0 * static_cast<double>(Units::Pi())) * lighting.Disk.Calibration
                            / RadianceToImage();
    CHECK(static_cast<double>(Luminance(disk.Color)) == Approx(expected).epsilon(1e-4));

    settings.DiskLight = false;
    const auto off = BuildMeshLighting(scene, UnitFrame(), settings, cache, false);
    CHECK_FALSE(off.Disk.Valid);
    CHECK(off.Lights.Count == 1);
}

TEST_CASE("The new render settings are registered with their defaults", "[rendering][settings][lighting]")
{
    const auto& registry = RenderSettingRegistry();
    for (const char* key : {"lighting.disk_light", "lighting.shadows", "lighting.sky_radiance", "post.camera_aperture",
                            "post.camera_shutter", "post.camera_iso"})
    {
        INFO(key);
        CHECK(registry.Find(key) != nullptr);
    }
    const auto* mode = registry.Find("post.exposure_mode");
    REQUIRE(mode != nullptr);
    CHECK(mode->Meta.Options == std::vector<std::string>{"Manual", "Auto", "Physical"});

    const RenderSettings defaults;
    CHECK(defaults.Exposure == ExposureMode::Manual);
    CHECK(defaults.ExposureEv == 0.0f);
    CHECK(defaults.DiskLight);
    CHECK(defaults.Shadows);
    CHECK(defaults.SkyRadiance * RadianceToImage() == Approx(0.03));
    CHECK(registry.ToText(defaults, *registry.Find("lighting.disk_light")) == "true");
}

TEST_CASE("The physical camera maps sunny 16 to a unit-order exposure of an illuminated white surface", "[exposure]")
{
    const Radiometry::PhysicalCamera camera{16.0, 1.0 / 100.0, 100.0};
    const double ev100 = Radiometry::Ev100(camera);
    CHECK(ev100 == Approx(std::log2(256.0 * 100.0)));
    // image value of a white Lambertian under E: (E / pi) * 683 / 203, times the exposure of the stage
    const double exposure = Radiometry::ExposureScale(camera) * Radiometry::kHdrReferenceWhite;
    const double illuminance = 1.27e5; // lx, the Sun at 1 AU in clear sky
    const double luminance = illuminance / static_cast<double>(Units::Pi());
    const double displayed = luminance / Radiometry::kHdrReferenceWhite * exposure;
    CHECK(displayed > 0.5);
    CHECK(displayed < 2.0);
}
