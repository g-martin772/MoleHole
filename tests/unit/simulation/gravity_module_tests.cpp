#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr double kUnitM = Units::kSolarGravitationalRadius;
}

TEST_CASE("GravitySimulationModule accelerates a dynamic body toward a black hole without overflow",
          "[simulation][gravity]")
{
    RegisterComponents();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    auto physics = std::make_shared<PhysicsSimulationModule>(dispatcher, logger);
    GravitySimulationModule gravity(physics, dispatcher, logger);

    Scene scene("GravityTest");
    scene.SetTimeScale(Units::Legacy::kTimeS);
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(blackHole, TransformComponent{.Position = {0.0, 0.0, 0.0}});
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = Units::kSolarMass});

    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {10.0 * kUnitM, 0.0, 0.0}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 0.5 * kUnitM});
    scene.Registry().emplace<RigidBodyComponent>(
        sphere, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = Units::kEarthMass, .EnableGravity = true});
    scene.Registry().emplace<ColliderComponent>(
        sphere, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5 * kUnitM});

    // Module order matches ViewportLayer::StartSimulationFor exactly: gravity before physics.
    gravity.OnInit(scene);
    physics->OnInit(scene);

    const double initialDistance = scene.Registry().get<TransformComponent>(sphere).Position.x;
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 120; ++i)
    {
        gravity.OnTick(scene, dt);
        physics->OnTick(scene, dt);
    }

    const auto& transform = scene.Registry().get<TransformComponent>(sphere);
    CHECK(std::isfinite(transform.Position.x));
    CHECK(std::isfinite(transform.Position.y));
    CHECK(std::isfinite(transform.Position.z));
    // With the old real-G-against-scene-units formula this force overflows to inf/NaN, PhysX
    // rejects it outright, and the sphere never moves at all -- this is the regression this fixes.
    CHECK(transform.Position.x < initialDistance);

    physics->OnShutdown(scene);
}

TEST_CASE("GravitySimulationModule respects a zero GravityMultiplier", "[simulation][gravity]")
{
    RegisterComponents();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    auto physics = std::make_shared<PhysicsSimulationModule>(dispatcher, logger);
    GravitySimulationModule gravity(physics, dispatcher, logger);
    gravity.SetGravityMultiplier(0.0f);

    Scene scene("GravityZeroTest");
    scene.SetTimeScale(Units::Legacy::kTimeS);
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(blackHole, TransformComponent{.Position = {0.0, 0.0, 0.0}});
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = Units::kSolarMass});

    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {10.0 * kUnitM, 0.0, 0.0}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 0.5 * kUnitM});
    scene.Registry().emplace<RigidBodyComponent>(
        sphere, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = Units::kEarthMass, .EnableGravity = true});
    scene.Registry().emplace<ColliderComponent>(
        sphere, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5 * kUnitM});

    gravity.OnInit(scene);
    physics->OnInit(scene);

    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i)
    {
        gravity.OnTick(scene, dt);
        physics->OnTick(scene, dt);
    }

    const auto& transform = scene.Registry().get<TransformComponent>(sphere);
    CHECK(transform.Position.x == Catch::Approx(10.0 * kUnitM).margin(0.01 * kUnitM));

    physics->OnShutdown(scene);
}

TEST_CASE("Restoring a play snapshot after an entity was absorbed doesn't crash", "[simulation][gravity]")
{
    RegisterComponents();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    auto physics = std::make_shared<PhysicsSimulationModule>(dispatcher, logger);
    GravitySimulationModule gravity(physics, dispatcher, logger);

    Scene scene("AbsorptionRestoreTest");
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(blackHole, TransformComponent{.Position = {0.0, 0.0, 0.0}});
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = Units::kSolarMass});

    const auto sphere = scene.CreateEntity("Sphere");
    // Starts already inside the black hole's (radius 2*mass = 2.0) trigger volume so absorption
    // fires on the very first physics step -- no gravity/velocity needed to get it there.
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {0.5 * kUnitM, 0.0, 0.0}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 0.1 * kUnitM});
    scene.Registry().emplace<RigidBodyComponent>(
        sphere, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0, .EnableGravity = false});
    scene.Registry().emplace<ColliderComponent>(
        sphere, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.1 * kUnitM});
    const auto sphereGuid = scene.GuidOf(sphere);

    gravity.OnInit(scene);
    physics->OnInit(scene);

    // Simulates pressing Play: snapshot the scene while the sphere is still alive.
    const Scene playSnapshot = scene.Clone();

    bool absorbed = false;
    for (int i = 0; i < 60 && !absorbed; ++i)
    {
        gravity.OnTick(scene, 1.0f / 60.0f);
        physics->OnTick(scene, 1.0f / 60.0f);
        absorbed = !scene.IsValid(scene.FindByGuid(sphereGuid));
    }
    REQUIRE(absorbed);

    // Simulates pressing Stop: must not crash even though `scene` just destroyed an entity whose
    // guid this snapshot still references (the bug: Registry().destroy() left the guid dangling in
    // Scene's GuidIndex pointing at the now-invalid handle, which SyncInto would then try to reuse).
    CHECK_NOTHROW(Scene::SyncInto(playSnapshot, scene));
    CHECK(scene.IsValid(scene.FindByGuid(sphereGuid)));

    physics->OnShutdown(scene);
}

TEST_CASE("NewtonianAcceleration matches GM/r^2 in every precision", "[simulation][gravity][units]")
{
    const std::array<GravitySource, 1> sources{GravitySource{.PositionM = {0.0, 0.0, 0.0}, .MassKg = Units::kSolarMass}};
    const glm::dvec3 at{1.0 * Units::kAstronomicalUnit, 0.0, 0.0};
    const double expected = Units::kSolarMassParameter / (Units::kAstronomicalUnit * Units::kAstronomicalUnit);

    for (const auto precision : Units::kPrecisions)
    {
        if (!Units::CpuSupports(precision)) continue;
        const auto a = NewtonianAcceleration(precision, sources, at);
        INFO(Units::PrecisionName(precision));
        CHECK(a.x == Catch::Approx(-expected).epsilon(precision == Units::Precision::F32 ? 1e-5 : 1e-12));
        CHECK(a.y == 0.0);
    }
}

TEST_CASE("Single precision loses a small separation between far-away bodies, double keeps it",
          "[simulation][gravity][units]")
{
    // A 100 m offset at 1 AU: representable in double (1e-5 m resolution), not in float (16 km resolution).
    const glm::dvec3 target{Units::kAstronomicalUnit + 100.0, 0.0, 0.0};
    const std::array<GravitySource, 1> sources{GravitySource{.PositionM = {Units::kAstronomicalUnit, 0.0, 0.0},
                                                             .MassKg = 1.0e10}};
    const double exact = Units::kGravitationalConstant * 1.0e10 / (100.0 * 100.0);
    const auto accurate = NewtonianAcceleration(Units::Precision::F64, sources, target);
    const auto lossy = NewtonianAcceleration(Units::Precision::F32, sources, target);
    CHECK(-accurate.x == Catch::Approx(exact).epsilon(1e-6));
    CHECK(std::fabs(-lossy.x - exact) > 1e-3 * exact);
    if constexpr (Units::kHasFloat128)
    {
        const auto wide = NewtonianAcceleration(Units::Precision::F128, sources, target);
        CHECK(-wide.x == Catch::Approx(exact).epsilon(1e-9));
    }
}

TEST_CASE("The gravity module reports and clamps its precision", "[simulation][gravity][units]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    auto physics = std::make_shared<PhysicsSimulationModule>(dispatcher, logger);
    GravitySimulationModule gravity(physics, dispatcher, logger);
    CHECK(gravity.GetPrecision() == Units::Precision::F64);
    gravity.SetPrecision(Units::Precision::F32);
    CHECK(gravity.GetPrecision() == Units::Precision::F32);
}
