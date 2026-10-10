#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

TEST_CASE("PickClosestEntity hits the nearer of two spheres along the ray", "[simulation][picking]")
{
    RegisterComponents();

    Scene scene("PickingTest");

    const auto near = scene.CreateEntity("Near");
    scene.Registry().emplace<TransformComponent>(near, TransformComponent{.Position = {0.0f, 0.0f, 5.0f}});
    scene.Registry().emplace<SphereComponent>(near, SphereComponent{.Radius = 1.0f});

    const auto far = scene.CreateEntity("Far");
    scene.Registry().emplace<TransformComponent>(far, TransformComponent{.Position = {0.0f, 0.0f, 10.0f}});
    scene.Registry().emplace<SphereComponent>(far, SphereComponent{.Radius = 1.0f});

    const auto hit = PickClosestEntity(scene, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(hit.has_value());
    CHECK(hit->Entity == near);
    CHECK(hit->Distance == Catch::Approx(4.0f));
}

TEST_CASE("PickClosestEntity misses when the ray does not cross any bounding sphere", "[simulation][picking]")
{
    RegisterComponents();

    Scene scene("PickingMissTest");
    const auto sphere = scene.CreateEntity("Off axis");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {10.0f, 0.0f, 5.0f}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 1.0f});

    const auto hit = PickClosestEntity(scene, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK_FALSE(hit.has_value());
}

TEST_CASE("PickClosestEntity ignores hits behind the ray origin", "[simulation][picking]")
{
    RegisterComponents();

    Scene scene("PickingBehindTest");
    const auto sphere = scene.CreateEntity("Behind");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {0.0f, 0.0f, -5.0f}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 1.0f});

    const auto hit = PickClosestEntity(scene, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK_FALSE(hit.has_value());
}

TEST_CASE("ScreenPointToRay at the viewport center points straight down the camera's forward axis",
          "[simulation][picking]")
{
    const glm::vec3 cameraPos{0.0f, 0.0f, 10.0f};
    const glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
    projection[1][1] *= -1.0f;

    const glm::vec2 viewportMin{100.0f, 50.0f};
    const glm::vec2 viewportSize{800.0f, 600.0f};
    const glm::vec2 center = viewportMin + viewportSize * 0.5f;

    const auto ray = ScreenPointToRay(center, viewportMin, viewportSize, view, projection, cameraPos);
    CHECK(ray.Origin == cameraPos);
    CHECK(ray.Direction.x == Catch::Approx(0.0f).margin(1e-4));
    CHECK(ray.Direction.y == Catch::Approx(0.0f).margin(1e-4));
    CHECK(ray.Direction.z == Catch::Approx(-1.0f).margin(1e-4));
}

TEST_CASE("ScreenPointToRay finds an entity directly ahead of the camera", "[simulation][picking]")
{
    RegisterComponents();
    Scene scene("ScreenPickTest");
    const auto entity = scene.CreateEntity("Target");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {0.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<SphereComponent>(entity, SphereComponent{.Radius = 1.0f});

    const glm::vec3 cameraPos{0.0f, 0.0f, 10.0f};
    const glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
    projection[1][1] *= -1.0f;

    const glm::vec2 viewportMin{0.0f, 0.0f};
    const glm::vec2 viewportSize{800.0f, 600.0f};
    const glm::vec2 center = viewportSize * 0.5f;

    const auto ray = ScreenPointToRay(center, viewportMin, viewportSize, view, projection, cameraPos);
    const auto hit = PickClosestEntity(scene, ray.Origin, ray.Direction);
    REQUIRE(hit.has_value());
    CHECK(hit->Entity == entity);
}
