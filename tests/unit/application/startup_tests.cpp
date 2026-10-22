#include <catch2/catch_test_macros.hpp>

import MoleHole;
import GPP;
import std;

using namespace MoleHole;
namespace fs = std::filesystem;

namespace
{
    struct TempDir
    {
        fs::path Root;

        TempDir()
        {
            Root = fs::temp_directory_path() /
                ("molehole_startup_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            fs::create_directories(Root);
        }

        ~TempDir()
        {
            std::error_code error;
            fs::remove_all(Root, error);
        }
    };

    void Write(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream(path) << text;
    }

    StartupArgs Parse(std::vector<std::string> args)
    {
        return ParseStartupArgs(args);
    }

    StartupPrefs Prefs(const bool show, const bool exists)
    {
        return {.ShowLauncher = show, .LastScenePath = exists ? "/some/scene.yaml" : "", .LastSceneExists = exists};
    }
}

TEST_CASE("Startup args parse flags and the positional scene", "[startup]")
{
    CHECK(!Parse({}).Scene);
    CHECK(Parse({"--scene", "a.yaml"}).Scene == "a.yaml");
    CHECK(Parse({"a.yaml"}).Scene == "a.yaml");
    CHECK(Parse({"--template", "t.yaml"}).Template == "t.yaml");
    CHECK(Parse({"--new", "--skip-intro"}).New);
    CHECK(Parse({"--new", "--skip-intro"}).SkipIntro);
    CHECK(Parse({"--launcher"}).Launcher);
    CHECK(Parse({"--no-launcher"}).NoLauncher);

    const auto exporting = Parse({"--export-image", "out.png", "--width", "960", "--profile", "Flat", "--scene", "s.yaml"});
    CHECK(exporting.Export);
    CHECK(exporting.Scene == "s.yaml");
    CHECK(!exporting.Template);

    CHECK(!Parse({"--width", "960"}).Scene);
}

TEST_CASE("Startup mode selection", "[startup]")
{
    const auto mode = [](const StartupArgs& args, const StartupPrefs& prefs) { return SelectStartupMode(args, prefs); };

    SECTION("Explicit scene, template, new or export wins")
    {
        for (const auto& args : {Parse({"--scene", "a.yaml"}), Parse({"a.yaml"}), Parse({"--template", "t.yaml"}),
                                 Parse({"--new"}), Parse({"--export-image", "x.png"}),
                                 Parse({"--scene", "a.yaml", "--launcher"})})
        {
            CHECK(mode(args, Prefs(true, false)) == StartupMode::Editor);
            CHECK(mode(args, Prefs(true, true)) == StartupMode::Editor);
        }
    }
    SECTION("--launcher forces the launcher")
    {
        CHECK(mode(Parse({"--launcher"}), Prefs(false, true)) == StartupMode::Launcher);
    }
    SECTION("--no-launcher forces the editor")
    {
        CHECK(mode(Parse({"--no-launcher"}), Prefs(true, false)) == StartupMode::Editor);
    }
    SECTION("Preferences decide otherwise")
    {
        CHECK(mode(Parse({}), Prefs(true, true)) == StartupMode::Launcher);
        CHECK(mode(Parse({}), Prefs(true, false)) == StartupMode::Launcher);
        CHECK(mode(Parse({}), Prefs(false, false)) == StartupMode::Launcher);
        CHECK(mode(Parse({}), Prefs(false, true)) == StartupMode::Editor);
    }
}

TEST_CASE("Startup prefs are read from the state file", "[startup]")
{
    TempDir dir;
    const auto scene = dir.Root / "scene.yaml";
    Write(scene, "Entities: []\n");

    SECTION("Missing file gives defaults")
    {
        const auto prefs = ReadStartupPrefs(dir.Root / "none.yaml");
        CHECK(prefs.ShowLauncher);
        CHECK(prefs.LastScenePath.empty());
        CHECK(!prefs.LastSceneExists);
    }
    SECTION("Values and existence")
    {
        Write(dir.Root / "state.yaml", "LastScenePath: " + scene.string() + "\nShowLauncher: false\n");
        const auto prefs = ReadStartupPrefs(dir.Root / "state.yaml");
        CHECK(!prefs.ShowLauncher);
        CHECK(prefs.LastScenePath == scene.string());
        CHECK(prefs.LastSceneExists);
    }
    SECTION("Missing scene file and absent key")
    {
        Write(dir.Root / "state.yaml", "LastScenePath: " + (dir.Root / "gone.yaml").string() + "\n");
        const auto prefs = ReadStartupPrefs(dir.Root / "state.yaml");
        CHECK(prefs.ShowLauncher);
        CHECK(!prefs.LastSceneExists);
    }
    SECTION("Broken yaml gives defaults")
    {
        Write(dir.Root / "state.yaml", "LastScenePath: [unterminated\n");
        CHECK(ReadStartupPrefs(dir.Root / "state.yaml").ShowLauncher);
    }
}

TEST_CASE("Relative time text", "[startup]")
{
    constexpr std::int64_t now = 1'000'000'000;
    CHECK(RelativeTimeText(0, now).empty());
    CHECK(RelativeTimeText(now - 5, now) == "just now");
    CHECK(RelativeTimeText(now - 60, now) == "1 minute ago");
    CHECK(RelativeTimeText(now - 3 * 3600, now) == "3 hours ago");
    CHECK(RelativeTimeText(now - 2 * 86400, now) == "2 days ago");
    CHECK(RelativeTimeText(now - 90 * 86400, now) == "3 months ago");
}

TEST_CASE("State file recents migrate from the legacy list", "[startup][state]")
{
    using namespace GPP;
    TempDir dir;
    const auto sceneA = dir.Root / "a.yaml";
    const auto sceneB = dir.Root / "b.yaml";
    Write(sceneA, "x: 1\n");
    Write(sceneB, "x: 1\n");
    const auto configHome = dir.Root / "config";
    const auto statePath = configHome / "molehole" / "molehole_state.yaml";
    Write(statePath, "LastScenePath: " + sceneA.string() + "\nRecentScenes:\n  - " + sceneA.string() + "\n  - " +
                         sceneB.string() + "\n  - " + (dir.Root / "gone.yaml").string() + "\nShowLauncher: false\n");

    fs::create_directories(dir.Root / "lib" / "molehole");
    fs::create_directories(dir.Root / "share" / "molehole");
    AppInfo info;
    info.Id = "molehole";
    info.Name = "MoleHole";
    auto paths = std::make_shared<AppPaths>(AppPaths::Resolve({
        .BinaryDir = dir.Root / "lib" / "molehole",
        .WorkingDir = dir.Root,
        .Info = info,
        .Env = [&](std::string_view key) -> std::optional<std::string>
        {
            if (key == "HOME") return dir.Root.string();
            if (key == "XDG_CONFIG_HOME") return configHome.string();
            return std::nullopt;
        }}));

    REQUIRE(paths->IsInstalled());
    const auto fileSystem = std::make_shared<FileSystem>();
    const auto logger = std::make_shared<Logger>();
    const auto uiState = std::make_shared<UiState>();
    const auto makeService = [&]
    {
        return std::make_shared<AppStateService>(
            uiState, std::make_shared<RenderSettingsService>(),
            std::make_shared<UiPreferences>(std::make_shared<FontOptions>(), std::make_shared<EventDispatcher>()),
            fileSystem, paths, logger);
    };

    {
        const auto service = makeService();
        service->StartAsync({}).get();
        const auto recents = service->GetRecents();
        REQUIRE(recents.size() == 2);
        CHECK(recents[0].Path == sceneA.string());
        CHECK(recents[1].Path == sceneB.string());
        CHECK(!service->GetShowLauncher());
        service->NotifySceneOpened(sceneB.string());
        service->SetShowLauncher(true);
        service->StopAsync().get();
    }

    std::ifstream saved(statePath);
    const std::string text(std::istreambuf_iterator<char>(saved), {});
    CHECK(text.contains("Path: " + sceneB.string()));
    CHECK(text.contains("LastOpened:"));

    const auto prefs = ReadStartupPrefs(statePath);
    CHECK(prefs.ShowLauncher);

    const auto reloaded = makeService();
    reloaded->StartAsync({}).get();
    const auto recents = reloaded->GetRecents();
    REQUIRE(recents.size() == 2);
    CHECK(recents[0].Path == sceneB.string());
    CHECK(recents[0].LastOpened > 0);
    CHECK(reloaded->GetShowLauncher());
}
