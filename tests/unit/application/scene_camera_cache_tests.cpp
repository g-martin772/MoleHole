#include <catch2/catch_test_macros.hpp>
#include <unistd.h>

import MoleHole;
import GPP;
import std;

using namespace MoleHole;
namespace fs = std::filesystem;

namespace
{
    struct TempDir
    {
        fs::path Root = fs::temp_directory_path() /
                        std::format("molehole_cams_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        TempDir() { fs::create_directories(Root); }
        ~TempDir()
        {
            std::error_code ec;
            fs::permissions(Root, fs::perms::owner_all, fs::perm_options::replace, ec);
            fs::remove_all(Root, ec);
        }
    };

    SceneCameraPose Pose(const double x, const float yaw = 10.0f)
    {
        return {{x, 2.0, 3.0}, yaw, -5.0f, 1500.0, 0};
    }

    fs::path Touch(const fs::path& path)
    {
        std::ofstream(path) << "x";
        return path;
    }
}

TEST_CASE("Scene camera cache round-trips poses", "[scene-camera]")
{
    TempDir dir;
    const auto file = dir.Root / "cams.yaml";
    SceneCameraCache cache(file);
    cache.Store("/a/scene.yaml", Pose(1.0), 100);
    cache.Store("/b/scene.yaml", Pose(2.0, 45.0f), 200);
    REQUIRE(cache.Save());
    CHECK_FALSE(cache.Dirty());

    SceneCameraCache loaded(file);
    CHECK(loaded.Load().empty());
    CHECK(loaded.Size() == 2);
    const auto b = loaded.Lookup("/b/scene.yaml");
    REQUIRE(b);
    CHECK(b->PositionM.x == 2.0);
    CHECK(b->Yaw == 45.0f);
    CHECK(b->Pitch == -5.0f);
    CHECK(b->SpeedMs == 1500.0);
    CHECK(b->SavedAt == 200);
    CHECK_FALSE(loaded.Lookup("/c/scene.yaml"));
}

TEST_CASE("Scene camera cache keys are canonical", "[scene-camera]")
{
    TempDir dir;
    const auto scene = Touch(dir.Root / "scene.yaml");
    fs::create_directories(dir.Root / "sub");
    const auto roundabout = dir.Root / "sub" / ".." / "scene.yaml";
    CHECK(SceneCameraCache::KeyFor(scene) == SceneCameraCache::KeyFor(roundabout));
    CHECK(SceneCameraCache::KeyFor(dir.Root / "missing" / "gone.yaml") ==
          SceneCameraCache::KeyFor(dir.Root / "missing" / "." / "gone.yaml"));
    CHECK(SceneCameraCache::KeyFor("").empty());
}

TEST_CASE("Scene camera cache tolerates a corrupt or missing file", "[scene-camera]")
{
    TempDir dir;
    SceneCameraCache missing(dir.Root / "nope" / "cams.yaml");
    CHECK(missing.Load().empty());
    CHECK(missing.Size() == 0);

    const auto file = dir.Root / "cams.yaml";
    std::ofstream(file) << "Scenes: [unterminated\n  - : {";
    SceneCameraCache corrupt(file);
    CHECK_FALSE(corrupt.Load().empty());
    CHECK(corrupt.Size() == 0);

    std::ofstream(file) << "Scenes:\n"
                           "  - Path: /ok\n    PositionM: [1, 2, 3]\n    Yaw: 4\n"
                           "  - Path: /bad\n    PositionM: [1, 2]\n"
                           "  - Path: /nan\n    PositionM: [.nan, 2, 3]\n"
                           "  - garbage\n"
                           "  - Path: /words\n    PositionM: [a, b, c]\n";
    SceneCameraCache partial(file);
    CHECK(partial.Load().empty());
    CHECK(partial.Size() == 1);
    CHECK(partial.Lookup("/ok"));

    std::ofstream(file) << "just a string";
    SceneCameraCache scalar(file);
    CHECK_FALSE(scalar.Load().empty());
    CHECK(scalar.Size() == 0);
}

TEST_CASE("Scene camera cache save creates the directory and reports unwritable ones", "[scene-camera]")
{
    TempDir dir;
    SceneCameraCache nested(dir.Root / "a" / "b" / "cams.yaml");
    nested.Store("/s", Pose(1.0), 1);
    REQUIRE(nested.Save());
    CHECK(fs::exists(dir.Root / "a" / "b" / "cams.yaml"));

    const auto locked = dir.Root / "locked";
    fs::create_directories(locked);
    fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    SceneCameraCache readOnly(locked / "cams.yaml");
    readOnly.Store("/s", Pose(1.0), 1);
    const auto result = readOnly.Save();
    fs::permissions(locked, fs::perms::owner_all, fs::perm_options::replace);
    if (::geteuid() != 0)
    {
        CHECK_FALSE(result);
        CHECK(readOnly.Dirty());
    }
}

TEST_CASE("Scene camera cache storing an unchanged pose does not dirty it", "[scene-camera]")
{
    TempDir dir;
    SceneCameraCache cache(dir.Root / "cams.yaml");
    cache.Store("/s", Pose(1.0), 1);
    REQUIRE(cache.Save());
    cache.Store("/s", Pose(1.0), 99);
    CHECK_FALSE(cache.Dirty());
    cache.Store("/s", Pose(2.0), 100);
    CHECK(cache.Dirty());
}

TEST_CASE("Scene camera cache evicts the least recently saved beyond capacity", "[scene-camera]")
{
    SceneCameraCache cache({}, 3);
    cache.Store("/1", Pose(1.0), 10);
    cache.Store("/2", Pose(2.0), 20);
    cache.Store("/3", Pose(3.0), 30);
    cache.Store("/1", Pose(1.5), 40);
    cache.Store("/4", Pose(4.0), 50);
    CHECK(cache.Size() == 3);
    CHECK_FALSE(cache.Lookup("/2"));
    CHECK(cache.Lookup("/1"));
    CHECK(cache.Lookup("/3"));
    CHECK(cache.Lookup("/4"));
}

TEST_CASE("Scene camera cache prunes entries whose scene is gone", "[scene-camera]")
{
    TempDir dir;
    const auto kept = SceneCameraCache::KeyFor(Touch(dir.Root / "kept.yaml"));
    const auto gone = SceneCameraCache::KeyFor(dir.Root / "gone.yaml");
    SceneCameraCache cache;
    cache.Store(kept, Pose(1.0), 1);
    cache.Store(gone, Pose(2.0), 2);
    CHECK(cache.Prune() == 1);
    CHECK(cache.Lookup(kept));
    CHECK_FALSE(cache.Lookup(gone));
}

TEST_CASE("Scene camera cache prune is bounded", "[scene-camera]")
{
    SceneCameraCache cache;
    for (int i = 0; i < 10; ++i) cache.Store(std::format("/definitely/missing/{}", i), Pose(i), i);
    CHECK(cache.Prune(4) == 4);
    CHECK(cache.Size() == 6);
}

TEST_CASE("Scene camera cache carries the pose to a renamed scene", "[scene-camera]")
{
    SceneCameraCache cache;
    cache.Store("/old.yaml", Pose(7.0), 5);
    CHECK(cache.Carry("/old.yaml", "/new.yaml", 9));
    const auto moved = cache.Lookup("/new.yaml");
    REQUIRE(moved);
    CHECK(moved->PositionM.x == 7.0);
    CHECK(moved->SavedAt == 9);
    CHECK(cache.Lookup("/old.yaml"));
    CHECK_FALSE(cache.Carry("/none.yaml", "/other.yaml", 9));
    CHECK_FALSE(cache.Carry("/old.yaml", "/old.yaml", 9));
    CHECK_FALSE(cache.Carry("", "/x.yaml", 9));
}
