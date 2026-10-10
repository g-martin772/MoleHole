#include <catch2/catch_test_macros.hpp>

import MoleHole;
import std;

TEST_CASE("Numbered export paths skip existing files", "[export]")
{
    const auto dir = std::filesystem::temp_directory_path() / "molehole_export_path_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);

    CHECK(MoleHole::NextNumberedPath(dir, "render", "png").filename() == "render_001.png");
    std::ofstream(dir / "render_001.png").put('x');
    std::ofstream(dir / "render_002.png").put('x');
    CHECK(MoleHole::NextNumberedPath(dir, "render", "png").filename() == "render_003.png");
    CHECK(MoleHole::NextNumberedPath(dir, "video", "mp4").filename() == "video_001.mp4");

    std::filesystem::remove_all(dir);
}
