#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    entt::entity MakeMeshEntity(Scene& scene, glm::vec3 position)
    {
        const auto entity = scene.CreateEntity("Mesh");
        scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = position});
        scene.Registry().emplace<MeshComponent>(entity, MeshComponent{.AssetPath = "assets/models/test.gltf"});
        return entity;
    }

    entt::entity MakeSphereEntity(Scene& scene, glm::vec3 position)
    {
        const auto entity = scene.CreateEntity("Sphere");
        scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = position});
        scene.Registry().emplace<SphereComponent>(entity, SphereComponent{.Radius = 1.0f});
        return entity;
    }
}

TEST_CASE("ObjectPathTracker records mesh and sphere positions into separate per-entity histories",
          "[simulation][object-paths]")
{
    RegisterComponents();
    Scene scene("ObjectPathsTest");
    const auto mesh = MakeMeshEntity(scene, {0.0f, 0.0f, 0.0f});
    const auto sphere = MakeSphereEntity(scene, {5.0f, 0.0f, 0.0f});
    const auto meshGuid = scene.GuidOf(mesh);
    const auto sphereGuid = scene.GuidOf(sphere);

    ObjectPathTracker tracker;
    tracker.RecordPositions(scene);

    scene.Registry().get<TransformComponent>(mesh).Position = {1.0f, 0.0f, 0.0f};
    scene.Registry().get<TransformComponent>(sphere).Position = {5.0f, 1.0f, 0.0f};
    tracker.RecordPositions(scene);

    REQUIRE(tracker.MeshHistories().contains(meshGuid));
    REQUIRE(tracker.SphereHistories().contains(sphereGuid));
    CHECK(tracker.MeshHistories().at(meshGuid).size() == 2);
    CHECK(tracker.SphereHistories().at(sphereGuid).size() == 2);
    CHECK(tracker.MeshHistories().at(meshGuid).front() == glm::vec3(0.0f, 0.0f, 0.0f));
    CHECK(tracker.MeshHistories().at(meshGuid).back() == glm::vec3(1.0f, 0.0f, 0.0f));
    // Each category only holds its own entities.
    CHECK_FALSE(tracker.MeshHistories().contains(sphereGuid));
    CHECK_FALSE(tracker.SphereHistories().contains(meshGuid));
}

TEST_CASE("ObjectPathTracker evicts the oldest position once MaxHistorySize is exceeded (FIFO)",
          "[simulation][object-paths]")
{
    RegisterComponents();
    Scene scene("ObjectPathsEvictionTest");
    const auto mesh = MakeMeshEntity(scene, {0.0f, 0.0f, 0.0f});
    const auto meshGuid = scene.GuidOf(mesh);

    ObjectPathTracker tracker;
    tracker.SetMaxHistorySize(3);
    REQUIRE(tracker.MaxHistorySize() == 3);

    for (int i = 0; i < 5; ++i)
    {
        scene.Registry().get<TransformComponent>(mesh).Position = {static_cast<float>(i), 0.0f, 0.0f};
        tracker.RecordPositions(scene);
    }

    const auto& history = tracker.MeshHistories().at(meshGuid);
    REQUIRE(history.size() == 3);
    // The two oldest points (x=0, x=1) should have been popped from the front, leaving x=2,3,4.
    CHECK(history[0] == glm::vec3(2.0f, 0.0f, 0.0f));
    CHECK(history[1] == glm::vec3(3.0f, 0.0f, 0.0f));
    CHECK(history[2] == glm::vec3(4.0f, 0.0f, 0.0f));
}

TEST_CASE("ObjectPathTracker::Clear empties both histories", "[simulation][object-paths]")
{
    RegisterComponents();
    Scene scene("ObjectPathsClearTest");
    MakeMeshEntity(scene, {0.0f, 0.0f, 0.0f});
    MakeSphereEntity(scene, {1.0f, 0.0f, 0.0f});

    ObjectPathTracker tracker;
    tracker.RecordPositions(scene);
    REQUIRE_FALSE(tracker.MeshHistories().empty());
    REQUIRE_FALSE(tracker.SphereHistories().empty());

    tracker.Clear();
    CHECK(tracker.MeshHistories().empty());
    CHECK(tracker.SphereHistories().empty());
}

TEST_CASE("ObjectPathTracker never tracks an entity with neither MeshComponent nor SphereComponent",
          "[simulation][object-paths]")
{
    RegisterComponents();
    Scene scene("ObjectPathsUntrackedTest");
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(blackHole, TransformComponent{.Position = {0.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = 1.0f});
    const auto blackHoleGuid = scene.GuidOf(blackHole);

    ObjectPathTracker tracker;
    tracker.RecordPositions(scene);

    CHECK(tracker.MeshHistories().empty());
    CHECK(tracker.SphereHistories().empty());
    CHECK_FALSE(tracker.MeshHistories().contains(blackHoleGuid));
    CHECK_FALSE(tracker.SphereHistories().contains(blackHoleGuid));
}

TEST_CASE("ObjectPathTracker doesn't crash and stops growing a destroyed entity's history",
          "[simulation][object-paths]")
{
    RegisterComponents();
    Scene scene("ObjectPathsDestroyedTest");
    const auto sphere = MakeSphereEntity(scene, {0.0f, 0.0f, 0.0f});
    const auto sphereGuid = scene.GuidOf(sphere);

    ObjectPathTracker tracker;
    tracker.RecordPositions(scene);
    tracker.RecordPositions(scene);
    REQUIRE(tracker.SphereHistories().at(sphereGuid).size() == 2);

    scene.DestroyEntity(sphere);

    CHECK_NOTHROW(tracker.RecordPositions(scene));
    CHECK_NOTHROW(tracker.RecordPositions(scene));

    // Frozen trail: the destroyed entity's history stays exactly as it was when it disappeared --
    // it is neither pruned nor grown any further.
    CHECK(tracker.SphereHistories().at(sphereGuid).size() == 2);
}
