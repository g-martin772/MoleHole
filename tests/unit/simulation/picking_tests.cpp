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

    const auto hit = PickClosestEntity(scene, glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0));
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

    const auto hit = PickClosestEntity(scene, glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0));
    CHECK_FALSE(hit.has_value());
}

TEST_CASE("PickClosestEntity ignores hits behind the ray origin", "[simulation][picking]")
{
    RegisterComponents();

    Scene scene("PickingBehindTest");
    const auto sphere = scene.CreateEntity("Behind");
    scene.Registry().emplace<TransformComponent>(sphere, TransformComponent{.Position = {0.0f, 0.0f, -5.0f}});
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 1.0f});

    const auto hit = PickClosestEntity(scene, glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0));
    CHECK_FALSE(hit.has_value());
}

TEST_CASE("ScreenPointToRay at the viewport center points straight down the camera's forward axis",
          "[simulation][picking]")
{
    const glm::dvec3 cameraPos{0.0, 0.0, 10.0};
    const RenderFrame frame{.OriginM = glm::dvec3(0.0), .LengthUnitM = 1.0};
    const glm::mat4 view = glm::lookAt(glm::vec3(cameraPos), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
    projection[1][1] *= -1.0f;

    const glm::vec2 viewportMin{100.0f, 50.0f};
    const glm::vec2 viewportSize{800.0f, 600.0f};
    const glm::vec2 center = viewportMin + viewportSize * 0.5f;

    const auto ray = ScreenPointToRay(center, viewportMin, viewportSize, view, projection, cameraPos, frame);
    CHECK(ray.OriginM == cameraPos);
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

    const glm::dvec3 cameraPos{0.0, 0.0, 10.0};
    const RenderFrame frame{.OriginM = glm::dvec3(0.0), .LengthUnitM = 1.0};
    const glm::mat4 view = glm::lookAt(glm::vec3(cameraPos), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
    projection[1][1] *= -1.0f;

    const glm::vec2 viewportMin{0.0f, 0.0f};
    const glm::vec2 viewportSize{800.0f, 600.0f};
    const glm::vec2 center = viewportSize * 0.5f;

    const auto ray = ScreenPointToRay(center, viewportMin, viewportSize, view, projection, cameraPos, frame);
    const auto hit = PickClosestEntity(scene, ray.OriginM, ray.Direction);
    REQUIRE(hit.has_value());
    CHECK(hit->Entity == entity);
}

TEST_CASE("PickAllEntities returns overlapping bodies nearest first and CyclePick walks them", "[simulation][picking]")
{
    RegisterComponents();
    Scene scene("CycleTest");
    const auto far = scene.CreateEntity("Far");
    const auto near = scene.CreateEntity("Near");
    const auto off = scene.CreateEntity("Off");
    scene.Registry().emplace<TransformComponent>(far, TransformComponent{.Position = {0.0f, 0.0f, 20.0f}});
    scene.Registry().emplace<SphereComponent>(far, SphereComponent{.Radius = 1.0f});
    scene.Registry().emplace<TransformComponent>(near, TransformComponent{.Position = {0.0f, 0.0f, 5.0f}});
    scene.Registry().emplace<SphereComponent>(near, SphereComponent{.Radius = 1.0f});
    scene.Registry().emplace<TransformComponent>(off, TransformComponent{.Position = {50.0f, 0.0f, 10.0f}});
    scene.Registry().emplace<SphereComponent>(off, SphereComponent{.Radius = 1.0f});

    const auto hits = PickAllEntities(scene, glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0));
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].Entity == near);
    CHECK(hits[1].Entity == far);

    CHECK(CyclePick(hits, entt::null)->Entity == near);
    CHECK(CyclePick(hits, near)->Entity == far);
    CHECK(CyclePick(hits, far)->Entity == near);
    CHECK(CyclePick(hits, off)->Entity == near);
    CHECK_FALSE(CyclePick({}, near).has_value());
}

TEST_CASE("RenderPointToScreen inverts ScreenPointToRay and rejects points behind the camera", "[simulation][picking]")
{
    const glm::dvec3 cameraPos{0.0, 0.0, 10.0};
    const RenderFrame frame{.OriginM = glm::dvec3(0.0), .LengthUnitM = 1.0};
    const glm::mat4 view = glm::lookAt(glm::vec3(cameraPos), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 1000.0f);
    projection[1][1] *= -1.0f;
    const glm::vec2 viewportMin{100.0f, 50.0f};
    const glm::vec2 viewportSize{800.0f, 600.0f};

    const glm::vec2 mouse{250.0f, 420.0f};
    const auto ray = ScreenPointToRay(mouse, viewportMin, viewportSize, view, projection, cameraPos, frame);
    const glm::vec3 onRay = glm::vec3(cameraPos + ray.Direction * 7.0);
    const auto screen = RenderPointToScreen(onRay, viewportMin, viewportSize, view, projection);
    REQUIRE(screen.has_value());
    CHECK(screen->x == Catch::Approx(mouse.x).margin(0.01));
    CHECK(screen->y == Catch::Approx(mouse.y).margin(0.01));

    CHECK_FALSE(RenderPointToScreen({0.0f, 0.0f, 20.0f}, viewportMin, viewportSize, view, projection).has_value());
}

TEST_CASE("EntitiesInScreenRect selects entities whose centres project inside the rectangle", "[simulation][picking]")
{
    RegisterComponents();
    Scene scene("BoxTest");
    const auto left = scene.CreateEntity("Left");
    const auto right = scene.CreateEntity("Right");
    const auto behind = scene.CreateEntity("Behind");
    scene.Registry().emplace<TransformComponent>(left, TransformComponent{.Position = {-2.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<TransformComponent>(right, TransformComponent{.Position = {2.0f, 0.0f, 0.0f}});
    scene.Registry().emplace<TransformComponent>(behind, TransformComponent{.Position = {-2.0f, 0.0f, 30.0f}});

    const RenderFrame frame{.OriginM = glm::dvec3(0.0), .LengthUnitM = 1.0};
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
    projection[1][1] *= -1.0f;
    const glm::vec2 viewportMin{0.0f, 0.0f};
    const glm::vec2 viewportSize{800.0f, 800.0f};

    const auto leftHalf = EntitiesInScreenRect(scene, frame, viewportMin, viewportSize, view, projection, {0.0f, 0.0f}, {400.0f, 800.0f});
    REQUIRE(leftHalf.size() == 1);
    CHECK(leftHalf.front() == scene.Registry().get<MetadataComponent>(left).Guid);

    const auto all = EntitiesInScreenRect(scene, frame, viewportMin, viewportSize, view, projection, {800.0f, 800.0f}, {0.0f, 0.0f});
    CHECK(all.size() == 2);
}

TEST_CASE("Hidden and locked entities are neither picked nor box selected", "[simulation][picking]")
{
    RegisterComponents();

    Scene scene("PickingFlagsTest");
    const auto near = scene.CreateEntity("Near");
    scene.Registry().emplace<TransformComponent>(near, TransformComponent{.Position = {0.0f, 0.0f, 5.0f}});
    scene.Registry().emplace<SphereComponent>(near, SphereComponent{.Radius = 1.0f});
    const auto far = scene.CreateEntity("Far");
    scene.Registry().emplace<TransformComponent>(far, TransformComponent{.Position = {0.0f, 0.0f, 10.0f}});
    scene.Registry().emplace<SphereComponent>(far, SphereComponent{.Radius = 1.0f});

    scene.Registry().emplace<EditorFlagsComponent>(near, EditorFlagsComponent{.Hidden = true});
    auto hit = PickClosestEntity(scene, glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0));
    REQUIRE(hit.has_value());
    CHECK(hit->Entity == far);

    scene.Registry().get<EditorFlagsComponent>(near) = {.Hidden = false, .Locked = true};
    hit = PickClosestEntity(scene, glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0));
    REQUIRE(hit.has_value());
    CHECK(hit->Entity == far);

    scene.Registry().emplace<EditorFlagsComponent>(far, EditorFlagsComponent{.Locked = true});
    CHECK_FALSE(PickClosestEntity(scene, glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0)).has_value());

    scene.Registry().remove<EditorFlagsComponent>(near);
    CHECK(PickClosestEntity(scene, glm::dvec3(0.0), glm::dvec3(0.0, 0.0, 1.0))->Entity == near);
}

TEST_CASE("EditorFlags round-trip through the scene YAML", "[simulation][picking]")
{
    RegisterComponents();

    Scene scene("FlagsYamlTest");
    const auto hidden = scene.CreateEntity("Hidden");
    scene.Registry().emplace<EditorFlagsComponent>(hidden, EditorFlagsComponent{.Hidden = true});
    const auto plain = scene.CreateEntity("Plain");

    Scene loaded("Loaded");
    loaded.DeserializeFromYaml(scene.SerializeToYaml());
    const auto loadedHidden = loaded.FindByGuid(scene.Registry().get<MetadataComponent>(hidden).Guid);
    const auto loadedPlain = loaded.FindByGuid(scene.Registry().get<MetadataComponent>(plain).Guid);
    CHECK(IsHidden(loaded.Registry(), loadedHidden));
    CHECK_FALSE(IsLocked(loaded.Registry(), loadedHidden));
    CHECK_FALSE(IsHidden(loaded.Registry(), loadedPlain));
    CHECK_FALSE(loaded.Registry().all_of<EditorFlagsComponent>(loadedPlain));
}
