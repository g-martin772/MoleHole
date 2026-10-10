#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;
using GPP::Scene;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    constexpr float kPi = 3.14159265358979f;

    struct Rig
    {
        GPP::AssetOptions Options;
        std::unique_ptr<GPP::AssetDirectories> Assets;
        Scene Level{"SpringArm"};
        entt::entity Target{entt::null};
        entt::entity Camera{entt::null};
        std::unique_ptr<ScriptRuntime> Runtime;
        SceneGraphs Graphs;
        ScriptCache Cache;

        Rig(const glm::vec3& targetAt, const glm::vec3& cameraAt, std::map<std::string, GPP::FieldValue> props)
        {
            RegisterComponents();
            Options.Kinds["Scripts"] = GPP::AssetKindOptions{{fs::path(MOLEHOLE_SOURCE_DIR) / "scripts"}, {".luau"}};
            Assets = std::make_unique<GPP::AssetDirectories>(Options);
            Target = Level.CreateEntity("Target");
            Level.Registry().emplace<GPP::TransformComponent>(Target).Position = targetAt;
            Camera = Level.CreateEntity("Camera");
            Level.Registry().emplace<GPP::TransformComponent>(Camera).Position = cameraAt;
            Level.Registry().emplace<GPP::CameraComponent>(Camera);
            props["Target"] = Level.GuidOf(Target);
            Level.Registry().emplace<GPP::ScriptsComponent>(Camera, GPP::ScriptsComponent{{GPP::ScriptEntry{"spring_arm_camera", props}}});
            Runtime = std::make_unique<ScriptRuntime>(Graphs, Cache, nullptr, TranspileOptions{}, GPP::LuauLimits{}, Assets.get());
            REQUIRE(Runtime->Ok());
            Apply(Runtime->ExecuteStartEvent(Level));
        }

        void Apply(PendingWrites writes) { for (auto& write : writes) write(Level); }

        void Run(const float seconds, const float dt)
        {
            const int ticks = static_cast<int>(std::round(seconds / dt));
            for (int i = 0; i < ticks; ++i) Apply(Runtime->ExecuteTickEvent(Level, dt));
            for (const auto& error : Runtime->TakeScriptErrors()) FAIL(error.Message);
        }

        glm::vec3 CameraPosition() { return Level.Registry().get<GPP::TransformComponent>(Camera).Position; }
        glm::vec3 Forward()
        {
            return glm::normalize(Level.Registry().get<GPP::TransformComponent>(Camera).Rotation * glm::vec3(0, 0, -1));
        }
    };
}

TEST_CASE("The spring arm script declares the documented properties", "[springarm][scripts]")
{
    GPP::AssetOptions options;
    options.Kinds["Scripts"] = GPP::AssetKindOptions{{fs::path(MOLEHOLE_SOURCE_DIR) / "scripts"}, {".luau"}};
    GPP::AssetDirectories assets(options);
    GPP::ScriptCatalog catalog(assets);
    const auto descriptor = catalog.Describe("spring_arm_camera");
    INFO(descriptor->Error);
    REQUIRE(descriptor->Ok());
    std::vector<std::string> names;
    for (const auto& property : descriptor->Properties) names.push_back(property.Name);
    CHECK(names == std::vector<std::string>{"Target", "ArmLength", "Yaw", "Pitch", "OrbitSpeed", "HeightOffset", "Smoothing", "LookAtTarget"});
    CHECK(descriptor->Find("Target")->Type == GPP::ScriptPropType::Entity);
    CHECK(descriptor->Find("LookAtTarget")->Type == GPP::ScriptPropType::Bool);
    CHECK(descriptor->Find("ArmLength")->HasRange);
}

TEST_CASE("The camera converges to the arm length and looks at the target", "[springarm][scripts]")
{
    const glm::vec3 target(10.0f, 5.0f, -20.0f);
    Rig rig(target, glm::vec3(200.0f, 3.0f, 40.0f),
            {{"ArmLength", 14.0f}, {"Pitch", 30.0f}, {"OrbitSpeed", 0.0f}, {"Smoothing", 0.25f}});
    rig.Run(0.1f, 0.02f);
    CHECK(glm::distance(rig.CameraPosition(), target) > 14.5f);
    rig.Run(10.0f, 1.0f / 60.0f);
    CHECK(glm::distance(rig.CameraPosition(), target) == Approx(14.0f).margin(1.0e-3));
    const glm::vec3 offset = rig.CameraPosition() - target;
    CHECK(std::asin(offset.y / glm::length(offset)) == Approx(30.0f * kPi / 180.0f).margin(1.0e-3));
    const glm::vec3 toTarget = glm::normalize(target - rig.CameraPosition());
    CHECK(glm::dot(rig.Forward(), toTarget) == Approx(1.0f).margin(1.0e-4));
}

TEST_CASE("Without smoothing the yaw advances by OrbitSpeed times elapsed time", "[springarm][scripts]")
{
    const glm::vec3 target(0.0f, 0.0f, 0.0f);
    Rig rig(target, glm::vec3(0.0f, 0.0f, 20.0f), {{"ArmLength", 10.0f}, {"Pitch", 0.0f}, {"Yaw", 20.0f}, {"OrbitSpeed", 45.0f}, {"Smoothing", 0.0f}});
    rig.Run(2.0f, 0.25f);
    const glm::vec3 position = rig.CameraPosition();
    const float yaw = std::atan2(position.x, position.z) * 180.0f / kPi;
    CHECK(yaw == Approx(20.0f + 45.0f * 2.0f).margin(1.0e-2));
    CHECK(glm::length(position) == Approx(10.0f).margin(1.0e-3));
    rig.Run(2.0f, 0.25f);
    const glm::vec3 later = rig.CameraPosition();
    CHECK(std::atan2(later.x, later.z) * 180.0f / kPi == Approx(-160.0f).margin(1.0e-2));
}

TEST_CASE("Smoothing is frame-rate independent", "[springarm][scripts]")
{
    const std::map<std::string, GPP::FieldValue> props{{"ArmLength", 10.0f}, {"Pitch", 0.0f}, {"OrbitSpeed", 0.0f}, {"Smoothing", 0.5f}};
    Rig coarse(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 30.0f), props);
    Rig fine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 30.0f), props);
    coarse.Run(1.0f, 0.1f);
    fine.Run(1.0f, 0.01f);
    CHECK(coarse.CameraPosition().z == Approx(fine.CameraPosition().z).margin(1.0e-3));
    CHECK(coarse.CameraPosition().z == Approx(10.0f + 20.0f * 0.25f).margin(1.0e-3));
}

TEST_CASE("Height offset lifts the pivot and look-at can be switched off", "[springarm][scripts]")
{
    Rig rig(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 20.0f),
            {{"ArmLength", 10.0f}, {"Pitch", 0.0f}, {"OrbitSpeed", 0.0f}, {"Smoothing", 0.0f}, {"HeightOffset", 3.0f}, {"LookAtTarget", false}});
    rig.Run(0.1f, 0.05f);
    CHECK(rig.CameraPosition().y == Approx(3.0f).margin(1.0e-4));
    const glm::quat rotation = rig.Level.Registry().get<GPP::TransformComponent>(rig.Camera).Rotation;
    CHECK(glm::dot(rotation, glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) == Approx(1.0f).margin(1.0e-4));
}

TEST_CASE("Property edits take effect on the running script and a missing target is harmless", "[springarm][scripts]")
{
    Rig rig(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 20.0f),
            {{"ArmLength", 10.0f}, {"Pitch", 0.0f}, {"OrbitSpeed", 0.0f}, {"Smoothing", 0.0f}});
    rig.Run(0.1f, 0.05f);
    CHECK(glm::length(rig.CameraPosition()) == Approx(10.0f).margin(1.0e-3));
    rig.Level.Registry().get<GPP::ScriptsComponent>(rig.Camera).Entries[0].Props["ArmLength"] = 25.0f;
    rig.Run(0.1f, 0.05f);
    CHECK(glm::length(rig.CameraPosition()) == Approx(25.0f).margin(1.0e-3));
    rig.Level.Registry().get<GPP::ScriptsComponent>(rig.Camera).Entries[0].Props["Target"] = std::uint64_t{123456};
    rig.Run(0.1f, 0.05f);
    CHECK(glm::length(rig.CameraPosition()) == Approx(25.0f).margin(1.0e-3));
    CHECK(rig.Runtime->ScriptStatuses()[0].Message.empty());
}

TEST_CASE("The spring arm demo template wires the script to the black hole", "[springarm][templates]")
{
    RegisterComponents();
    std::ifstream stream(fs::path(MOLEHOLE_SOURCE_DIR) / "templates" / "spring-arm-demo.yaml");
    const std::string yaml{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    Scene scene;
    scene.DeserializeFromYaml(yaml);
    std::uint64_t blackHole = 0;
    for (auto [entity, metadata, hole] : scene.Registry().view<const GPP::MetadataComponent, const BlackHoleComponent>().each())
    {
        blackHole = metadata.Guid;
    }
    REQUIRE(blackHole != 0);
    bool found = false;
    for (auto [entity, camera, scripts] : scene.Registry().view<const GPP::CameraComponent, const GPP::ScriptsComponent>().each())
    {
        REQUIRE(scripts.Entries.size() == 1);
        CHECK(scripts.Entries[0].Name == "spring_arm_camera");
        CHECK(std::get<std::uint64_t>(GPP::CoerceProperty(scripts.Entries[0].Props.at("Target"), GPP::ScriptPropType::Entity)) == blackHole);
        CHECK(camera.Primary);
        found = true;
    }
    CHECK(found);

    GPP::AssetOptions options;
    options.Kinds["Scripts"] = GPP::AssetKindOptions{{fs::path(MOLEHOLE_SOURCE_DIR) / "scripts"}, {".luau"}};
    GPP::AssetDirectories assets(options);
    SceneGraphs graphs;
    ScriptCache cache;
    ScriptRuntime runtime(graphs, cache, nullptr, {}, {}, &assets);
    for (auto& write : runtime.ExecuteStartEvent(scene)) write(scene);
    for (int i = 0; i < 120; ++i)
    {
        for (auto& write : runtime.ExecuteTickEvent(scene, 1.0f / 60.0f)) write(scene);
    }
    CHECK(runtime.TakeScriptErrors().empty());
    for (auto [entity, camera, transform] : scene.Registry().view<const GPP::CameraComponent, const GPP::TransformComponent>().each())
    {
        const auto hole = scene.FindByGuid(blackHole);
        const float distance = glm::distance(transform.Position, scene.Registry().get<GPP::TransformComponent>(hole).Position);
        CHECK(distance < 15.0f);
        CHECK(distance > 12.0f);
    }
}
