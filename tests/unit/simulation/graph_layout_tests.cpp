#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import MoleHole;
import glm;
import std;

using namespace MoleHole;

namespace
{
    std::vector<NodeRect> Sample()
    {
        return {{1, {0.0f, 0.0f}, {100.0f, 50.0f}}, {2, {300.0f, 40.0f}, {60.0f, 30.0f}}, {3, {120.0f, 200.0f}, {80.0f, 80.0f}}};
    }

    const NodeRect& ById(const std::vector<NodeRect>& rects, const int id)
    {
        return *std::ranges::find(rects, id, &NodeRect::Id);
    }
}

TEST_CASE("Align moves rects to a shared edge", "[graph][layout]")
{
    const auto left = AlignRects(Sample(), AlignMode::Left);
    for (const auto& r : left) CHECK(r.Position.x == 0.0f);

    const auto right = AlignRects(Sample(), AlignMode::Right);
    for (const auto& r : right) CHECK(r.Position.x + r.Size.x == Catch::Approx(360.0f));

    const auto bottom = AlignRects(Sample(), AlignMode::Bottom);
    for (const auto& r : bottom) CHECK(r.Position.y + r.Size.y == Catch::Approx(280.0f));

    const auto centered = AlignRects(Sample(), AlignMode::CenterHorizontal);
    for (const auto& r : centered) CHECK(r.Position.x + r.Size.x * 0.5f == Catch::Approx(180.0f));
}

TEST_CASE("Align with fewer than two rects is a no-op", "[graph][layout]")
{
    std::vector<NodeRect> one = {{1, {5.0f, 6.0f}, {10.0f, 10.0f}}};
    CHECK(AlignRects(one, AlignMode::Left)[0].Position == glm::vec2(5.0f, 6.0f));
}

TEST_CASE("Distribute equalises gaps and keeps the outer rects fixed", "[graph][layout]")
{
    const auto out = DistributeRects(Sample(), true);
    REQUIRE(out.size() == 3);
    CHECK(ById(out, 1).Position.x == 0.0f);
    CHECK(ById(out, 2).Position.x == Catch::Approx(300.0f));
    const float gapA = ById(out, 3).Position.x - (ById(out, 1).Position.x + ById(out, 1).Size.x);
    const float gapB = ById(out, 2).Position.x - (ById(out, 3).Position.x + ById(out, 3).Size.x);
    CHECK(gapA == Catch::Approx(gapB));
    CHECK(DistributeRects({Sample()[0], Sample()[1]}, true)[1].Position.x == 300.0f);
}

TEST_CASE("BoundsOf pads and reserves a title strip", "[graph][layout]")
{
    const auto [pos, size] = BoundsOf(Sample(), 10.0f, 20.0f);
    CHECK(pos == glm::vec2(-10.0f, -30.0f));
    CHECK(size == glm::vec2(380.0f, 320.0f));
}

TEST_CASE("Comments contain nodes whose centre lies inside", "[graph][layout]")
{
    Comment comment;
    comment.Position = {-5.0f, -5.0f};
    comment.Size = {200.0f, 100.0f};
    const auto ids = NodesInsideComment(comment, Sample());
    REQUIRE(ids.size() == 1);
    CHECK(ids[0] == 1);
}
