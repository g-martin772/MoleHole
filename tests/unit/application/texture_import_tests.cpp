#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;

namespace
{
    std::filesystem::path FreshDirectory(const std::string& name)
    {
        const auto path = std::filesystem::temp_directory_path()
                          / std::format("molehole_{}_{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(path);
        return path;
    }

    void WritePng(const std::filesystem::path& path, const int width, const int height, const std::uint8_t value)
    {
        std::filesystem::create_directories(path.parent_path());
        const std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 3, value);
        REQUIRE(stbi_write_png(path.string().c_str(), width, height, 3, pixels.data(), width * 3) != 0);
    }
}

TEST_CASE("Images outside the project are copied in and keep a unique name", "[application][textures]")
{
    const auto project = FreshDirectory("project");
    const auto outside = FreshDirectory("outside");
    WritePng(outside / "planet.png", 8, 4, 10);

    const auto first = ImportTexture(outside / "planet.png", project);
    CHECK(first.Error.empty());
    CHECK(first.Copied);
    CHECK(first.AssetPath == "assets/textures/imported/planet.png");
    CHECK(first.Warnings.empty());
    CHECK(std::filesystem::exists(project / first.AssetPath));

    // The same file again reuses the copy
    const auto again = ImportTexture(outside / "planet.png", project);
    CHECK_FALSE(again.Copied);
    CHECK(again.AssetPath == first.AssetPath);

    // A different image with the same name gets a suffix
    const auto other = FreshDirectory("other");
    WritePng(other / "planet.png", 8, 4, 200);
    const auto second = ImportTexture(other / "planet.png", project);
    CHECK(second.Copied);
    CHECK(second.AssetPath == "assets/textures/imported/planet_1.png");

    std::filesystem::remove_all(project);
    std::filesystem::remove_all(outside);
    std::filesystem::remove_all(other);
}

TEST_CASE("Images inside the project are referenced where they are", "[application][textures]")
{
    const auto project = FreshDirectory("project");
    WritePng(project / "assets" / "textures" / "earth" / "albedo.png", 6, 4, 50);
    const auto result = ImportTexture(project / "assets" / "textures" / "earth" / "albedo.png", project);
    CHECK_FALSE(result.Copied);
    CHECK(result.AssetPath == "assets/textures/earth/albedo.png");
    REQUIRE(result.Warnings.size() == 1); // 6x4 is not an equirectangular 2:1 map
    std::filesystem::remove_all(project);
}

TEST_CASE("Unreadable and unsupported images are refused", "[application][textures]")
{
    const auto project = FreshDirectory("project");
    const auto tiff = project / "map.tif";
    std::ofstream(tiff) << "not decoded by stb";
    const auto unsupported = ImportTexture(tiff, project);
    CHECK(unsupported.AssetPath.empty());
    CHECK(unsupported.Error.find("TIFF") != std::string::npos);

    const auto broken = project / "broken.png";
    std::ofstream(broken) << "garbage";
    CHECK_FALSE(ImportTexture(broken, project).Error.empty());
    CHECK_FALSE(ImportTexture(project / "missing.png", project).Error.empty());
    std::filesystem::remove_all(project);
}
