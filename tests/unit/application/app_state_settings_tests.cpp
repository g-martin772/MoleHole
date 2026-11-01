#include <catch2/catch_test_macros.hpp>

import MoleHole;
import GPP;
import std;

using namespace MoleHole;
using namespace GPP;
namespace fs = std::filesystem;

TEST_CASE("Settings persist in the state file and are sanitised on load", "[state][settings]")
{
    const auto root = fs::temp_directory_path() /
        ("molehole_settings_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "lib" / "molehole");
    fs::create_directories(root / "share" / "molehole");
    const auto configHome = root / "config";
    const auto statePath = configHome / "molehole" / "molehole_state.yaml";
    fs::create_directories(statePath.parent_path());
    std::ofstream(statePath) << "UI:\n  UndoHistoryCapacity: 99999999\nSimulation:\n  FixedTimestep: false\nAutosave:\n  Enabled: true\n  DelaySeconds: 30\n"
                                "Shortcuts:\n  file.save: Ctrl+K\n  file.open: Ctrl+K\n  bogus.action: Ctrl+J\n  edit.delete: Ctrl+L\n  file.new: ''\n";

    AppInfo info;
    info.Id = "molehole";
    info.Name = "MoleHole";
    auto paths = std::make_shared<AppPaths>(AppPaths::Resolve({
        .BinaryDir = root / "lib" / "molehole",
        .WorkingDir = root,
        .Info = info,
        .Env = [&](std::string_view key) -> std::optional<std::string>
        {
            if (key == "HOME") return root.string();
            if (key == "XDG_CONFIG_HOME") return configHome.string();
            return std::nullopt;
        }}));
    REQUIRE(paths->IsInstalled());

    ShortcutOverrideStore::Instance().Set({});
    const auto uiState = std::make_shared<UiState>();
    const auto makeService = [&]
    {
        return std::make_shared<AppStateService>(
            uiState, std::make_shared<RenderSettingsService>(),
            std::make_shared<UiPreferences>(std::make_shared<FontOptions>(), std::make_shared<EventDispatcher>()),
            std::make_shared<FileSystem>(), paths, std::make_shared<Logger>());
    };

    {
        const auto service = makeService();
        service->StartAsync({}).get();
        CHECK(uiState->UndoHistoryCapacity == kMaxUndoCapacity);
        CHECK(uiState->AutosaveEnabled);
        CHECK(uiState->AutosaveDelaySeconds == 30);
        CHECK_FALSE(uiState->SimulationFixedTimestep);

        const auto& overrides = ShortcutOverrideStore::Instance().Overrides();
        CHECK(overrides.at("file.open") == "Ctrl+K");
        CHECK_FALSE(overrides.contains("file.save"));
        CHECK_FALSE(overrides.contains("bogus.action"));
        CHECK_FALSE(overrides.contains("edit.delete"));
        CHECK(overrides.at("file.new").empty());

        uiState->UndoHistoryCapacity = 321;
        uiState->RecentActionIds.Touch("file.save");
        uiState->RecentActionIds.Touch("view.frame");
        ShortcutOverrideStore::Instance().Set({{"help.shortcuts", "F5"}});
        service->StopAsync().get();
    }

    ShortcutOverrideStore::Instance().Set({});
    uiState->UndoHistoryCapacity = kDefaultUndoCapacity;
    uiState->RecentActionIds.Restore({});
    uiState->SimulationFixedTimestep = true;
    {
        const auto service = makeService();
        service->StartAsync({}).get();
        CHECK(uiState->UndoHistoryCapacity == 321);
        CHECK(uiState->RecentActionIds.Ids() == std::vector<std::string>{"view.frame", "file.save"});
        CHECK_FALSE(uiState->SimulationFixedTimestep);
        CHECK(ShortcutOverrideStore::Instance().Overrides() == ShortcutOverrides{{"help.shortcuts", "F5"}});
        CHECK(ActionShortcut("help.shortcuts") == "F5");
        service->StopAsync().get();
    }

    ShortcutOverrideStore::Instance().Set({});
    std::error_code error;
    fs::remove_all(root, error);
}

TEST_CASE("Headless exports ignore the saved camera and the per-scene camera cache", "[state][export]")
{
    const auto root = fs::temp_directory_path() /
        ("molehole_export_cam_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "lib" / "molehole");
    fs::create_directories(root / "share" / "molehole");
    const auto configHome = root / "config";
    fs::create_directories(configHome / "molehole");
    const auto scenePath = (root / "scene.yaml").string();
    std::ofstream(scenePath) << "Scene: S\n";

    AppInfo info;
    info.Id = "molehole";
    info.Name = "MoleHole";
    auto paths = std::make_shared<AppPaths>(AppPaths::Resolve({
        .BinaryDir = root / "lib" / "molehole",
        .WorkingDir = root,
        .Info = info,
        .Env = [&](std::string_view key) -> std::optional<std::string>
        {
            if (key == "HOME") return root.string();
            if (key == "XDG_CONFIG_HOME") return configHome.string();
            return std::nullopt;
        }}));
    REQUIRE(paths->IsInstalled());

    const auto makeService = [&](const std::shared_ptr<UiState>& state)
    {
        return std::make_shared<AppStateService>(
            state, std::make_shared<RenderSettingsService>(),
            std::make_shared<UiPreferences>(std::make_shared<FontOptions>(), std::make_shared<EventDispatcher>()),
            std::make_shared<FileSystem>(), paths, std::make_shared<Logger>());
    };

    const glm::dvec3 saved{1.0e9, 2.0e9, 3.0e9};
    {
        const auto state = std::make_shared<UiState>();
        state->CameraPositionM = saved;
        const auto service = makeService(state);
        service->StartAsync({}).get();
        state->CameraPositionM = saved;
        service->StoreSceneCamera(scenePath, SceneCameraPose{.PositionM = saved, .Yaw = 12.0f, .Pitch = 3.0f, .SpeedMs = 5.0});
        service->StopAsync().get();
    }
    {
        const auto state = std::make_shared<UiState>();
        const auto service = makeService(state);
        service->StartAsync({}).get();
        CHECK(state->CameraPositionM == saved);
        CHECK(service->LookupSceneCamera(scenePath).has_value());
        service->StopAsync().get();
    }
    {
        const auto state = std::make_shared<UiState>();
        const auto fresh = state->CameraPositionM;
        state->ExitWhenExportDone = true;
        const auto service = makeService(state);
        service->StartAsync({}).get();
        CHECK(state->CameraPositionM == fresh);
        CHECK_FALSE(service->LookupSceneCamera(scenePath).has_value());
        service->StopAsync().get();
    }

    std::error_code error;
    fs::remove_all(root, error);
}

TEST_CASE("An unreadable state file is kept aside and the app starts with defaults", "[state][corrupt]")
{
    const auto root = fs::temp_directory_path() /
        ("molehole_corrupt_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "lib" / "molehole");
    fs::create_directories(root / "share" / "molehole");
    const auto configHome = root / "config";
    const auto statePath = configHome / "molehole" / "molehole_state.yaml";
    fs::create_directories(statePath.parent_path());
    const std::string garbage = "UI: [unclosed\n  Font: : :\n\t- {";
    std::ofstream(statePath) << garbage;
    std::ofstream(statePath.parent_path() / "scene_cameras.yaml") << "Scenes: {not: [a list\n";

    AppInfo info;
    info.Id = "molehole";
    info.Name = "MoleHole";
    auto paths = std::make_shared<AppPaths>(AppPaths::Resolve({
        .BinaryDir = root / "lib" / "molehole",
        .WorkingDir = root,
        .Info = info,
        .Env = [&](std::string_view key) -> std::optional<std::string>
        {
            if (key == "HOME") return root.string();
            if (key == "XDG_CONFIG_HOME") return configHome.string();
            return std::nullopt;
        }}));
    REQUIRE(paths->IsInstalled());

    const auto uiState = std::make_shared<UiState>();
    const auto defaultCapacity = uiState->UndoHistoryCapacity;
    const auto service = std::make_shared<AppStateService>(
        uiState, std::make_shared<RenderSettingsService>(),
        std::make_shared<UiPreferences>(std::make_shared<FontOptions>(), std::make_shared<EventDispatcher>()),
        std::make_shared<FileSystem>(), paths, std::make_shared<Logger>());
    service->StartAsync({}).get();

    CHECK(uiState->UndoHistoryCapacity == defaultCapacity);
    CHECK_FALSE(fs::exists(statePath));
    auto kept = statePath;
    kept += ".corrupt";
    REQUIRE(fs::exists(kept));
    std::ifstream in(kept);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    CHECK(text == garbage);
    CHECK(fs::exists(statePath.parent_path() / "scene_cameras.yaml.corrupt"));

    service->StopAsync().get();
    CHECK(fs::exists(statePath));
    CHECK(fs::exists(kept));

    std::error_code error;
    fs::remove_all(root, error);
}

TEST_CASE("Quarantine moves a file aside and reports failure for a missing one", "[state][corrupt]")
{
    const auto dir = fs::temp_directory_path() /
        ("molehole_quarantine_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const auto file = dir / "state.yaml";
    std::ofstream(file) << "x";
    const auto kept = QuarantineCorruptFile(file);
    CHECK(kept == dir / "state.yaml.corrupt");
    CHECK_FALSE(fs::exists(file));
    CHECK(fs::exists(kept));
    CHECK(QuarantineCorruptFile(file).empty());
    std::error_code error;
    fs::remove_all(dir, error);
}
