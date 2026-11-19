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
    // A hole at the origin with its spin axis along world +y: MakeHoleFrame then puts the hole frame's x axis along
    // world +z and its y axis along world +x, and the equatorial plane is the world xz plane.
    struct InspectorScene
    {
        Scene World{"Inspector"};
        double M = 1.0; // metres per M
    };

    InspectorScene MakeScene(const double spin = 0.0)
    {
        RegisterComponents();
        InspectorScene test;
        const BlackHoleComponent hole{.Mass = 1e6 * Units::kSolarMass, .Spin = spin, .SpinAxis = glm::vec3(0.0f, 1.0f, 0.0f)};
        const auto entity = test.World.CreateEntity("Hole");
        test.World.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = glm::dvec3(0.0)});
        test.World.Registry().emplace<BlackHoleComponent>(entity, hole);
        test.M = Units::GravitationalRadius(hole.Mass);
        return test;
    }

    RenderSettings Integrated(const Spacetime solution = Spacetime::Schwarzschild)
    {
        RenderSettings settings;
        settings.TransportMode = Transport::Integrated;
        settings.Solution = solution;
        return settings;
    }

    // Camera on the +x axis at distance D (in M), offset `lift` (in M) along +y, aiming in the xy plane at angle
    // `alpha` from the inward radial direction, towards +y. A static observer there sees impact parameter
    // b = D sin(alpha) / sqrt(1 - 2M/D) in Schwarzschild.
    struct Shot
    {
        glm::dvec3 Origin;
        glm::dvec3 Direction;
    };

    Shot Aim(const InspectorScene& scene, const double distance, const double impact, const double lift = 0.0)
    {
        const glm::dvec3 origin(distance, lift, 0.0);
        const double radius = glm::length(origin);
        const double alpha = std::asin(impact * std::sqrt(1.0 - 2.0 / radius) / radius);
        const glm::dvec3 inward = -origin / radius;
        const glm::dvec3 side(inward.y, -inward.x, 0.0); // inward turned towards +y
        return {origin * scene.M, std::cos(alpha) * inward + std::sin(alpha) * side};
    }
}

TEST_CASE("A ray aimed at the hole falls in", "[rendering][inspector]")
{
    const auto scene = MakeScene();
    const auto ray = InspectRay(scene.World, glm::dvec3(30.0, 0.0, 0.0) * scene.M, {-1.0, 0.0, 0.0}, Integrated());
    REQUIRE(ray.Valid);
    CHECK(ray.Curved);
    CHECK(ray.Fate == RayFate::Horizon);
    CHECK(ray.ClosestRadiusGeo < 2.01);
    CHECK(ray.HorizonGeo == Approx(2.0));
    CHECK(std::abs(ray.ImpactParameterGeo) < 1e-9);
    CHECK(ray.PathM.size() > 10);
    CHECK(glm::length(ray.PathM.back()) < 2.05 * scene.M);
}

TEST_CASE("Rays just outside the critical impact parameter wind around the hole, rays inside it fall in", "[rendering][inspector]")
{
    const auto scene = MakeScene();
    const double critical = 3.0 * std::sqrt(3.0);

    const auto outside = Aim(scene, 40.0, critical * 1.01, 0.5);
    const auto escaped = InspectRay(scene.World, outside.Origin, outside.Direction, Integrated());
    CHECK(escaped.Fate == RayFate::Sky);
    // A polar-plane orbit: L = 0 and the whole impact parameter is in Carter's constant
    CHECK(std::abs(escaped.ImpactParameterGeo) < 1e-9);
    CHECK(std::sqrt(escaped.CarterGeo2) == Approx(critical * 1.01).epsilon(1e-6));
    CHECK(escaped.ClosestRadiusGeo > 3.0);
    CHECK(escaped.ClosestRadiusGeo < 3.4);
    CHECK(escaped.PhotonOrbitProgradeGeo == Approx(3.0));
    CHECK(escaped.EquatorCrossings >= 2);

    const auto inside = Aim(scene, 40.0, critical * 0.99, 0.5);
    const auto captured = InspectRay(scene.World, inside.Origin, inside.Direction, Integrated());
    CHECK(captured.Fate == RayFate::Horizon);
}

TEST_CASE("A distant ray is bent a little towards the hole", "[rendering][inspector]")
{
    const auto scene = MakeScene();
    const double impact = 25.0;
    const auto shot = Aim(scene, 45.0, impact);
    const auto ray = InspectRay(scene.World, shot.Origin, shot.Direction, Integrated());
    CHECK(ray.Fate == RayFate::Sky);
    CHECK(std::sqrt(ray.CarterGeo2 + ray.ImpactParameterGeo * ray.ImpactParameterGeo) == Approx(impact).epsilon(1e-6));
    CHECK(ray.ClosestRadiusGeo > impact - 2.0);
    // Between 45 M in and 50 M out the ray picks up most of the full deflection 4 M / b + 15 pi M² / (4 b²) = 10.3
    // degrees; the start direction is measured in the camera's local frame, which shifts it by a fraction of a degree
    const double full = (4.0 / impact + 15.0 * std::numbers::pi / (4.0 * impact * impact)) * 180.0 / std::numbers::pi;
    CHECK(ray.DeflectionDeg > 0.5 * full);
    CHECK(ray.DeflectionDeg < 1.1 * full);
    CHECK(glm::length(ray.EscapeDirection) == Approx(1.0));
}

TEST_CASE("A disk crossing reports the redshift of gas on a circular orbit", "[rendering][inspector]")
{
    const auto scene = MakeScene();
    const glm::dvec3 camera = glm::dvec3(1.0, 30.0, 0.0) * scene.M;
    const glm::dvec3 target = glm::dvec3(10.0, 0.0, 0.0) * scene.M;
    const auto ray = InspectRay(scene.World, camera, glm::normalize(target - camera), Integrated());
    REQUIRE_FALSE(ray.DiskCrossings.empty());
    const auto& crossing = ray.DiskCrossings.front();
    CHECK(crossing.Order == 1);
    CHECK(crossing.RadiusGeo > 6.0);
    CHECK(crossing.RadiusGeo < 20.0);
    CHECK(std::abs(glm::length(crossing.PointM) / scene.M - crossing.RadiusGeo) < 1e-6 * crossing.RadiusGeo);
    // Schwarzschild: the ZAMO is the static observer, its lapse sqrt(1 - 2M/r)
    const double cameraR = glm::length(camera) / scene.M;
    CHECK(crossing.GGravity == Approx(std::sqrt(1.0 - 2.0 / crossing.RadiusGeo) / std::sqrt(1.0 - 2.0 / cameraR)).epsilon(1e-6));
    // Seen nearly face-on, the gas motion only adds the transverse Doppler redshift
    CHECK(crossing.G < crossing.GGravity);
    CHECK(crossing.G / crossing.GGravity > 0.8);

    auto noDisk = Integrated();
    noDisk.AccretionDisk = false;
    CHECK(InspectRay(scene.World, camera, glm::normalize(target - camera), noDisk).DiskCrossings.empty());
}

TEST_CASE("Frame dragging makes prograde and retrograde rays differ in Kerr", "[rendering][inspector]")
{
    const auto scene = MakeScene(0.95);
    // Equatorial rays (world xz plane) passing the hole on either side at the same distance
    const glm::dvec3 camera = glm::dvec3(40.0, 0.0, 0.0) * scene.M;
    const auto pass = [&](const double side)
    {
        const glm::dvec3 target = glm::dvec3(0.0, 0.0, side * 4.5) * scene.M;
        return InspectRay(scene.World, camera, glm::normalize(target - camera), Integrated(Spacetime::Kerr));
    };
    const auto a = pass(1.0);
    const auto b = pass(-1.0);
    CHECK(a.ImpactParameterGeo * b.ImpactParameterGeo < 0.0);
    // One of them co-rotates: it gets closer to the hole without being caught, the other is swallowed
    CHECK((a.Fate == RayFate::Horizon) != (b.Fate == RayFate::Horizon));
}

TEST_CASE("The Straight transport and a scene without holes give a straight line", "[rendering][inspector]")
{
    const auto scene = MakeScene();
    RenderSettings straight;
    straight.TransportMode = Transport::Straight;
    const auto ray = InspectRay(scene.World, glm::dvec3(30.0, 0.0, 0.0) * scene.M, {-1.0, 0.0, 0.0}, straight);
    CHECK_FALSE(ray.Curved);
    CHECK(ray.Fate == RayFate::Horizon);
    REQUIRE(ray.PathM.size() == 2);
    CHECK(glm::length(ray.PathM.back()) == Approx(2.0 * scene.M));

    RegisterComponents();
    Scene empty{"Empty"};
    const auto flat = InspectRay(empty, glm::dvec3(0.0), {0.0, 0.0, -1.0}, Integrated());
    CHECK_FALSE(flat.Curved);
    CHECK(flat.Fate == RayFate::Sky);
}

TEST_CASE("A ray that meets a sphere stops on its surface", "[rendering][inspector]")
{
    auto scene = MakeScene();
    const auto ball = scene.World.CreateEntity("Ball");
    scene.World.Registry().emplace<TransformComponent>(ball, TransformComponent{.Position = glm::dvec3(0.0, 0.0, 20.0) * scene.M});
    scene.World.Registry().emplace<SphereComponent>(ball, SphereComponent{.Radius = 2.0 * scene.M});
    const glm::dvec3 camera = glm::dvec3(0.0, 1.0, 40.0) * scene.M;
    const auto ray = InspectRay(scene.World, camera, {0.0, 0.0, -1.0}, Integrated());
    CHECK(ray.Fate == RayFate::Object);
    CHECK(ray.ObjectName == "Ball");
    CHECK(glm::distance(ray.PathM.back(), glm::dvec3(0.0, 0.0, 20.0) * scene.M) == Approx(2.0 * scene.M).epsilon(1e-3));
}

TEST_CASE("The step budget ends a ray that needs more", "[rendering][inspector]")
{
    const auto scene = MakeScene();
    const auto shot = Aim(scene, 40.0, 3.0 * std::sqrt(3.0) * 1.01, 0.5);
    const auto ray = InspectRay(scene.World, shot.Origin, shot.Direction, Integrated(), 0, 5);
    CHECK(ray.Fate == RayFate::StepLimit);
}

TEST_CASE("Pixel directions follow the tracer's camera model", "[rendering][inspector]")
{
    Camera camera(90.0f, 2.0f);
    camera.SetView(glm::dvec3(0.0), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto center = CameraPixelDirection(camera, 2.0, {0.0, 0.0});
    CHECK(center.z == Approx(-1.0));
    // tan(45 deg) = 1: the top edge is 45 degrees up, the right edge atan(aspect) to the side
    const auto top = CameraPixelDirection(camera, 2.0, {0.0, 1.0});
    CHECK(top.y == Approx(std::sqrt(0.5)));
    const auto right = CameraPixelDirection(camera, 2.0, {1.0, 0.0});
    CHECK(right.x / -right.z == Approx(2.0));
}

TEST_CASE("The inspector overlay colours the integrated part and marks disk crossings", "[rendering][inspector]")
{
    const auto scene = MakeScene();
    const glm::dvec3 camera = glm::dvec3(1.0, 30.0, 0.0) * scene.M;
    const glm::dvec3 target = glm::dvec3(10.0, 0.0, 0.0) * scene.M;
    const auto ray = InspectRay(scene.World, camera, glm::normalize(target - camera), Integrated());
    const auto lines = RayInspectionOverlay(ray);
    CHECK(lines.size() == ray.PathM.size() - 1 + 3 * ray.DiskCrossings.size());
    CHECK(std::ranges::count_if(lines, [](const OverlaySegment& s) { return s.Color == kInspectorDiskColor; })
          == static_cast<std::ptrdiff_t>(3 * ray.DiskCrossings.size()));
    CHECK(lines[ray.CurvedBegin].Color == kInspectorNearColor);
    CHECK(RayInspectionOverlay(RayInspection{}).empty());
}
