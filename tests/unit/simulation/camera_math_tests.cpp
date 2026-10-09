#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

TEST_CASE("LookAtRotation points -Z at the target", "[simulation][camera]")
{
    const glm::vec3 eye{1.0f, 2.0f, 3.0f};
    const glm::vec3 target{1.0f, 2.0f, -7.0f};
    const glm::vec3 front = GPP::LookAtRotation(eye, target) * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(front.z == Approx(-1.0f).margin(1e-5));

    const glm::vec3 diagonal = GPP::LookAtRotation({0, 0, 0}, {3, 4, 0}) * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(glm::length(diagonal - glm::normalize(glm::vec3(3, 4, 0))) == Approx(0.0f).margin(1e-5));
}

TEST_CASE("LookAtRotation survives degenerate input", "[simulation][camera]")
{
    const auto same = GPP::LookAtRotation({1, 1, 1}, {1, 1, 1});
    CHECK(same.w == Approx(1.0f));

    const glm::vec3 up = GPP::LookAtRotation({0, 0, 0}, {0, 5, 0}) * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(up.y == Approx(1.0f).margin(1e-5));
}

TEST_CASE("Euler output reproduces the look-at orientation through the Transform pin conversion",
          "[simulation][camera]")
{
    const glm::vec3 eye{0.0f}, target{2.0f, -1.0f, -5.0f};
    const glm::quat rebuilt{glm::radians(GPP::LookAtEulerDegrees(eye, target))};
    const glm::vec3 front = rebuilt * glm::vec3(0.0f, 0.0f, -1.0f);
    CHECK(glm::length(front - glm::normalize(target - eye)) == Approx(0.0f).margin(1e-4));
}

TEST_CASE("Scene camera view and matrix follow the transform", "[simulation][camera]")
{
    TransformComponent transform{.Position = {5.0f, 0.0f, 0.0f}};
    transform.Rotation = GPP::LookAtRotation(transform.Position, {0.0f, 0.0f, 0.0f});
    const CameraComponent camera{.Fov = 45.0f};

    const auto view = MakeSceneCameraView(transform, camera);
    CHECK(view.Front.x == Approx(-1.0f).margin(1e-5));
    CHECK(view.Fov == 45.0f);

    const glm::vec4 origin = SceneCameraViewMatrix(view) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    CHECK(origin.x == Approx(0.0f).margin(1e-4));
    CHECK(origin.z == Approx(-5.0f).margin(1e-4));
}

TEST_CASE("FindPrimarySceneCamera picks only primary cameras", "[simulation][camera]")
{
    RegisterComponents();
    Scene scene("Cams");
    CHECK_FALSE(FindPrimarySceneCamera(scene).has_value());

    const auto secondary = scene.CreateEntity("A", "Camera");
    scene.Registry().emplace<TransformComponent>(secondary);
    scene.Registry().emplace<CameraComponent>(secondary, CameraComponent{.Primary = false});
    CHECK_FALSE(FindPrimarySceneCamera(scene).has_value());

    const auto primary = scene.CreateEntity("B", "Camera");
    scene.Registry().emplace<TransformComponent>(primary, TransformComponent{.Position = {0, 0, 9}});
    scene.Registry().emplace<CameraComponent>(primary, CameraComponent{.Fov = 30.0f});
    const auto found = FindPrimarySceneCamera(scene);
    REQUIRE(found.has_value());
    CHECK(found->Position.z == 9.0f);
    CHECK(found->Fov == 30.0f);
}

TEST_CASE("Entity presets spawn with the requested guid and the first camera becomes primary",
          "[simulation][camera]")
{
    RegisterComponents();
    Scene scene("Presets");
    const auto first = SpawnPreset(scene, 11, "camera", {0, 1, 2});
    REQUIRE(scene.IsValid(first));
    CHECK(scene.Registry().get<CameraComponent>(first).Primary);
    const auto second = SpawnPreset(scene, 12, "Camera", {});
    CHECK_FALSE(scene.Registry().get<CameraComponent>(second).Primary);
    CHECK_FALSE(scene.IsValid(SpawnPreset(scene, 13, "Nope", {})));
    CHECK_FALSE(scene.IsValid(SpawnPreset(scene, 11, "Empty", {})));
}
