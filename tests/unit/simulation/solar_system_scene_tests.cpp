#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    constexpr double kAuM = 149597870700.0;

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    Scene LoadSolarSystem(const char* file = "solar-system.yaml")
    {
        RegisterComponents();
        Scene scene("SolarSystem");
        scene.DeserializeFromYaml(ReadFile(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "templates" / file));
        return scene;
    }

    // Horizons states (tools/fetch_ephemeris.py) in the scene frame: ecliptic (x, y, z) -> scene (x, z, -y), km -> m
    std::map<std::string, std::pair<glm::dvec3, glm::dvec3>> Truth(const std::string& epoch)
    {
        const auto path = std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "tools" / "data" / ("ephemeris_" + epoch + ".json");
        const YAML::Node root = YAML::Load(ReadFile(path));
        std::map<std::string, std::pair<glm::dvec3, glm::dvec3>> result;
        for (const auto& item : root["bodies"])
        {
            const auto p = item.second["position_km"].as<std::vector<double>>();
            const auto v = item.second["velocity_km_s"].as<std::vector<double>>();
            result[item.first.as<std::string>()] = {glm::dvec3(p[0], p[2], -p[1]) * 1000.0,
                                                    glm::dvec3(v[0], v[2], -v[1]) * 1000.0};
        }
        return result;
    }

    std::map<std::string, entt::entity> ByName(Scene& scene)
    {
        std::map<std::string, entt::entity> result;
        for (auto [entity, meta] : scene.Registry().view<MetadataComponent>().each()) result[meta.Name] = entity;
        return result;
    }

    glm::dvec3 PositionOf(Scene& scene, const entt::entity entity)
    {
        return scene.Registry().get<TransformComponent>(entity).Position;
    }

    NBodySystem GatherSystem(Scene& scene)
    {
        NBodySystem system;
        for (auto [entity, body, transform] : scene.Registry().view<const NBodyComponent, const TransformComponent>().each())
        {
            system.Add(transform.Position, body.Velocity, body.GM);
        }
        return system;
    }
}

TEST_CASE("The solar-system template holds the bodies at the ephemeris epoch", "[simulation][scene][nbody][solar-system]")
{
    auto scene = LoadSolarSystem();
    const auto names = ByName(scene);
    const auto& registry = scene.Registry();

    std::size_t bodies = 0, spheres = 0;
    for (auto [entity, body] : registry.view<const NBodyComponent>().each())
    {
        ++bodies;
        INFO(registry.get<MetadataComponent>(entity).Name);
        CHECK(registry.all_of<TransformComponent>(entity));
        if (registry.all_of<SphereComponent>(entity)) ++spheres;
        CHECK(body.GM >= 0.0);
    }
    CHECK(bodies >= 74);
    CHECK(spheres <= 62); // the renderer draws at most 64 spheres: the biggest bodies get one

    for (const char* name : {"Sun", "Mercury", "Venus", "Earth", "Moon", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune",
                             "Pluto", "Charon", "Io", "Europa", "Ganymede", "Callisto", "Titan", "Triton", "Ceres", "Eris"})
    {
        CHECK(names.contains(name));
    }

    // Distances from the Sun (AU) on 2026-10-22; a planet is within its orbital range
    const glm::dvec3 sun = PositionOf(scene, names.at("Sun"));
    const auto au = [&](const char* name) { return glm::length(PositionOf(scene, names.at(name)) - sun) / kAuM; };
    CHECK(au("Earth") == Approx(0.995).margin(0.005));      // Earth is near the October perihelion side
    CHECK(au("Mercury") == Approx(0.387).margin(0.09));
    CHECK(au("Jupiter") == Approx(5.2).margin(0.3));
    CHECK(au("Saturn") == Approx(9.5).margin(0.6));
    CHECK(au("Neptune") == Approx(30.1).margin(0.4));
    CHECK(au("Pluto") == Approx(35.6).margin(0.2));
    CHECK(au("Voyager 1") > 160.0);

    // Moons sit where they belong: the Moon 0.0026 AU from Earth, Io 421,700 km from Jupiter
    CHECK(glm::length(PositionOf(scene, names.at("Moon")) - PositionOf(scene, names.at("Earth"))) == Approx(3.84e8).margin(4.5e7));
    CHECK(glm::length(PositionOf(scene, names.at("Io")) - PositionOf(scene, names.at("Jupiter"))) == Approx(4.217e8).margin(5e6));
}

TEST_CASE("The solar-system template is a barycentric frame: total momentum vanishes", "[simulation][scene][nbody][solar-system]")
{
    auto scene = LoadSolarSystem();
    const auto system = GatherSystem(scene);
    double totalMu = 0.0, weightedSpeed = 0.0;
    glm::dvec3 centre{0.0};
    for (std::size_t i = 0; i < system.Size(); ++i)
    {
        totalMu += system.Mu[i];
        weightedSpeed += system.Mu[i] * glm::length(system.Velocity[i]);
        centre += system.Mu[i] * system.Position[i];
    }
    // The residual is the bodies the scene does not contain (the rest of the asteroid belt, small moons)
    CHECK(glm::length(NBodyMomentum(system)) / weightedSpeed < 1e-4);
    CHECK(glm::length(centre / totalMu) < 5e6); // m, against the 1.3e9 m offset of the Sun from the barycentre
}

TEST_CASE("Rotation: IAU rates, retrograde bodies and tidal locking", "[simulation][scene][solar-system]")
{
    auto scene = LoadSolarSystem();
    const auto names = ByName(scene);
    const auto& registry = scene.Registry();
    const auto period = [&](const char* name) { return registry.get<SurfaceComponent>(names.at(name)).RotationPeriod; };

    CHECK(period("Earth") == Approx(86164.1).epsilon(1e-5));          // sidereal day
    CHECK(period("Mars") == Approx(88642.7).epsilon(1e-4));            // 24h 37m 22.7s
    CHECK(period("Jupiter") == Approx(35730.0).epsilon(2e-4));         // System III, 9h 55m 30s
    CHECK(period("Saturn") == Approx(38361.6).epsilon(1e-4));
    CHECK(period("Venus") < 0.0);                                       // retrograde, 243 d
    CHECK(std::abs(period("Venus")) == Approx(243.0185 * 86400.0).epsilon(1e-4));
    CHECK(period("Uranus") < 0.0);
    CHECK(period("Triton") < 0.0);
    // The Moon, Io and Titan keep one face to their planet: the rotation period is the orbital period
    CHECK(period("Moon") == Approx(27.321661 * 86400.0).epsilon(1e-5));
    CHECK(period("Io") == Approx(1.769138 * 86400.0).epsilon(1e-4));
    CHECK(period("Titan") == Approx(15.945421 * 86400.0).epsilon(1e-4));
    // Earth's axis is tilted 23.44 deg from the ecliptic normal (+Y)
    const glm::vec3 axis = glm::normalize(registry.get<SphereComponent>(names.at("Earth")).SpinAxis);
    CHECK(glm::degrees(std::acos(axis.y)) == Approx(23.44).margin(0.02));
}

TEST_CASE("One day of the solar system conserves the Newtonian energy and the momentum", "[simulation][scene][nbody][solar-system]")
{
    auto scene = LoadSolarSystem();
    const auto before = GatherSystem(scene);
    const double energy = NBodyEnergy(before);
    const glm::dvec3 momentum = NBodyMomentum(before);

    NBodySimulationModule module;
    constexpr float dt = 1.0f / 60.0f;
    const int ticks = static_cast<int>(std::round(86400.0 / (static_cast<double>(dt) * scene.Metadata().TimeScale)));
    for (int i = 0; i < ticks; ++i) module.OnTick(scene, dt);

    const auto after = GatherSystem(scene);
    // The 1PN and J2 parts are not in the Newtonian energy, so it wanders at their size (a few 1e-9 over a day)
    CHECK(std::abs(NBodyEnergy(after) / energy - 1.0) < 1e-8);
    double weightedSpeed = 0.0;
    for (std::size_t i = 0; i < after.Size(); ++i) weightedSpeed += after.Mu[i] * glm::length(after.Velocity[i]);
    CHECK(glm::length(NBodyMomentum(after) - momentum) / weightedSpeed < 1e-9);
    CHECK(module.LastSteps() >= 1);
}

// Slow in a Debug build (minutes): run it with the tag, "molehole_tests [slow]"
TEST_CASE("Thirty days of the solar system against JPL Horizons", "[.][simulation][scene][nbody][solar-system][slow]")
{    auto scene = LoadSolarSystem();
    const auto names = ByName(scene);
    const auto truth = Truth("2026-11-21");

    NBodySimulationModule module;
    constexpr float dt = 1.0f / 60.0f;
    const double seconds = 30.0 * 86400.0;
    const int ticks = static_cast<int>(std::round(seconds / (static_cast<double>(dt) * scene.Metadata().TimeScale)));
    for (int i = 0; i < ticks; ++i) module.OnTick(scene, dt);

    const auto error = [&](const char* name, const char* id)
    {
        const double e = glm::length(PositionOf(scene, names.at(name)) - truth.at(id).first);
        UNSCOPED_INFO(std::format("{} is {:.1f} m off", name, e));
        return e;
    };
    const auto relative = [&](const char* name, const char* centre, const char* id, const char* centreId)
    {
        const glm::dvec3 simulated = PositionOf(scene, names.at(name)) - PositionOf(scene, names.at(centre));
        const double e = glm::length(simulated - (truth.at(id).first - truth.at(centreId).first));
        UNSCOPED_INFO(std::format("{} is {:.1f} m off", name, e));
        return e;
    };

    // Positions in the barycentric frame, metres. The remaining error is what the model leaves out (asteroid
    // perturbations, tides, the full set of small bodies), and the 1e-8 truncation of the 1PN-limited initial data.
    CHECK(error("Sun", "sun") < 5e3);
    CHECK(error("Mercury", "mercury") < 2e4);
    CHECK(error("Venus", "venus") < 2e4);
    CHECK(error("Earth", "earth") < 2e4);
    CHECK(error("Mars", "mars") < 2e4);
    CHECK(error("Jupiter", "jupiter") < 2e4);
    CHECK(error("Saturn", "saturn") < 2e4);
    CHECK(error("Neptune", "neptune") < 2e4);
    // Moons relative to their planet: a month of the Moon's orbit, three of Io's
    CHECK(relative("Moon", "Earth", "moon", "earth") < 1e3);
    CHECK(relative("Io", "Jupiter", "io", "jupiter") < 2e5);
    CHECK(relative("Titan", "Saturn", "titan", "saturn") < 2e3);
    CHECK(relative("Phobos", "Mars", "phobos", "mars") < 1e6);   // Mars' C22 and higher terms are not modelled
    CHECK(relative("Charon", "Pluto", "charon", "pluto") < 1e5);
}

TEST_CASE("The black-hole template is the same system with the Sun replaced by a 1 solar-mass hole", "[simulation][scene][nbody][solar-system]")
{
    auto real = LoadSolarSystem();
    auto scene = LoadSolarSystem("solar-system-black-hole.yaml");
    const auto names = ByName(scene);
    const auto realNames = ByName(real);
    const auto& registry = scene.Registry();

    REQUIRE(names.contains("Black hole"));
    CHECK_FALSE(names.contains("Sun"));
    const auto hole = names.at("Black hole");
    REQUIRE(registry.all_of<BlackHoleComponent>(hole));
    CHECK_FALSE(registry.all_of<StarComponent>(hole));
    CHECK(registry.get<NBodyComponent>(hole).GM == Approx(1.32712440041279419e20).epsilon(1e-12));
    CHECK(registry.get<BlackHoleComponent>(hole).Mass * 6.6743e-11 == Approx(1.32712440041279419e20).epsilon(1e-6));
    CHECK(registry.get<BlackHoleComponent>(hole).Spin == 0.0);

    // Same gravity, same orbits: every other body keeps its state
    CHECK(PositionOf(scene, hole) == PositionOf(real, realNames.at("Sun")));
    for (const char* name : {"Mercury", "Earth", "Moon", "Jupiter", "Io", "Neptune"})
    {
        CHECK(PositionOf(scene, names.at(name)) == PositionOf(real, realNames.at(name)));
        CHECK(registry.get<NBodyComponent>(names.at(name)).Velocity == real.Registry().get<NBodyComponent>(realNames.at(name)).Velocity);
    }
    // The hole is the light: a point light of the Sun's irradiance at 1 AU
    const auto& light = registry.get<LightComponent>(hole);
    CHECK(light.Kind == BodyLightKind::Point);
    CHECK(light.ReferenceDistance == Approx(kAuM));
}
