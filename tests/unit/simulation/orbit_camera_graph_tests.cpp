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
    constexpr std::uint64_t kHoleGuid = 7710000000000000001ull;
    constexpr std::uint64_t kCameraGuid = 7710000000000000004ull;
    constexpr float kTick = 0.1f; // a 10 fps video export steps the runner by 0.1 s per frame

    Scene LoadTemplate()
    {
        RegisterComponents();
        std::ifstream file(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "templates" / "gargantua-orbits.yaml");
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        Scene scene("Template");
        scene.DeserializeFromYaml(text);
        return scene;
    }

    glm::dvec3 PositionOf(const Scene& scene, const std::uint64_t guid)
    {
        return scene.Registry().get<TransformComponent>(scene.FindByGuid(guid)).Position;
    }

    // Angle between the camera's view direction and the direction to the black hole.
    double AngleToHole(const Scene& scene)
    {
        const auto camera = scene.FindByGuid(kCameraGuid);
        const auto view = MakeSceneCameraView(scene.Registry().get<TransformComponent>(camera),
                                              scene.Registry().get<CameraComponent>(camera));
        const glm::dvec3 toHole = glm::normalize(PositionOf(scene, kHoleGuid) - view.PositionM);
        return std::acos(std::clamp(glm::dot(glm::dvec3(glm::normalize(view.Front)), toHole), -1.0, 1.0));
    }

    // Azimuth of the camera around the spin axis (world y), to measure how far it has orbited.
    double Azimuth(const Scene& scene)
    {
        const auto p = PositionOf(scene, kCameraGuid) - PositionOf(scene, kHoleGuid);
        return std::atan2(p.x, p.z);
    }
}

TEST_CASE("The orbit camera's animation graph keeps Gargantua centred while the camera rides its geodesic",
          "[simulation][animation][geodesic][camera]")
{
    Scene scene = LoadTemplate();
    const SceneGraphs graphs = LoadSceneGraphs(scene);
    REQUIRE(graphs.Items.size() == 1);
    CHECK(graphs.Items[0].Name == "Camera looks at Gargantua");
    GraphSetExecutor graph(graphs);
    GeodesicSimulationModule module;
    module.OnInit(scene);

    const auto cameraEntity = scene.FindByGuid(kCameraGuid);
    const glm::dvec3 start = PositionOf(scene, kCameraGuid);
    const double startAzimuth = Azimuth(scene);
    for (auto& write : graph.ExecuteStartEvent(scene)) write(scene);

    double properTime = 0.0;
    double worstAngle = 0.0;
    constexpr int kTicks = 30; // three seconds of the 10 fps export
    for (int i = 0; i < kTicks; ++i)
    {
        module.OnTick(scene, kTick);
        for (auto& write : graph.ExecuteTickEvent(scene, kTick)) write(scene);

        const auto& body = scene.Registry().get<GeodesicBodyComponent>(cameraEntity);
        REQUIRE(body.Status == GeodesicBodyStatus::FreeFall);
        // Writing only the rotation never looks like a teleport: the geodesic continues, its clock keeps running
        CHECK(body.ProperTime > properTime);
        properTime = body.ProperTime;
        worstAngle = std::max(worstAngle, AngleToHole(scene));
    }

    // The hole stays dead centre (float positions in the graph limit this to ~1e-6 rad)
    CHECK(worstAngle < 1e-3);
    // ...while the camera has swept a real arc around it
    const double swept = std::abs(std::remainder(Azimuth(scene) - startAzimuth, 2.0 * std::numbers::pi));
    CHECK(swept > 0.5);

    // and it is exactly where the integrator alone puts it: the graph steered the view, not the orbit
    const auto frame = MakeHoleFrame(scene.Registry().get<BlackHoleComponent>(scene.FindByGuid(kHoleGuid)),
                                     PositionOf(scene, kHoleGuid));
    auto reference = LaunchGeodesic(frame, start, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});
    REQUIRE(reference);
    AdvanceToCoordinateTime(frame.Metric, *reference, kTicks * kTick * scene.Metadata().TimeScale / frame.TimeS,
                            {.Relative = 1e-12, .Absolute = 1e-14});
    const auto expected = BoyerLindquistToWorld(frame, reference->R, reference->Theta, reference->Phi);
    CHECK(glm::length(PositionOf(scene, kCameraGuid) - expected) < 1e-7 * glm::length(expected - frame.Position));
}

TEST_CASE("Without the graph the orbiting camera loses sight of the hole", "[simulation][animation][geodesic][camera]")
{
    Scene scene = LoadTemplate();
    GeodesicSimulationModule module;
    module.OnInit(scene);
    const double startAngle = AngleToHole(scene);
    for (int i = 0; i < 30; ++i) module.OnTick(scene, kTick);

    const float fov = scene.Registry().get<CameraComponent>(scene.FindByGuid(kCameraGuid)).Fov;
    CHECK(startAngle < 0.05);                                       // the template starts aimed at the hole
    CHECK(AngleToHole(scene) > glm::radians(static_cast<double>(fov)) / 2.0); // fixed orientation: out of view
}
