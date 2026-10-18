#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    constexpr float kTick = 1.0f / 60.0f;

    using Entity = decltype(std::declval<Scene&>().CreateEntity(std::string{}));

    struct TestScene
    {
        Scene World{"GeodesicBodies"};
        Entity Hole{};
        Entity Body{};
        HoleFrame Frame{};
    };

    // A spinning hole with a tilted axis, away from the origin; each runner tick is `tickM` of coordinate time.
    TestScene MakeScene(const double spin, const double tickM)
    {
        RegisterComponents();
        TestScene test;
        const BlackHoleComponent hole{.Mass = Units::kSagittariusAStarMass, .Spin = spin,
                                      .SpinAxis = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f))};
        const glm::dvec3 position{3.0e10, -1.0e10, 2.0e10};
        test.Hole = test.World.CreateEntity("Hole");
        test.World.Registry().emplace<TransformComponent>(test.Hole, TransformComponent{.Position = position});
        test.World.Registry().emplace<BlackHoleComponent>(test.Hole, hole);
        test.Frame = MakeHoleFrame(hole, position);
        test.World.SetTimeScale(tickM * test.Frame.TimeS / kTick);
        return test;
    }

    Entity AddBody(TestScene& test, const glm::dvec3& localM, const GeodesicBodyComponent& body)
    {
        const auto entity = test.World.CreateEntity("Body");
        const glm::dvec3 world = test.Frame.Position + test.Frame.Frame * localM * test.Frame.LengthM;
        test.World.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = world});
        test.World.Registry().emplace<GeodesicBodyComponent>(entity, body);
        return entity;
    }

    template <typename Pred>
    bool WaitFor(Pred&& pred)
    {
        for (int i = 0; i < 300; ++i)
        {
            if (pred()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return pred();
    }
}

TEST_CASE("World and Boyer-Lindquist coordinates round-trip in the hole's spin frame", "[simulation][geodesic]")
{
    auto test = MakeScene(0.9, 1.0);
    for (const glm::dvec3 local : {glm::dvec3(5.0, 1.0, -2.0), glm::dvec3(0.0, 0.0, 7.0), glm::dvec3(-30.0, 4.0, 0.0)})
    {
        const glm::dvec3 world = test.Frame.Position + test.Frame.Frame * local * test.Frame.LengthM;
        const auto bl = WorldToBoyerLindquist(test.Frame, world);
        const auto back = BoyerLindquistToWorld(test.Frame, bl.x, bl.y, bl.z);
        CHECK(glm::length(back - world) < 1e-9 * glm::length(world - test.Frame.Position));
    }
    // The spin axis is the polar axis
    const auto pole = WorldToBoyerLindquist(test.Frame, test.Frame.Position + test.Frame.Frame[2] * 10.0 * test.Frame.LengthM);
    CHECK(pole.y == Approx(0.0).margin(1e-12));
}

TEST_CASE("A geodesic body follows the standalone integrator", "[simulation][geodesic]")
{
    auto test = MakeScene(0.6, 0.5);
    const glm::dvec3 start{9.0, 4.0, 3.0};
    test.Body = AddBody(test, start, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});

    GeodesicSimulationModule module;
    module.OnInit(test.World);
    for (int i = 0; i < 200; ++i) module.OnTick(test.World, kTick);

    const auto& body = test.World.Registry().get<GeodesicBodyComponent>(test.Body);
    REQUIRE(body.Status == GeodesicBodyStatus::FreeFall);

    const glm::dvec3 world = test.Frame.Position + test.Frame.Frame * start * test.Frame.LengthM;
    auto reference = LaunchGeodesic(test.Frame, world, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});
    REQUIRE(reference);
    AdvanceToCoordinateTime(test.Frame.Metric, *reference, 100.0, {.Relative = 1e-12, .Absolute = 1e-14});

    const auto expected = BoyerLindquistToWorld(test.Frame, reference->R, reference->Theta, reference->Phi);
    const auto& position = test.World.Registry().get<TransformComponent>(test.Body).Position;
    CHECK(glm::length(position - expected) < 1e-7 * glm::length(expected - test.Frame.Position));
    CHECK(body.State.T == Approx(100.0).epsilon(1e-9));
    CHECK(body.ProperTime == Approx(reference->Lambda * test.Frame.TimeS).epsilon(1e-7));
}

TEST_CASE("An equatorial orbiting body keeps its radius and runs at the circular-orbit clock rate", "[simulation][geodesic]")
{
    auto test = MakeScene(0.9, 1.0);
    test.Body = AddBody(test, {10.0, 0.0, 0.0}, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});

    GeodesicSimulationModule module;
    module.OnInit(test.World);
    const double r = test.World.Registry().get<GeodesicBodyComponent>(test.Body).State.R;
    for (int i = 0; i < 300; ++i) module.OnTick(test.World, kTick);

    const auto& body = test.World.Registry().get<GeodesicBodyComponent>(test.Body);
    CHECK(body.State.R == Approx(r).epsilon(1e-8));
    const double rate = std::sqrt(CircularOrbitNorm(test.Frame.Metric, r, OrbitDirection::Prograde));
    CHECK(body.ClockRate == Approx(rate).epsilon(1e-9));
    CHECK(body.ProperTime == Approx(rate * 300.0 * test.Frame.TimeS).epsilon(1e-9));
    CHECK(body.Speed > 0.2);
    CHECK(body.Speed < 0.6);
}

TEST_CASE("Moving a geodesic body or editing its launch re-launches it", "[simulation][geodesic]")
{
    auto test = MakeScene(0.5, 1.0);
    test.Body = AddBody(test, {12.0, 0.0, 0.0}, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});
    GeodesicSimulationModule module;
    module.OnInit(test.World);
    for (int i = 0; i < 50; ++i) module.OnTick(test.World, kTick);
    REQUIRE(test.World.Registry().get<GeodesicBodyComponent>(test.Body).ProperTime > 0.0);

    const glm::dvec3 teleport = test.Frame.Position + test.Frame.Frame * glm::dvec3(0.0, 20.0, 0.0) * test.Frame.LengthM;
    test.World.Registry().get<TransformComponent>(test.Body).Position = teleport;
    module.OnTick(test.World, kTick);
    auto& body = test.World.Registry().get<GeodesicBodyComponent>(test.Body);
    CHECK(body.State.R == Approx(20.0).epsilon(1e-2));
    CHECK(body.ProperTime < 1.01 * test.Frame.TimeS);

    body.Retrograde = true;
    module.OnTick(test.World, kTick);
    CHECK(test.World.Registry().get<GeodesicBodyComponent>(test.Body).State.AngularMomentum < 0.0);
}

TEST_CASE("A raindrop crosses the horizon in finite proper time and then stays put", "[simulation][geodesic]")
{
    auto test = MakeScene(0.0, 5.0);
    test.Body = AddBody(test, {10.0, 0.0, 0.0}, GeodesicBodyComponent{.Launch = GeodesicLaunch::Raindrop});
    GeodesicSimulationModule module;
    module.OnInit(test.World);
    for (int i = 0; i < 100; ++i) module.OnTick(test.World, kTick);

    const auto& body = test.World.Registry().get<GeodesicBodyComponent>(test.Body);
    REQUIRE(body.Status == GeodesicBodyStatus::Captured);
    CHECK(body.ClockRate == 0.0);
    // From rest at infinity: τ(r0 -> r) = (2/3) (r0^{3/2} - r^{3/2}) / sqrt(2M), here from 10 M to the capture radius
    const double expected = 2.0 / 3.0 * (std::pow(10.0, 1.5) - std::pow(body.State.R, 1.5)) / std::sqrt(2.0);
    CHECK(body.ProperTime == Approx(expected * test.Frame.TimeS).epsilon(1e-7));

    const auto frozen = test.World.Registry().get<TransformComponent>(test.Body).Position;
    module.OnTick(test.World, kTick);
    CHECK(test.World.Registry().get<TransformComponent>(test.Body).Position == frozen);
}

TEST_CASE("Impossible launches leave the body where it is", "[simulation][geodesic]")
{
    auto test = MakeScene(0.9, 1.0);
    // Static inside the ergosphere (equatorial r = 1.8 M, between r+ ≈ 1.44 M and 2 M)
    test.Body = AddBody(test, {1.8, 0.0, 0.0}, GeodesicBodyComponent{.Launch = GeodesicLaunch::Static});
    const auto faster = AddBody(test, {10.0, 0.0, 0.0},
                                GeodesicBodyComponent{.Launch = GeodesicLaunch::Velocity, .LaunchVelocity = {0.8, 0.0, 0.8}});
    const auto start = test.World.Registry().get<TransformComponent>(test.Body).Position;

    GeodesicSimulationModule module;
    module.OnInit(test.World);
    for (int i = 0; i < 10; ++i) module.OnTick(test.World, kTick);
    CHECK(test.World.Registry().get<GeodesicBodyComponent>(test.Body).Status == GeodesicBodyStatus::CannotLaunch);
    CHECK(test.World.Registry().get<GeodesicBodyComponent>(faster).Status == GeodesicBodyStatus::CannotLaunch);
    CHECK(test.World.Registry().get<TransformComponent>(test.Body).Position == start);

    Scene empty("NoHole");
    const auto lonely = empty.CreateEntity("Lonely");
    empty.Registry().emplace<TransformComponent>(lonely);
    empty.Registry().emplace<GeodesicBodyComponent>(lonely);
    module.OnInit(empty);
    module.OnTick(empty, kTick);
    CHECK(empty.Registry().get<GeodesicBodyComponent>(lonely).Status == GeodesicBodyStatus::NoBlackHole);
}

TEST_CASE("Newtonian gravity leaves geodesic bodies to the geodesic module", "[simulation][geodesic][gravity]")
{
    auto test = MakeScene(0.0, 1.0);
    test.Body = AddBody(test, {10.0, 0.0, 0.0}, GeodesicBodyComponent{.Launch = GeodesicLaunch::Zamo});
    test.World.Registry().emplace<RigidBodyComponent>(
        test.Body, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = Units::kEarthMass, .EnableGravity = true});

    auto dispatcher = std::make_shared<EventDispatcher>();
    auto logger = std::make_shared<Logger>();
    auto physics = std::make_shared<PhysicsSimulationModule>(dispatcher, logger);
    GravitySimulationModule gravity(physics, dispatcher, logger);
    gravity.OnInit(test.World);
    CHECK(test.World.Registry().get<RigidBodyComponent>(test.Body).Type == RigidBodyType::Kinematic);
}

TEST_CASE("Geodesic bodies save their launch settings but not their runtime state", "[simulation][geodesic]")
{
    auto test = MakeScene(0.5, 1.0);
    const auto holeGuid = test.World.GuidOf(test.Hole);
    test.Body = AddBody(test, {12.0, 0.0, 0.0},
                        GeodesicBodyComponent{.BlackHole = holeGuid, .Launch = GeodesicLaunch::Velocity,
                                              .Retrograde = true, .LaunchVelocity = {0.1, -0.05, 0.3}});
    const auto guid = test.World.GuidOf(test.Body);
    GeodesicSimulationModule module;
    module.OnInit(test.World);
    module.OnTick(test.World, kTick);

    Scene loaded("Loaded");
    loaded.DeserializeFromYaml(test.World.SerializeToYaml());
    const auto& body = loaded.Registry().get<GeodesicBodyComponent>(loaded.FindByGuid(guid));
    CHECK(body.BlackHole == holeGuid);
    CHECK(body.Launch == GeodesicLaunch::Velocity);
    CHECK(body.Retrograde);
    CHECK(body.LaunchVelocity == glm::dvec3(0.1, -0.05, 0.3));
    CHECK(body.Status == GeodesicBodyStatus::NotLaunched);
    CHECK(body.ProperTime == 0.0);
}

TEST_CASE("Play and Stop restore geodesic bodies", "[simulation][geodesic][playstop]")
{
    auto test = MakeScene(0.7, 1.0);
    test.Body = AddBody(test, {12.0, 0.0, 0.0}, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});
    const auto guid = test.World.GuidOf(test.Body);
    const auto start = test.World.Registry().get<TransformComponent>(test.Body).Position;

    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());
    auto runner = scenes.CreateSimulation(std::move(test.World), std::vector<std::shared_ptr<ISimulationModule>>{
                                                                     std::make_shared<GeodesicSimulationModule>()});
    runner->Start();
    runner->SetPaused(true);
    const auto read = [&]
    {
        const auto snapshot = runner->AcquireSnapshot();
        const auto entity = snapshot->FindByGuid(guid);
        return std::pair{snapshot->Registry().get<TransformComponent>(entity).Position,
                         snapshot->Registry().get<GeodesicBodyComponent>(entity).ProperTime};
    };

    auto session = std::make_shared<PlaySession>();
    REQUIRE(session->Resume(runner));
    REQUIRE(WaitFor([&] { return read().second > 0.0 && read().first != start; }));
    session->Stop(runner, std::nullopt);
    REQUIRE(WaitFor([&] { return runner->IsPaused() && read().first == start; }));
    CHECK(read().second == 0.0);
    runner->Stop();
}

TEST_CASE("The Gargantua orbits template launches its geodesic bodies", "[simulation][geodesic][templates]")
{
    RegisterComponents();
    std::ifstream file(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "templates" / "gargantua-orbits.yaml");
    REQUIRE(file);
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    Scene scene("Template");
    scene.DeserializeFromYaml(text);
    CHECK(scene.Metadata().TimeScale == 6000.0);

    GeodesicSimulationModule module;
    module.OnInit(scene);
    std::map<std::string, glm::dvec3> start;
    for (auto [entity, body, transform] : scene.Registry().view<GeodesicBodyComponent, TransformComponent>().each())
    {
        INFO(scene.GuidOf(entity));
        CHECK(body.Status == GeodesicBodyStatus::FreeFall);
        start[std::to_string(scene.GuidOf(entity))] = transform.Position;
    }
    REQUIRE(start.size() == 3);
    for (int i = 0; i < 30; ++i) module.OnTick(scene, 0.1f);
    for (auto [entity, body, transform] : scene.Registry().view<GeodesicBodyComponent, TransformComponent>().each())
    {
        INFO(scene.GuidOf(entity) << " r = " << body.State.R << " status " << static_cast<int>(body.Status));
        CHECK(glm::length(transform.Position - start[std::to_string(scene.GuidOf(entity))]) > 1e10);
    }
}

TEST_CASE("A simulation keeps its scene's TimeScale", "[simulation][geodesic][timescale]")
{
    // Scene::Clone used to drop it, so every scene ran at TimeScale 1 in the app
    RegisterComponents();
    Scene scene("Scaled");
    scene.SetTimeScale(6000.0);
    CHECK(scene.Clone().Metadata().TimeScale == 6000.0);

    SceneManager scenes(std::make_shared<Logger>(), std::make_shared<FileSystem>());
    auto runner = scenes.CreateSimulation(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{});
    CHECK(runner->Metadata().TimeScale == 6000.0);
}
