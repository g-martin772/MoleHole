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
                        std::format("molehole_dirty_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        TempDir() { fs::create_directories(Root); }
        ~TempDir()
        {
            std::error_code ec;
            fs::permissions(Root, fs::perms::owner_all, fs::perm_options::replace, ec);
            fs::remove_all(Root, ec);
        }
    };

    std::string Slurp(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    SceneAction Open(std::string path) { return {SceneAction::Kind::OpenFile, std::move(path)}; }
}

TEST_CASE("SceneDirty compares the live revision with the saved one", "[scene-dirty]")
{
    SceneDirty state;
    CHECK_FALSE(state.IsDirty());

    state.Observe(3);
    CHECK(state.IsDirty());
    state.MarkSaved(3);
    CHECK_FALSE(state.IsDirty());

    state.Observe(4);
    CHECK(state.IsDirty());
    state.Observe(3);
    CHECK_FALSE(state.IsDirty());
}

TEST_CASE("SceneDirty reset adopts a new baseline", "[scene-dirty]")
{
    SceneDirty state;
    state.Observe(9);
    REQUIRE(state.IsDirty());
    state.Reset(0);
    CHECK_FALSE(state.IsDirty());
}

TEST_CASE("A save that is not exact leaves the scene modified", "[scene-dirty]")
{
    SceneDirty state;
    state.Observe(2);
    state.MarkSaved(2, false);
    CHECK(state.IsDirty());
    state.MarkSaved(2);
    CHECK_FALSE(state.IsDirty());
}

TEST_CASE("UnsavedPrompt lets actions through when nothing is modified", "[scene-dirty][prompt]")
{
    UnsavedPrompt prompt;
    CHECK(prompt.Request(Open("a.yaml"), false));
    CHECK_FALSE(prompt.Open());
}

TEST_CASE("UnsavedPrompt holds the action until the user decides", "[scene-dirty][prompt]")
{
    UnsavedPrompt prompt;
    REQUIRE_FALSE(prompt.Request(Open("a.yaml"), true));
    REQUIRE(prompt.Open());
    CHECK(prompt.Pending().Path == "a.yaml");

    SECTION("discard runs the action")
    {
        const auto action = prompt.Resolve(UnsavedChoice::Discard);
        REQUIRE(action);
        CHECK(action->What == SceneAction::Kind::OpenFile);
        CHECK_FALSE(prompt.Open());
    }
    SECTION("cancel drops it")
    {
        CHECK_FALSE(prompt.Resolve(UnsavedChoice::Cancel));
        CHECK_FALSE(prompt.Open());
    }
    SECTION("save runs it only after a successful save")
    {
        CHECK_FALSE(prompt.Resolve(UnsavedChoice::Save, false));
        CHECK(prompt.Open());
        const auto action = prompt.Resolve(UnsavedChoice::Save, true);
        REQUIRE(action);
        CHECK(action->Path == "a.yaml");
        CHECK_FALSE(prompt.Open());
    }
}

TEST_CASE("Only a quit request replaces the pending action", "[scene-dirty][prompt]")
{
    UnsavedPrompt prompt;
    REQUIRE_FALSE(prompt.Request(Open("a.yaml"), true));
    CHECK_FALSE(prompt.Request(Open("b.yaml"), true));
    CHECK(prompt.Pending().Path == "a.yaml");
    CHECK_FALSE(prompt.Request({SceneAction::Kind::Quit, {}}, true));
    CHECK(prompt.Pending().What == SceneAction::Kind::Quit);
}

TEST_CASE("SaveSceneFile replaces the file and leaves no temporary", "[scene-dirty][save]")
{
    TempDir dir;
    const auto file = dir.Root / "scene.yaml";
    REQUIRE(SaveSceneFile(file, "old: 1\n"));
    REQUIRE(SaveSceneFile(file, "new: 2\n"));
    CHECK(Slurp(file) == "new: 2\n");

    std::size_t entries = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(dir.Root)) ++entries;
    CHECK(entries == 1);
}

TEST_CASE("A failed save keeps the previous file intact", "[scene-dirty][save]")
{
    TempDir dir;
    const auto locked = dir.Root / "locked";
    fs::create_directories(locked);
    const auto file = locked / "scene.yaml";
    REQUIRE(SaveSceneFile(file, "precious\n"));

    fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_exec, fs::perm_options::replace);
    const auto result = SaveSceneFile(file, "replacement\n");
    fs::permissions(locked, fs::perms::owner_all, fs::perm_options::replace);

    if (::geteuid() != 0)
    {
        CHECK_FALSE(result);
        if (!result) CHECK_FALSE(result.error().empty());
        CHECK(Slurp(file) == "precious\n");
    }
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(locked))
        CHECK(entry.path().extension() != ".tmp");
}
