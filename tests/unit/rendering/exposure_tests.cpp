#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

import MoleHole;
import std;

using namespace MoleHole;
using Catch::Matchers::WithinAbs;

TEST_CASE("The first render of an exposure meters exactly", "[rendering][exposure]")
{
    ExposureAdaptation adaptation;
    const auto first = adaptation.Next(12.5, 0.5, 1);
    CHECK(first.Alpha == 1.0f);
    CHECK_FALSE(first.Commit);
}

TEST_CASE("Adaptation weight follows a first-order response", "[rendering][exposure]")
{
    CHECK_THAT(ExposureAdaptation::Weight(0.5, 0.5), WithinAbs(1.0 - std::exp(-1.0), 1e-6));
    CHECK(ExposureAdaptation::Weight(0.0, 0.5) == 0.0f);
    CHECK(ExposureAdaptation::Weight(1.0, 0.0) == 1.0f);
    CHECK(ExposureAdaptation::Weight(100.0, 0.5) > 0.999f);
}

TEST_CASE("Advancing time commits the previous render and blends by the elapsed film time", "[rendering][exposure]")
{
    ExposureAdaptation adaptation;
    adaptation.Next(0.0, 0.5, 1);
    const auto second = adaptation.Next(1.0 / 24.0, 0.5, 1);
    CHECK(second.Commit);
    CHECK_THAT(second.Alpha, WithinAbs(ExposureAdaptation::Weight(1.0 / 24.0, 0.5), 1e-7));
}

TEST_CASE("Rendering a film frame again gives the same blend", "[rendering][exposure]")
{
    ExposureAdaptation adaptation;
    adaptation.Next(0.0, 0.5, 1);
    const auto once = adaptation.Next(0.1, 0.5, 1);
    const auto again = adaptation.Next(0.1, 0.5, 1);
    CHECK_FALSE(again.Commit);
    CHECK(again.Alpha == once.Alpha);
}

TEST_CASE("Re-rendering the first frame stays an exact meter", "[rendering][exposure]")
{
    ExposureAdaptation adaptation;
    adaptation.Next(3.0, 0.5, 1);
    const auto again = adaptation.Next(3.0, 0.5, 1);
    CHECK(again.Alpha == 1.0f);
    CHECK_FALSE(again.Commit);
}

TEST_CASE("A new context, a backwards jump or zero adaptation restart the meter", "[rendering][exposure]")
{
    ExposureAdaptation adaptation;
    adaptation.Next(0.0, 0.5, 1);
    adaptation.Next(1.0, 0.5, 1);
    CHECK(adaptation.Next(1.5, 0.5, 2).Alpha == 1.0f);
    adaptation.Next(2.0, 0.5, 2);
    CHECK(adaptation.Next(0.5, 0.5, 2).Alpha == 1.0f);
    adaptation.Next(1.0, 0.5, 2);
    CHECK(adaptation.Next(2.0, 0.0, 2).Alpha == 1.0f);
}

TEST_CASE("Reset forgets the history", "[rendering][exposure]")
{
    ExposureAdaptation adaptation;
    adaptation.Next(0.0, 0.5, 1);
    adaptation.Next(1.0, 0.5, 1);
    adaptation.Reset();
    CHECK(adaptation.Next(2.0, 0.5, 1).Alpha == 1.0f);
}

TEST_CASE("Exposure adaptation is a registered render setting", "[rendering][exposure]")
{
    const auto* setting = RenderSettingRegistry().Find("post.exposure_adaptation");
    REQUIRE(setting != nullptr);
    CHECK(RenderSettings{}.AutoExposureAdaptation == 0.5f);
}
