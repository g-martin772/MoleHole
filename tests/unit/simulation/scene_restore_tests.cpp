#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

// Mirrors the Play->Stop restore path in ViewportLayer::CheckPendingSceneSwitch: Scene::SyncInto
// only restores ECS component data, which is not enough on its own for a Dynamic rigid body --
// PhysicsSimulationModule treats the live PxRigidActor as authoritative over TransformComponent and
// overwrites it every tick (WriteBackTransforms), so without also resetting the actor's pose/velocity,
// the very next tick snaps the "restored" object right back toward wherever it drifted to during play.
TEST_CASE("Restoring a play snapshot resets the live PhysX actor, not just the ECS transform",
          "[simulation][physics][playstop]")
{
    RegisterComponents();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    auto physics = std::make_shared<PhysicsSimulationModule>(dispatcher, logger);

    Scene scene("RestoreTest");
    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {0.0f, 10.0f, 0.0f}});
    scene.Registry().emplace<VelocityComponent>(sphere, VelocityComponent{.Linear = {0.0f, -1.0f, 0.0f}});
    scene.Registry().emplace<RigidBodyComponent>(
        sphere, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0f});
    scene.Registry().emplace<ColliderComponent>(
        sphere, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});

    physics->OnInit(scene);
    constexpr float dt = 1.0f / 60.0f;

    // Simulates pressing Play: snapshot while the sphere is still at its starting position.
    const Scene playSnapshot = scene.Clone();
    const float snapshotY = scene.Registry().get<TransformComponent>(sphere).Position.y;

    // "Played" for a while -- the sphere drifts well away from the snapshot.
    for (int i = 0; i < 60; ++i)
    {
        physics->OnTick(scene, dt);
    }
    const float playedY = scene.Registry().get<TransformComponent>(sphere).Position.y;
    REQUIRE(playedY < snapshotY - 0.1f);

    // Simulates pressing Stop: restore the ECS data AND the live actor (the fix).
    Scene::SyncInto(playSnapshot, scene);
    for (auto [entity, transform] : scene.Registry().view<const TransformComponent>().each())
    {
        auto* actor = physics->FindActor(entity);
        REQUIRE(actor != nullptr);
        actor->setGlobalPose(physx::PxTransform(
            physx::PxVec3(transform.Position.x, transform.Position.y, transform.Position.z),
            physx::PxQuat(transform.Rotation.x, transform.Rotation.y, transform.Rotation.z, transform.Rotation.w)));
        if (auto* dynamic = actor->is<physx::PxRigidDynamic>())
        {
            const auto* velocity = scene.Registry().try_get<const VelocityComponent>(entity);
            const auto linear = velocity ? velocity->Linear : glm::vec3(0.0f);
            dynamic->setLinearVelocity(physx::PxVec3(linear.x, linear.y, linear.z));
            dynamic->setAngularVelocity(physx::PxVec3(0.0f));
        }
    }

    // One more tick, as happens the moment the sim resumes (or even just the next sim-thread
    // iteration) -- without resetting the actor, WriteBackTransforms immediately clobbers the
    // restore right back toward `playedY`.
    physics->OnTick(scene, dt);
    const float restoredY = scene.Registry().get<TransformComponent>(sphere).Position.y;

    // restoredY is one tick's worth of (correctly reset) velocity past the exact snapshot value --
    // expected, since the fix resets pose+velocity and then this test steps once more.
    CHECK(restoredY == Catch::Approx(snapshotY).margin(0.1f));
    CHECK(restoredY > playedY + 0.5f);

    physics->OnShutdown(scene);
}

TEST_CASE("Without resetting the actor, restoring only the ECS transform does not stick",
          "[simulation][physics][playstop]")
{
    // Negative control proving the bug this fixes is real, not a strawman: SyncInto alone (no actor
    // reset) must NOT survive a subsequent tick.
    RegisterComponents();
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    auto physics = std::make_shared<PhysicsSimulationModule>(dispatcher, logger);

    Scene scene("RestoreRegressionTest");
    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {0.0f, 10.0f, 0.0f}});
    scene.Registry().emplace<VelocityComponent>(sphere, VelocityComponent{.Linear = {0.0f, -1.0f, 0.0f}});
    scene.Registry().emplace<RigidBodyComponent>(
        sphere, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0f});
    scene.Registry().emplace<ColliderComponent>(
        sphere, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});

    physics->OnInit(scene);
    constexpr float dt = 1.0f / 60.0f;

    const Scene playSnapshot = scene.Clone();
    const float snapshotY = scene.Registry().get<TransformComponent>(sphere).Position.y;

    for (int i = 0; i < 60; ++i)
    {
        physics->OnTick(scene, dt);
    }
    const float playedY = scene.Registry().get<TransformComponent>(sphere).Position.y;

    Scene::SyncInto(playSnapshot, scene); // deliberately WITHOUT the actor-reset step
    physics->OnTick(scene, dt);
    const float afterTickY = scene.Registry().get<TransformComponent>(sphere).Position.y;

    // The restore gets clobbered: it ends up back near where "played" state had drifted to, not
    // near the snapshot.
    CHECK(afterTickY != Catch::Approx(snapshotY).margin(0.1f));
    CHECK(afterTickY == Catch::Approx(playedY).margin(0.2f));

    physics->OnShutdown(scene);
}
