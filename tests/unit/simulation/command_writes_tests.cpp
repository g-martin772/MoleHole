#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

TEST_CASE("Pending writes carry commands where they exist and closures otherwise", "[commands][writes]")
{
    Scene scene("Writes");
    const auto entity = scene.CreateEntity("A", "Box");
    scene.Registry().emplace<TransformComponent>(entity);
    const auto guid = scene.GuidOf(entity);

    PendingWrites writes;
    writes.push_back(SetFieldCommand{guid, "Transform", "Position", glm::vec3(1, 2, 3)});
    writes.push_back([guid](Scene& live)
    {
        live.Registry().get<TransformComponent>(live.FindByGuid(guid)).Scale = glm::vec3(5.0f);
    });

    REQUIRE(writes[0].AsCommand().has_value());
    CHECK_FALSE(writes[0].AsClosure());
    CHECK_FALSE(writes[1].AsCommand().has_value());
    REQUIRE(writes[1].AsClosure());

    for (const auto& write : writes) write(scene);
    const auto& transform = scene.Registry().get<TransformComponent>(entity);
    CHECK(transform.Position == glm::vec3(1, 2, 3));
    CHECK(transform.Scale == glm::vec3(5.0f));
}

TEST_CASE("Spawn preset command round-trips through destroy and undo", "[commands][writes]")
{
    RegisterComponents();
    Scene scene("Presets");
    const auto command = MakeSpawnPresetCommand(scene, 555, "Sphere", glm::vec3(5, 0, 0));
    REQUIRE(command.has_value());

    Command inverse = DestroyEntityCommand{};
    REQUIRE(ApplyCommand(scene, *command, &inverse));
    const auto entity = scene.FindByGuid(555);
    REQUIRE(scene.IsValid(entity));
    CHECK(scene.Registry().get<TransformComponent>(entity).Position == glm::vec3(5, 0, 0));
    CHECK(scene.Registry().all_of<SphereComponent>(entity));
    CHECK(scene.Registry().all_of<RigidBodyComponent>(entity));

    REQUIRE(ApplyCommand(scene, inverse));
    CHECK_FALSE(scene.IsValid(scene.FindByGuid(555)));
}
