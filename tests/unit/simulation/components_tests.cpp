#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

TEST_CASE("BlackHoleComponent and SphereComponent round-trip through scene YAML", "[simulation][scene]")
{
    RegisterComponents();

    Scene scene("ComponentRoundTrip");
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(
        blackHole, TransformComponent{.Position = {0.0f, 17.0f, 60.0f}});
    scene.Registry().emplace<BlackHoleComponent>(
        blackHole, BlackHoleComponent{.Mass = 3.978e+30f, .Spin = 0.65f, .Charge = 0.0f,
                                       .SpinAxis = {0.5f, 1.0f, 0.1f}});

    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<TransformComponent>(
        sphere, TransformComponent{.Position = {0.0f, 0.0f, 20.0f}});
    scene.Registry().emplace<SphereComponent>(
        sphere, SphereComponent{.Radius = 5.0f, .Spin = 0.2f, .Color = {0.0f, 0.5f, 1.0f},
                                 .SpinAxis = {0.0f, 1.0f, 0.0f}, .TexturePath = "moon.png"});

    Scene loaded;
    loaded.DeserializeFromYaml(scene.SerializeToYaml());

    const auto loadedBlackHole = loaded.FindByGuid(scene.GuidOf(blackHole));
    REQUIRE(loaded.IsValid(loadedBlackHole));
    const auto& bh = loaded.Registry().get<BlackHoleComponent>(loadedBlackHole);
    CHECK(bh.Mass == Catch::Approx(3.978e+30f));
    CHECK(bh.Spin == Catch::Approx(0.65f));
    CHECK(bh.SpinAxis.x == Catch::Approx(0.5f));

    const auto loadedSphere = loaded.FindByGuid(scene.GuidOf(sphere));
    REQUIRE(loaded.IsValid(loadedSphere));
    const auto& sph = loaded.Registry().get<SphereComponent>(loadedSphere);
    CHECK(sph.Radius == Catch::Approx(5.0f));
    CHECK(sph.Color.z == Catch::Approx(1.0f));
    CHECK(sph.TexturePath == "moon.png");
}
