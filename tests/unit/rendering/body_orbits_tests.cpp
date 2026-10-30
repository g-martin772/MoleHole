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
    constexpr double kSunMu = 1.32712440041e20;
    constexpr double kEarthMu = 3.986004355e14;
    constexpr double kAu = 1.495978707e11;

    entt::entity AddBody(Scene& scene, const char* name, const glm::dvec3& position, const glm::dvec3& velocity, const double mu,
                         const double radius)
    {
        const auto entity = scene.CreateEntity(name);
        scene.Registry().emplace_or_replace<TransformComponent>(entity).Position = position;
        scene.Registry().emplace<NBodyComponent>(entity, NBodyComponent{.GM = mu, .Velocity = velocity});
        scene.Registry().emplace<SphereComponent>(entity, SphereComponent{.Radius = radius});
        return entity;
    }

    // Sun, Earth on a circular orbit, a Moon-like body circling the Earth
    Scene MakeSystem()
    {
        RegisterComponents();
        Scene scene("Orbits");
        AddBody(scene, "Sun", {0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, kSunMu, 6.957e8);
        const double earthSpeed = std::sqrt(kSunMu / kAu);
        AddBody(scene, "Earth", {kAu, 0.0, 0.0}, {0.0, 0.0, earthSpeed}, kEarthMu, 6.378e6);
        const double moonDistance = 3.844e8;
        const double moonSpeed = std::sqrt(kEarthMu / moonDistance);
        AddBody(scene, "Moon", {kAu + moonDistance, 0.0, 0.0}, {0.0, 0.0, earthSpeed + moonSpeed}, 4.9e12, 1.7374e6);
        return scene;
    }

    OrbitOverlayView ViewFrom(const glm::dvec3& position, const glm::dvec3& target, const double pixelAngle)
    {
        const glm::dvec3 front = glm::normalize(target - position);
        const glm::dvec3 right = glm::normalize(glm::cross(front, glm::dvec3(0.0, 1.0, 0.0)));
        return {.Position = position, .Front = front, .Right = right, .Up = glm::cross(right, front), .PixelAngle = pixelAngle};
    }
}

TEST_CASE("A circular state gives a circle, an eccentric one the vis-viva ellipse", "[rendering][overlay][body-orbits]")
{
    const double r = 7.0e6, mu = kEarthMu;
    const auto circular = ConicFromState({r, 0.0, 0.0}, {0.0, 0.0, std::sqrt(mu / r)}, mu);
    REQUIRE(circular);
    CHECK(circular->Eccentricity == Approx(0.0).margin(1e-9));
    for (const auto& point : ConicPoints(*circular, 64, 1e9)) CHECK(glm::length(point) == Approx(r).epsilon(1e-9));

    // Periapsis speed 1.2 x circular: e = 0.44, apoapsis at r (1 + e)/(1 - e)
    const auto ellipse = ConicFromState({r, 0.0, 0.0}, {0.0, 0.0, 1.2 * std::sqrt(mu / r)}, mu);
    REQUIRE(ellipse);
    CHECK(ellipse->Eccentricity == Approx(0.44).epsilon(1e-9));
    const auto points = ConicPoints(*ellipse, 128, 1e9);
    CHECK(glm::length(points.front()) == Approx(r).epsilon(1e-9));
    CHECK(glm::length(points[64]) == Approx(r * 1.44 / 0.56).epsilon(1e-9));
    CHECK(glm::length(points.front() - points.back()) < 1e-3);
    for (const auto& point : points)
    {
        const double d = glm::length(point);
        // Every point of the conic satisfies r = p / (1 + e cos nu)
        const double cosNu = glm::dot(point, ellipse->P) / d;
        CHECK(d == Approx(ellipse->SemiLatusRectum / (1.0 + ellipse->Eccentricity * cosNu)).epsilon(1e-9));
    }
}

TEST_CASE("An unbound state gives one branch of a hyperbola up to the radius limit", "[rendering][overlay][body-orbits]")
{
    const double r = 1.0e8, mu = kEarthMu;
    const auto hyperbola = ConicFromState({r, 0.0, 0.0}, {0.0, 0.0, 1.6 * std::sqrt(2.0 * mu / r)}, mu);
    REQUIRE(hyperbola);
    CHECK_FALSE(IsBound(*hyperbola));
    const auto points = ConicPoints(*hyperbola, 64, 2.0 * r);
    REQUIRE(points.size() == 65);
    CHECK(glm::length(points.front()) == Approx(2.0 * r).epsilon(1e-9));
    CHECK(glm::length(points.back()) == Approx(2.0 * r).epsilon(1e-9));
    CHECK(glm::length(points[32]) == Approx(r).epsilon(1e-9)); // the state is at periapsis
    CHECK(glm::length(points[16]) > r);
}

TEST_CASE("Orbit parents follow the Hill spheres", "[rendering][overlay][body-orbits]")
{
    const std::vector<glm::dvec3> position{{0.0, 0.0, 0.0}, {kAu, 0.0, 0.0}, {kAu + 3.844e8, 0.0, 0.0}, {2.0e11, 0.0, 0.0}};
    const std::vector<double> mu{kSunMu, kEarthMu, 4.9e12, 0.0};
    const auto parent = OrbitParents(position, mu);
    CHECK(parent[0] == -1);
    CHECK(parent[1] == 0);
    CHECK(parent[2] == 1);
    CHECK(parent[3] == 0); // a test particle outside every Hill sphere orbits the heaviest body
}

TEST_CASE("The overlay draws a planet's orbit about the Sun and the Moon's about the Earth", "[rendering][overlay][body-orbits]")
{
    auto scene = MakeSystem();
    const auto view = ViewFrom({0.0, 3.0e11, 1.0e11}, {0.0, 0.0, 0.0}, 1.2e-4);
    const auto segments = BodyOrbitsOverlay(scene, view, 64);

    std::size_t planet = 0, moon = 0;
    for (const auto& segment : segments)
    {
        if (segment.Color == kPlanetOrbitColor)
        {
            ++planet;
            if (glm::length(segment.A - segment.B) > 5e9) CHECK(glm::length(segment.A) == Approx(kAu).epsilon(1e-4)); // a circle about the Sun
        }
        if (segment.Color == kMoonOrbitColor) ++moon;
    }
    CHECK(planet > 40);
    // 384,000 km seen from 3e11 m at 0.12 mrad per pixel is 20 px across: the Moon's orbit is drawn about the Earth
    CHECK(moon > 0);
    for (const auto& segment : segments)
    {
        // The ring marking the Moon is bigger than a side of its orbit (6.3e7 m)
        if (segment.Color == kMoonOrbitColor && glm::length(segment.A - segment.B) < 6.0e7)
            CHECK(glm::length(segment.A - glm::dvec3(kAu, 0.0, 0.0)) == Approx(3.844e8).epsilon(5e-2));
    }
}

TEST_CASE("Orbits under the pixel threshold are left out and small bodies get a ring", "[rendering][overlay][body-orbits]")
{
    auto scene = MakeSystem();
    // 1e-3 rad per pixel: the lunar orbit is 2.6 px across, the Earth's 300 px; the Earth's disc is far below a pixel
    const auto segments = BodyOrbitsOverlay(scene, ViewFrom({0.0, 3.0e11, 1.0e11}, {0.0, 0.0, 0.0}, 1e-3), 64);
    CHECK(std::ranges::none_of(segments, [](const OverlaySegment& s) { return s.Color == kMoonOrbitColor; }));
    const auto marker = std::ranges::count_if(segments, [](const OverlaySegment& s)
                                              { return s.Color == kPlanetOrbitColor && glm::length(s.A - s.B) < 1e10; });
    CHECK(marker >= 8); // the 12-sided ring around the Earth 
}

TEST_CASE("A sphere in front hides the orbit line behind it", "[rendering][overlay][body-orbits]")
{
    auto scene = MakeSystem();
    // From the Earth's far side the Sun disc is large (> 2 px) and the orbit lines behind it are dropped
    const auto near = BodyOrbitsOverlay(scene, ViewFrom({1.0e9, 0.0, 0.0}, {0.0, 0.0, 0.0}, 1e-4), 128);
    const glm::dvec3 camera{1.0e9, 0.0, 0.0};
    for (const auto& segment : near)
    {
        const glm::dvec3 mid = 0.5 * (segment.A + segment.B);
        const glm::dvec3 ray = glm::normalize(mid - camera);
        const glm::dvec3 toSun = -camera;
        const double along = glm::dot(toSun, ray);
        const double miss2 = glm::dot(toSun, toSun) - along * along;
        const bool behind = along > 0.0 && miss2 < 6.957e8 * 6.957e8 && along - std::sqrt(6.957e8 * 6.957e8 - miss2) < glm::length(mid - camera);
        CHECK_FALSE(behind);
    }
}

TEST_CASE("The body orbits setting is registered, off by default, with a legend", "[rendering][overlay][body-orbits][settings]")
{
    CHECK_FALSE(RenderSettings{}.ShowBodyOrbits);
    CHECK(RenderSettingRegistry().Find("overlay.body_orbits") != nullptr);
}

TEST_CASE("Bodies outside the view cone and their orbits are left out, test particles are dimmed", "[rendering][overlay][body-orbits]")
{
    auto scene = MakeSystem();
    AddBody(scene, "Comet", {0.5 * kAu, 0.0, 0.0}, {0.0, 0.0, 0.8 * std::sqrt(kSunMu / (0.5 * kAu))}, 0.0, 5.0e3);
    auto view = ViewFrom({0.0, 3.0e11, 1.0e11}, {0.0, 0.0, 0.0}, 1.2e-4);

    const auto all = BodyOrbitsOverlay(scene, view, 64);
    CHECK(std::ranges::any_of(all, [](const OverlaySegment& s) { return s.Color == kPlanetOrbitColor * kTestParticleDimming; }));

    // Look away from the system: nothing of it is in the cone
    view.ViewHalfAngle = 0.3;
    view.Front = -view.Front;
    CHECK(BodyOrbitsOverlay(scene, view, 64).empty());
}
