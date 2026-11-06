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
    struct Rig
    {
        Scene Space{"EntityClockRate"};
        HoleFrame Frame{};
        entt::entity Hole{entt::null};
        entt::entity Probe{entt::null};

        Rig(const double spin, const bool withHole = true)
        {
            RegisterComponents();
            if (withHole)
            {
                const BlackHoleComponent hole{.Mass = 10.0 * Units::kSolarMass, .Spin = spin};
                Hole = Space.CreateEntity("Hole");
                Space.Registry().emplace<TransformComponent>(Hole);
                Space.Registry().emplace<BlackHoleComponent>(Hole, hole);
                Frame = MakeHoleFrame(hole, {0.0, 0.0, 0.0});
            }
            Probe = Space.CreateEntity("Probe");
            Space.Registry().emplace<TransformComponent>(Probe);
            Space.Registry().emplace<VelocityComponent>(Probe);
        }

        void Place(const glm::dvec3& positionInM, const glm::dvec3& velocityInC = glm::dvec3(0.0))
        {
            Space.Registry().get<TransformComponent>(Probe).Position = positionInM * Frame.LengthM;
            Space.Registry().get<VelocityComponent>(Probe).Linear = velocityInC * Units::kSpeedOfLight;
        }

        [[nodiscard]] double Rate() { return EntityClockRate(Space, Space.GuidOf(Probe)); }
    };
}

TEST_CASE("Entity clock rate is the Schwarzschild closed form", "[simulation][relativity][script]")
{
    Rig rig(0.0);
    const double r = 40.0;
    const double alpha2 = 1.0 - 2.0 / r;

    rig.Place({r, 0.0, 0.0});
    CHECK(rig.Rate() == Approx(std::sqrt(alpha2)).epsilon(1e-12));

    const double tangential = 0.2;
    rig.Place({r, 0.0, 0.0}, {0.0, tangential, 0.0});
    CHECK(rig.Rate() == Approx(std::sqrt(alpha2 - tangential * tangential)).epsilon(1e-12));
    const double local = tangential / std::sqrt(alpha2);
    CHECK(rig.Rate() == Approx(std::sqrt(alpha2) * std::sqrt(1.0 - local * local)).epsilon(1e-12));

    const double radial = 0.3;
    rig.Place({r, 0.0, 0.0}, {radial, 0.0, 0.0});
    CHECK(rig.Rate() == Approx(std::sqrt(alpha2 - radial * radial / alpha2)).epsilon(1e-12));

    rig.Place({0.0, 0.0, r}, {0.0, 0.0, -radial});
    CHECK(rig.Rate() == Approx(std::sqrt(alpha2 - radial * radial / alpha2)).epsilon(1e-12));
}

TEST_CASE("Entity clock rate of a resting entity is the static observer of the full Kerr metric", "[simulation][relativity][script]")
{
    Rig rig(0.8);
    for (const auto& position : {glm::dvec3(30.0, 0.0, 0.0), glm::dvec3(10.0, 20.0, 15.0), glm::dvec3(0.0, 0.0, 25.0)})
    {
        rig.Place(position);
        CHECK(rig.Rate() == Approx(ObserverClockRateAt(rig.Space, position * rig.Frame.LengthM, 1)).epsilon(1e-9));
    }
}

TEST_CASE("Entity clock rate follows the hole's own velocity and the horizon", "[simulation][relativity][script]")
{
    Rig rig(0.0);
    const double r = 40.0;
    const double alpha2 = 1.0 - 2.0 / r;
    rig.Place({r, 0.0, 0.0}, {0.0, 0.1, 0.0});
    rig.Space.Registry().emplace<VelocityComponent>(rig.Hole, VelocityComponent{.Linear = glm::dvec3(0.0, 0.1, 0.0) * Units::kSpeedOfLight});
    CHECK(rig.Rate() == Approx(std::sqrt(alpha2)).epsilon(1e-12));

    rig.Place({1.5, 0.0, 0.0});
    CHECK(rig.Rate() == 0.0);
    rig.Place({r, 0.0, 0.0}, {0.0, 1.5, 0.0});
    CHECK(rig.Rate() == 0.0);
}

TEST_CASE("Entity clock rate is one without a hole or entity and the body value for geodesic bodies", "[simulation][relativity][script]")
{
    Rig flat(0.0, false);
    CHECK(flat.Rate() == 1.0);
    CHECK(EntityClockRate(flat.Space, 0) == 1.0);
    CHECK(EntityClockRate(flat.Space, 12345) == 1.0);

    Rig rig(0.0);
    rig.Place({12.0, 0.0, 0.0});
    rig.Space.Registry().emplace<GeodesicBodyComponent>(rig.Probe, GeodesicBodyComponent{.Launch = GeodesicLaunch::Orbiting});
    GeodesicSimulationModule module;
    module.OnTick(rig.Space, 1.0f);
    const auto& body = rig.Space.Registry().get<GeodesicBodyComponent>(rig.Probe);
    REQUIRE(body.Status == GeodesicBodyStatus::FreeFall);
    CHECK(rig.Rate() == body.ClockRate);
    CHECK(rig.Rate() == Approx(std::sqrt(1.0 - 3.0 / 12.0)).epsilon(1e-6));
}
