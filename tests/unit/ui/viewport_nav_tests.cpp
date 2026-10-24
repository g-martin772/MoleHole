#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import MoleHole;
import glm;
import std;

using namespace MoleHole;
using Catch::Approx;

TEST_CASE("Scrolling scales the fly speed geometrically and clamps", "[ui][viewport]")
{
    CHECK(ScaleFlySpeed(10.0, 0.0) == Approx(10.0));
    CHECK(ScaleFlySpeed(10.0, 1.0) == Approx(10.0 * kFlySpeedStep));
    CHECK(ScaleFlySpeed(ScaleFlySpeed(10.0, 3.0), -3.0) == Approx(10.0));
    CHECK(ScaleFlySpeed(1.0e-3, -50.0) == kMinFlySpeedMs);
    CHECK(ScaleFlySpeed(1.0e11, 500.0) == kMaxFlySpeedMs);
    CHECK(ClampFlySpeed(std::numeric_limits<double>::quiet_NaN()) == kMinFlySpeedMs);
}

TEST_CASE("The fly speed readout names a velocity unit", "[ui][viewport]")
{
    const auto text = FormatFlySpeed(1500.0);
    CHECK(text.starts_with("Fly speed "));
    CHECK(text.size() > std::string("Fly speed ").size());
}

TEST_CASE("Pan scales with distance and field of view", "[ui][viewport]")
{
    CHECK(PanPerPixel(100.0, 90.0, 1000.0) == Approx(0.2));
    CHECK(PanPerPixel(200.0, 90.0, 1000.0) == Approx(0.4));
    CHECK(PanPerPixel(100.0, 90.0, 0.0) == 0.0);
    CHECK(PanPerPixel(0.0, 90.0, 1000.0) == 0.0);
}

TEST_CASE("EnclosingSphere covers every input sphere", "[ui][viewport]")
{
    CHECK_FALSE(EnclosingSphere({}).has_value());

    const std::vector<BoundingSphere> spheres{{{10.0, 0.0, 0.0}, 1.0}, {{-10.0, 0.0, 0.0}, 2.0}, {{0.0, 30.0, 0.0}, 0.5}};
    const auto bounds = EnclosingSphere(spheres);
    REQUIRE(bounds.has_value());
    for (const auto& sphere : spheres)
        CHECK(glm::distance(sphere.Center, bounds->Center) + sphere.Radius <= bounds->Radius + 1.0e-9);

    const auto single = EnclosingSphere(std::vector<BoundingSphere>{{{1.0, 2.0, 3.0}, 4.0}});
    REQUIRE(single.has_value());
    CHECK(single->Center == glm::dvec3(1.0, 2.0, 3.0));
    CHECK(single->Radius == Approx(4.0));
}

TEST_CASE("FrameDistance fits the sphere in the narrower field of view", "[ui][viewport]")
{
    const double wide = FrameDistance(10.0, 60.0, 2.0, 1.0);
    const double narrow = FrameDistance(10.0, 60.0, 0.5, 1.0);
    CHECK(wide == Approx(10.0 / std::sin(glm::radians(30.0))));
    CHECK(narrow > wide);
    CHECK(FrameDistance(10.0, 60.0, 2.0, 1.25) == Approx(wide * 1.25));
    CHECK(FrameDistance(0.0, 60.0, 1.0) == 0.0);
}

TEST_CASE("View requests are taken once, in order", "[ui][viewport]")
{
    ViewRequests requests;
    CHECK(requests.Take().empty());
    requests.Push({ViewRequest::Kind::FrameAll, 1});
    requests.Push({ViewRequest::Kind::FrameAll, 2});
    const auto taken = requests.Take();
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].Index == 1);
    CHECK(taken[1].Index == 2);
    CHECK(requests.Take().empty());
}
