#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;

namespace
{
    CameraBookmark Sample(const double seed)
    {
        return {{seed * 1.0e7 + 0.123456789012345, -seed * 3.3e9, seed / 7.0}, 12.3456789f + static_cast<float>(seed), -45.25f, 72.5f,
                seed * 1.0e4 + 0.1};
    }
}

TEST_CASE("Bookmark slots store, read and clear", "[application][bookmarks]")
{
    CameraBookmarks marks;
    CHECK(marks.Count() == 0);
    CHECK_FALSE(marks.Get(0).has_value());
    CHECK(marks.Set(0, Sample(1.0)));
    CHECK(marks.Set(8, Sample(2.0)));
    CHECK_FALSE(marks.Set(9, Sample(3.0)));
    CHECK(marks.Count() == 2);
    CHECK(marks.Get(8) == Sample(2.0));
    CHECK_FALSE(marks.Get(9).has_value());
    marks.Clear(0);
    CHECK(marks.Count() == 1);
}

TEST_CASE("Bookmarks round trip through YAML exactly", "[application][bookmarks]")
{
    CameraBookmarks marks;
    marks.Set(0, Sample(1.0));
    marks.Set(3, Sample(2.5));
    marks.Set(8, Sample(9.0));
    const auto text = marks.ToYaml();
    REQUIRE_FALSE(text.empty());
    CHECK(CameraBookmarks::FromNode(YAML::Load(text)) == marks);
}

TEST_CASE("Empty bookmarks serialise to nothing", "[application][bookmarks]")
{
    CHECK(CameraBookmarks{}.ToYaml().empty());
    CHECK(CameraBookmarks::FromNode(YAML::Node()) == CameraBookmarks{});
}

TEST_CASE("Bookmarks survive a scene save and load", "[application][bookmarks]")
{
    CameraBookmarks marks;
    marks.Set(1, Sample(4.0));
    marks.Set(6, Sample(5.0));

    GPP::Scene scene("Bookmarked");
    CHECK(CameraBookmarks::FromScene(scene).Count() == 0);
    scene.SetExtension(kCameraBookmarksKey, YAML::Load(marks.ToYaml()));
    const auto yaml = scene.SerializeToYaml();

    GPP::Scene loaded("Loaded");
    loaded.DeserializeFromYaml(yaml);
    CHECK(CameraBookmarks::FromScene(loaded) == marks);
}

TEST_CASE("Malformed bookmark entries are skipped", "[application][bookmarks]")
{
    const auto node = YAML::Load(R"(
- Slot: 0
  PositionM: [1, 2, 3]
- Slot: 10
  PositionM: [1, 2, 3]
- Slot: 2
  PositionM: [1, 2]
- Slot: 3
  PositionM: [.nan, 0, 0]
- Slot: 4
  PositionM: [1, 2, 3]
  Yaw: 20
- Slot: 4
  PositionM: [4, 5, 6]
- PositionM: [1, 2, 3]
)");
    const auto marks = CameraBookmarks::FromNode(node);
    CHECK(marks.Count() == 1);
    REQUIRE(marks.Get(3).has_value());
    CHECK(marks.Get(3)->PositionM == glm::dvec3(4.0, 5.0, 6.0));
}
