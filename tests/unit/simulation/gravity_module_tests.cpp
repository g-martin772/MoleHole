#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

TEST_CASE("GravitySimulationModule accelerates a dynamic body toward a black hole without overflow",
          "[simulation][gravity]")
{
    RegisterComponents();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    auto physics = std::make_shared<PhysicsSimulationModule>(dispatcher, logger);
    GravitySimulationModule gravity(physics, dispatcher, logger);

    Scene scene("GravityTest");
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(blackHole, TransformComponent{.Position = {0.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = 1.0f});

    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {10.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 0.5f});
    scene.Registry().emplace<RigidBodyComponent>(
        sphere, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 5.972e24f, .EnableGravity = true});
    scene.Registry().emplace<ColliderComponent>(
        sphere, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});

    // Module order matches ViewportLayer::StartSimulationFor exactly: gravity before physics.
    gravity.OnInit(scene);
    physics->OnInit(scene);

    const float initialDistance = scene.Registry().get<TransformComponent>(sphere).Position.x;
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
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(blackHole, TransformComponent{.Position = {0.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = 1.0f});

    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {10.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 0.5f});
    scene.Registry().emplace<RigidBodyComponent>(
        sphere, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 5.972e24f, .EnableGravity = true});
    scene.Registry().emplace<ColliderComponent>(
        sphere, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});

    gravity.OnInit(scene);
    physics->OnInit(scene);

    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i)
    {
        gravity.OnTick(scene, dt);
        physics->OnTick(scene, dt);
    }

    const auto& transform = scene.Registry().get<TransformComponent>(sphere);
    CHECK(transform.Position.x == Catch::Approx(10.0f).margin(0.01f));

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
    scene.Registry().emplace<TransformComponent>(blackHole, TransformComponent{.Position = {0.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = 1.0f});

    const auto sphere = scene.CreateEntity("Sphere");
    // Starts already inside the black hole's (radius 2*mass = 2.0) trigger volume so absorption
    // fires on the very first physics step -- no gravity/velocity needed to get it there.
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {0.5f, 0.0f, 0.0f}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 0.1f});
    scene.Registry().emplace<RigidBodyComponent>(
        sphere, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0f, .EnableGravity = false});
    scene.Registry().emplace<ColliderComponent>(
        sphere, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.1f});
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
