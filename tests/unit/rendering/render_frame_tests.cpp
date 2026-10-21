#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    void AddBlackHole(Scene& scene, const double massKg, const glm::dvec3& position, const double spin = 0.0,
                      const double charge = 0.0)
    {
        const auto entity = scene.CreateEntity("BH", "BlackHole");
        scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = position});
        scene.Registry().emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = massKg, .Spin = spin, .Charge = charge});
    }
}

TEST_CASE("RenderFrame converts world metres to origin-relative render units", "[rendering][units]")
{
    RenderFrame frame;
    frame.OriginM = {1.0e12, 0.0, 0.0};
    frame.LengthUnitM = 1000.0;
    const glm::vec3 p = frame.ToRender({1.0e12 + 2500.0, 500.0, -1000.0});
    CHECK(p.x == Approx(2.5f));
    CHECK(p.y == Approx(0.5f));
    CHECK(p.z == Approx(-1.0f));
    const glm::dvec3 back = frame.ToWorld(p);
    CHECK(back.x == Approx(1.0e12 + 2500.0).epsilon(1e-12));
}

TEST_CASE("A far-away camera keeps nearby objects precise", "[rendering][units]")
{
    RenderFrame frame;
    frame.OriginM = {1.495978707e11, 0.0, 0.0}; // 1 AU from the world origin
    frame.LengthUnitM = 1.0;
    const glm::vec3 offset = frame.ToRender({1.495978707e11 + 12.5, 0.0, 0.0});
    CHECK(offset.x == 12.5f); // a float of the absolute position could not resolve 12.5 m at 1 AU
}

TEST_CASE("The frame unit follows the mode", "[rendering][units]")
{
    RegisterComponents();
    Scene scene;
    AddBlackHole(scene, 10.0 * Units::kSolarMass, {0.0, 0.0, 0.0});
    AddBlackHole(scene, 4.0 * Units::kSolarMass, {1.0e5, 0.0, 0.0});

    const auto solar = RenderFrame::ForScene(scene, FrameMode::SolarMass, {});
    CHECK(solar.LengthUnitM == Approx(Units::kSolarGravitationalRadius));
    CHECK(solar.GeometricMass(10.0 * Units::kSolarMass) == Approx(10.0f));

    const auto primary = RenderFrame::ForScene(scene, FrameMode::PrimaryBlackHole, {});
    CHECK(primary.LengthUnitM == Approx(10.0 * Units::kSolarGravitationalRadius));
    CHECK(primary.GeometricMass(10.0 * Units::kSolarMass) == Approx(1.0f));
    CHECK(primary.GeometricMass(4.0 * Units::kSolarMass) == Approx(0.4f));
}

TEST_CASE("Vacuum images are scale invariant: the same r/M gives the same GPU data for any mass", "[rendering][units]")
{
    RegisterComponents();
    const auto fill = [](const double massSolar)
    {
        Scene scene;
        AddBlackHole(scene, massSolar * Units::kSolarMass, {0.0, 0.0, 0.0});
        const double rg = Units::GravitationalRadius(massSolar * Units::kSolarMass);
        const auto frame = RenderFrame::ForScene(scene, FrameMode::PrimaryBlackHole, {0.0, 0.0, 30.0 * rg});
        RaytraceParamsGpu params{};
        FillSceneData(params, scene, frame);
        return params;
    };
    for (const double mass : {1.0, 10.0, 4.297e6, 6.5e9})
    {
        const auto params = fill(mass);
        INFO(mass);
        CHECK(params.NumBlackHoles == 1);
        CHECK(params.BlackHoles[0].Mass == Approx(1.0f));
        CHECK(params.BlackHoles[0].Position.z == Approx(-30.0f));
    }
}

TEST_CASE("GPU black hole data carries a*, q* and geometric mass only", "[rendering][units]")
{
    RegisterComponents();
    Scene scene;
    const auto entity = scene.CreateEntity("BH", "BlackHole");
    scene.Registry().emplace<TransformComponent>(entity);
    scene.Registry().emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = Units::kSolarMass, .Spin = 0.7, .Charge = 0.2});
    RaytraceParamsGpu params{};
    FillSceneData(params, scene, RenderFrame::ForScene(scene, FrameMode::SolarMass, {}));
    CHECK(params.BlackHoles[0].Spin == Approx(0.7f));
    CHECK(params.BlackHoles[0].Charge == Approx(0.2f));
    CHECK(params.BlackHoles[0].Mass == Approx(1.0f));
}

TEST_CASE("A negative spin reverses the spin axis and uploads |a*|", "[rendering][metric]")
{
    const glm::vec3 axis{0.0f, 2.0f, 0.0f};
    CHECK(EffectiveSpinAxis(axis, 0.5) == glm::vec3(0.0f, 1.0f, 0.0f));
    CHECK(EffectiveSpinAxis(axis, -0.5) == glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(EffectiveSpinAxis(glm::vec3(0.0f), -0.5) == glm::vec3(0.0f, -1.0f, 0.0f));

    RegisterComponents();
    Scene scene;
    AddBlackHole(scene, Units::kSolarMass, {0.0, 0.0, 0.0}, -0.6, 0.3);
    RaytraceParamsGpu params{};
    FillSceneData(params, scene, RenderFrame::ForScene(scene, FrameMode::SolarMass, {}));
    CHECK(params.BlackHoles[0].Spin == Approx(0.6f));
    CHECK(params.BlackHoles[0].SpinAxis.y == Approx(-1.0f));
}

TEST_CASE("The heaviest black hole is uploaded first", "[rendering][units]")
{
    RegisterComponents();
    Scene scene;
    AddBlackHole(scene, 3.0 * Units::kSolarMass, {1.0, 0.0, 0.0});
    AddBlackHole(scene, 30.0 * Units::kSolarMass, {2.0, 0.0, 0.0});
    AddBlackHole(scene, 10.0 * Units::kSolarMass, {3.0, 0.0, 0.0});
    RaytraceParamsGpu params{};
    FillSceneData(params, scene, RenderFrame::ForScene(scene, FrameMode::PrimaryBlackHole, {}));
    REQUIRE(params.NumBlackHoles == 3);
    CHECK(params.BlackHoles[0].Mass == Approx(1.0f));
    CHECK(params.BlackHoles[1].Mass == Approx(1.0f / 3.0f));
    CHECK(params.BlackHoles[2].Mass == Approx(0.1f));
    CHECK(params.BlackHoles[0].MassSolar == Approx(30.0f));
    CHECK(params.BlackHoles[0].Position.x > 0.0f);
}

TEST_CASE("The viewport warns about several holes and naked singularities", "[rendering][metric]")
{
    RegisterComponents();
    Scene one;
    AddBlackHole(one, Units::kSolarMass, {0.0, 0.0, 0.0}, 0.9);
    CHECK(CollectRenderWarnings(one, Spacetime::Kerr).empty());

    Scene two;
    AddBlackHole(two, Units::kSolarMass, {0.0, 0.0, 0.0});
    AddBlackHole(two, 2.0 * Units::kSolarMass, {5.0, 0.0, 0.0});
    const auto warnings = CollectRenderWarnings(two, Spacetime::Kerr);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0].starts_with("2 black holes"));

    Scene naked;
    AddBlackHole(naked, Units::kSolarMass, {0.0, 0.0, 0.0}, 0.65, 0.8);
    CHECK(CollectRenderWarnings(naked, Spacetime::Kerr).empty());
    const auto nakedWarnings = CollectRenderWarnings(naked, Spacetime::KerrNewman);
    REQUIRE(nakedWarnings.size() == 1);
    CHECK(nakedWarnings[0].starts_with("Naked singularity"));
}
