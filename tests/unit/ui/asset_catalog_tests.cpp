#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;
using GPP::AssetFieldKind;

namespace
{
    std::filesystem::path FreshDirectory(const std::string& name)
    {
        const auto path = std::filesystem::temp_directory_path()
                          / std::format("molehole_{}_{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(path);
        return path;
    }

    void Touch(const std::filesystem::path& path, const std::string& content = "x")
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << content;
    }

    std::vector<AssetEntry> Entries(std::initializer_list<const char*> paths)
    {
        std::vector<AssetEntry> result;
        for (const auto* path : paths) result.push_back({path, 10});
        return result;
    }
}

TEST_CASE("AssetMatchesKind filters by extension case-insensitively", "[ui][assets]")
{
    CHECK(AssetMatchesKind("a/Earth.PNG", AssetFieldKind::Texture));
    CHECK(AssetMatchesKind("a/b.jpg", AssetFieldKind::Texture));
    CHECK_FALSE(AssetMatchesKind("a/b.gltf", AssetFieldKind::Texture));
    CHECK(AssetMatchesKind("a/scene.glb", AssetFieldKind::Mesh));
    CHECK_FALSE(AssetMatchesKind("a/b.png", AssetFieldKind::Mesh));
    CHECK(AssetMatchesKind("scripts/x.luau", AssetFieldKind::Script));
    CHECK(AssetMatchesKind("a/b.ogg", AssetFieldKind::Audio));
    CHECK(AssetMatchesKind("anything.bin", AssetFieldKind::Any));
}

TEST_CASE("NormalizeAssetPath is project-relative inside the project and absolute outside", "[ui][assets]")
{
    const auto project = FreshDirectory("norm_project");
    const auto outside = FreshDirectory("norm_outside");
    Touch(project / "assets" / "textures" / "a.png");
    Touch(outside / "b.png");

    CHECK(NormalizeAssetPath(project / "assets" / "textures" / "a.png", project) == "assets/textures/a.png");
    CHECK(NormalizeAssetPath(project / "assets" / ".." / "assets" / "textures" / "a.png", project) == "assets/textures/a.png");
    const auto absolute = NormalizeAssetPath(outside / "b.png", project);
    CHECK(absolute.starts_with('/'));
    CHECK(absolute.ends_with("b.png"));
}

TEST_CASE("ScanAssets lists only the requested kind, sorted, skipping hidden files", "[ui][assets]")
{
    const auto project = FreshDirectory("scan_project");
    Touch(project / "assets" / "textures" / "b.png", "12345");
    Touch(project / "assets" / "textures" / "a.JPG");
    Touch(project / "assets" / "textures" / "notes.txt");
    Touch(project / "assets" / "textures" / ".hidden.png");
    Touch(project / "assets" / "models" / "ship" / "scene.gltf");
    Touch(project / "assets" / ".git" / "x.png");

    const AssetScanConfig config{{project / "assets", project / "assets" / "textures", project / "missing"}, project};
    const auto textures = ScanAssets(config, AssetFieldKind::Texture);
    REQUIRE(textures.size() == 2);
    CHECK(textures[0].Path == "assets/textures/a.JPG");
    CHECK(textures[1].Path == "assets/textures/b.png");
    CHECK(textures[1].Size == 5);

    const auto meshes = ScanAssets(config, AssetFieldKind::Mesh);
    REQUIRE(meshes.size() == 1);
    CHECK(meshes[0].Path == "assets/models/ship/scene.gltf");
    CHECK(ScanAssets(config, AssetFieldKind::Any).size() == 4);
}

TEST_CASE("RankAssets orders by match quality and boosts recents", "[ui][assets]")
{
    const auto entries = Entries({"assets/textures/earth/albedo.png", "assets/textures/mars.png",
                                  "assets/textures/earth.png", "assets/models/ship.gltf"});

    const auto earth = RankAssets(entries, "earth");
    REQUIRE(earth.size() == 2);
    CHECK(earth[0] == 2);

    CHECK(RankAssets(entries, "zzz").empty());
    CHECK(RankAssets(entries, "  ").size() == entries.size());

    const std::vector<std::string> recents{"assets/models/ship.gltf", "assets/textures/mars.png"};
    const auto all = RankAssets(entries, "", recents);
    REQUIRE(all.size() == 4);
    CHECK(all[0] == 3);
    CHECK(all[1] == 1);
    CHECK(all[2] == 0);
    CHECK(all[3] == 2);
}

TEST_CASE("ListAssetFolder groups entries by directory", "[ui][assets]")
{
    const auto entries = Entries({"assets/textures/a.png", "assets/textures/earth/albedo.png",
                                  "assets/textures/earth/normal.png", "assets/models/m.glb", "top.png"});

    const auto root = ListAssetFolder(entries, "");
    CHECK(root.Files == std::vector<std::size_t>{4});
    REQUIRE(root.Folders.size() == 1);
    CHECK(root.Folders[0] == std::pair<std::string, std::size_t>{"assets", 4});

    const auto textures = ListAssetFolder(entries, "assets/textures");
    CHECK(textures.Files == std::vector<std::size_t>{0});
    REQUIRE(textures.Folders.size() == 1);
    CHECK(textures.Folders[0] == std::pair<std::string, std::size_t>{"earth", 2});

    CHECK(ListAssetFolder(entries, "nothing").Files.empty());
}

TEST_CASE("Recent assets keep the newest first without duplicates", "[ui][assets]")
{
    std::vector<std::string> recents;
    recents = PushRecentAsset(recents, "a");
    recents = PushRecentAsset(recents, "b");
    recents = PushRecentAsset(recents, "a");
    CHECK(recents == std::vector<std::string>{"a", "b"});
    recents = PushRecentAsset(recents, "c", 2);
    CHECK(recents == std::vector<std::string>{"c", "a"});
    CHECK(PushRecentAsset(recents, "").size() == 2);
}

TEST_CASE("FormatAssetSize picks a readable unit", "[ui][assets]")
{
    CHECK(FormatAssetSize(12) == "12 B");
    CHECK(FormatAssetSize(2048) == "2 KB");
    CHECK(FormatAssetSize(5ull * 1024 * 1024) == "5.0 MB");
}

TEST_CASE("AssetCatalog scans on a worker thread and picks up new files on refresh", "[ui][assets]")
{
    const auto project = FreshDirectory("catalog_project");
    Touch(project / "assets" / "a.png");
    const AssetScanConfig config{{project / "assets"}, project};

    AssetCatalog catalog;
    auto wait = [&](const bool refresh)
    {
        auto snapshot = catalog.Get(AssetFieldKind::Texture, config, refresh);
        for (int i = 0; i < 400 && (snapshot.Scanning || !snapshot.Items); ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            snapshot = catalog.Get(AssetFieldKind::Texture, config);
        }
        return snapshot;
    };

    const auto first = wait(false);
    REQUIRE(first.Items);
    CHECK(first.Items->size() == 1);

    Touch(project / "assets" / "b.png");
    const auto second = wait(true);
    REQUIRE(second.Items);
    CHECK(second.Items->size() == 2);
}
