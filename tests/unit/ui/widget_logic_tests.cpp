#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

import MoleHole;
import glm;
import std;

using namespace MoleHole;
using Catch::Matchers::WithinAbs;

TEST_CASE("Easing hits its endpoints and stays monotonic", "[ui][widgets]")
{
    CHECK(EaseOutCubic(0.0f) == 0.0f);
    CHECK(EaseOutCubic(1.0f) == 1.0f);
    CHECK(EaseInOutCubic(0.0f) == 0.0f);
    CHECK_THAT(EaseInOutCubic(0.5f), WithinAbs(0.5f, 1e-6));
    CHECK(EaseInOutCubic(2.0f) == 1.0f);
    float previous = -1.0f;
    for (int i = 0; i <= 20; ++i)
    {
        const float v = EaseInOutCubic(static_cast<float>(i) / 20.0f);
        CHECK(v >= previous);
        previous = v;
    }
}

TEST_CASE("ApproachExp is frame-rate independent", "[ui][widgets]")
{
    float coarse = 0.0f;
    for (int i = 0; i < 10; ++i) coarse = ApproachExp(coarse, 1.0f, 10.0f, 0.05f);
    float fine = 0.0f;
    for (int i = 0; i < 100; ++i) fine = ApproachExp(fine, 1.0f, 10.0f, 0.005f);
    CHECK_THAT(coarse, WithinAbs(fine, 1e-4));
    CHECK(ApproachExp(0.3f, 1.0f, 10.0f, 0.0f) == 0.3f);
}

TEST_CASE("AnimatedToggle settles at its target in the configured duration", "[ui][widgets]")
{
    AnimatedToggle t;
    for (int i = 0; i < 100; ++i) t.Update(true, 0.01f, 0.18f);
    CHECK(t.Settled(true));
    CHECK(t.Eased() == 1.0f);
    for (int i = 0; i < 5; ++i) t.Update(false, 0.01f, 0.18f);
    CHECK_FALSE(t.Settled(false));
    CHECK(t.Eased() < 1.0f);
}

TEST_CASE("Colour helpers", "[ui][widgets]")
{
    const auto c = HexColor(0xff8000);
    CHECK_THAT(c.r, WithinAbs(1.0f, 1e-6));
    CHECK_THAT(c.g, WithinAbs(128.0f / 255.0f, 1e-6));
    CHECK(LerpColor(glm::vec4(0.0f), glm::vec4(1.0f), 0.5f).r == 0.5f);
    CHECK(LerpColor(glm::vec4(0.0f), glm::vec4(1.0f), 5.0f).r == 1.0f);
    CHECK(Lighten(c, 1.0f).g == 1.0f);
    CHECK(Darken(c, 1.0f).r == 0.0f);
    CHECK(Luminance(glm::vec4(1.0f)) > 0.99f);
}

TEST_CASE("Name and text helpers", "[ui][widgets]")
{
    CHECK(SpacedName("RigidBody") == "Rigid Body");
    CHECK(SpacedName("ABC") == "ABC");
    CHECK(ContainsInsensitive("Black Hole", "hOLe"));
    CHECK_FALSE(ContainsInsensitive("Sphere", "cube"));
    CHECK(ContainsInsensitive("x", ""));
    CHECK(Utf8Encode(0xf111) == "\xef\x84\x91");
    CHECK(Utf8Encode('a') == "a");
    CHECK(IconCodepointForType("Mesh") != IconCodepointForType("Sphere"));
}

namespace
{
    std::vector<OutlinerEntry> SampleScene()
    {
        return {
            {1, 0, "Root", "Sphere"},
            {2, 1, "Child", "Mesh"},
            {3, 2, "Grandchild", "BlackHole"},
            {4, 0, "Other", "Sphere"},
        };
    }
}

TEST_CASE("Outliner rows respect expansion and depth", "[ui][outliner]")
{
    const auto entries = SampleScene();
    auto rows = BuildOutlinerRows(entries, {}, "");
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].HasChildren);
    CHECK_FALSE(rows[0].Expanded);

    rows = BuildOutlinerRows(entries, {1, 2}, "");
    REQUIRE(rows.size() == 4);
    CHECK(rows[2].Guid == 3);
    CHECK(rows[2].Depth == 2);
    CHECK(rows[3].Guid == 4);
}

TEST_CASE("Outliner filter keeps matches with their ancestors and expands them", "[ui][outliner]")
{
    const auto rows = BuildOutlinerRows(SampleScene(), {}, "grand");
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].Guid == 1);
    CHECK(rows[2].Guid == 3);
    CHECK(BuildOutlinerRows(SampleScene(), {}, "nomatch").empty());
}

TEST_CASE("Outliner survives missing parents and cycles", "[ui][outliner]")
{
    std::vector<OutlinerEntry> entries{
        {1, 99, "Orphan", ""},
        {2, 3, "A", ""},
        {3, 2, "B", ""},
    };
    const auto rows = BuildOutlinerRows(entries, {2, 3}, "");
    CHECK(rows.size() == 3);
}

TEST_CASE("CanReparent rejects self and descendants", "[ui][outliner]")
{
    const auto entries = SampleScene();
    CHECK(CanReparent(entries, 3, 0));
    CHECK(CanReparent(entries, 3, 4));
    CHECK_FALSE(CanReparent(entries, 1, 1));
    CHECK_FALSE(CanReparent(entries, 1, 3));
    CHECK_FALSE(CanReparent(entries, 1, 42));
}

TEST_CASE("RowRange returns the visible span in order either direction", "[ui][outliner]")
{
    const auto rows = BuildOutlinerRows(SampleScene(), {1, 2}, "");
    CHECK(RowRange(rows, 2, 4) == std::vector<std::uint64_t>{2, 3, 4});
    CHECK(RowRange(rows, 4, 2) == std::vector<std::uint64_t>{2, 3, 4});
    CHECK(RowRange(rows, 99, 3) == std::vector<std::uint64_t>{3});
}

TEST_CASE("OrphanedChildren lists surviving children of removed entities", "[ui][outliner]")
{
    const auto orphans = OrphanedChildren(SampleScene(), {1, 2});
    CHECK(orphans == std::vector<std::uint64_t>{3});
}
