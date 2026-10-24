#include <catch2/catch_test_macros.hpp>

import MoleHole;
import std;

using namespace MoleHole;

TEST_CASE("Speed presets match the current time scale", "[ui][transport]")
{
    CHECK(MatchSpeedPreset(1.0) == 1);
    CHECK(MatchSpeedPreset(0.1) == 0);
    CHECK(MatchSpeedPreset(100.0) == 3);
    CHECK_FALSE(MatchSpeedPreset(2.5));
    CHECK_FALSE(MatchSpeedPreset(10.001));
}

TEST_CASE("Slower and faster walk the presets and clamp at the ends", "[ui][transport]")
{
    CHECK(NextSpeedPreset(1.0, 1) == 10.0);
    CHECK(NextSpeedPreset(10.0, -1) == 1.0);
    CHECK(NextSpeedPreset(100.0, 1) == 100.0);
    CHECK(NextSpeedPreset(0.1, -1) == 0.1);
    CHECK(NextSpeedPreset(3.0, 1) == 10.0);
    CHECK(NextSpeedPreset(3.0, -1) == 1.0);
    CHECK(NextSpeedPreset(1.0e6, 1) == 100.0);
    CHECK(NextSpeedPreset(1.0e-6, -1) == 0.1);
    CHECK(NextSpeedPreset(1.0e-6, 1) == 0.1);
    CHECK(NextSpeedPreset(7.0, 0) == 7.0);
}

TEST_CASE("Speeds format compactly", "[ui][transport]")
{
    CHECK(FormatSpeed(1.0) == "x1");
    CHECK(FormatSpeed(0.1) == "x0.1");
    CHECK(FormatSpeed(100.0) == "x100");
    CHECK(FormatSpeed(2.5) == "x2.5");
    CHECK(FormatSpeed(1.0e9) == "x1e+09");
    CHECK(FormatSpeed(std::numeric_limits<double>::quiet_NaN()) == "x?");
}

TEST_CASE("Jump times parse with units, plain numbers are seconds", "[ui][transport]")
{
    CHECK(ParseJumpTime("1.5 d") == 1.5 * 86400.0);
    CHECK(ParseJumpTime("3600") == 3600.0);
    CHECK(ParseJumpTime("  2 h ") == 7200.0);
    CHECK(ParseJumpTime("-10 s") == -10.0);
    CHECK_FALSE(ParseJumpTime(""));
    CHECK_FALSE(ParseJumpTime("soon"));
    CHECK_FALSE(ParseJumpTime("5 m"));
    CHECK_FALSE(ParseJumpTime("nan"));
}

TEST_CASE("Step counts are clamped", "[ui][transport]")
{
    CHECK(StepTicks(false, 10) == 1);
    CHECK(StepTicks(true, 10) == 10);
    CHECK(StepTicks(true, 0) == 1);
    CHECK(StepTicks(true, -4) == 1);
    CHECK(StepTicks(true, 1'000'000) == kMaxStepTicks);
    CHECK(ClampStepCount(25) == 25);
}

TEST_CASE("The transport collapses to the richest tier that fits", "[ui][transport]")
{
    const std::array<float, 4> widths{800.0f, 600.0f, 300.0f, 40.0f};
    CHECK(PickTransportTier(1000.0f, widths) == 0);
    CHECK(PickTransportTier(800.0f, widths) == 0);
    CHECK(PickTransportTier(799.0f, widths) == 1);
    CHECK(PickTransportTier(350.0f, widths) == 2);
    CHECK(PickTransportTier(100.0f, widths) == 3);
    CHECK(PickTransportTier(0.0f, widths) == 3);
}

TEST_CASE("The bookmark ring keeps the newest entries", "[ui][transport]")
{
    BookmarkRing<int> ring(3);
    CHECK(ring.Size() == 0);
    CHECK(ring.At(0) == nullptr);
    for (int i = 1; i <= 5; ++i) ring.Push(i);
    REQUIRE(ring.Size() == 3);
    CHECK(*ring.At(0) == 3);
    CHECK(*ring.At(2) == 5);
    CHECK(ring.At(3) == nullptr);
    ring.Clear();
    CHECK(ring.Size() == 0);
    CHECK(BookmarkRing<int>().Capacity() == kMaxBookmarks);
}

TEST_CASE("Transport requests are taken once and in order", "[ui][transport]")
{
    TransportRequests requests;
    requests.Push({TransportRequest::Kind::Step, 3});
    requests.Push({TransportRequest::Kind::Restore, 1, 2});
    const auto taken = requests.Take();
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].What == TransportRequest::Kind::Step);
    CHECK(taken[0].Ticks == 3);
    CHECK(taken[1].Index == 2);
    CHECK(requests.Take().empty());
}

TEST_CASE("Bookmarks are named by their simulation time", "[ui][transport]")
{
    CHECK(BookmarkLabel(0.0) == "0 s");
    CHECK(BookmarkLabel(1.5) == "1.500 s");
}
