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
    std::ofstream(statePath) << "UI:\n  UndoHistoryCapacity: 99999999\nAutosave:\n  Enabled: true\n  DelaySeconds: 30\n"
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

        const auto& overrides = ShortcutOverrideStore::Instance().Overrides();
        CHECK(overrides.at("file.open") == "Ctrl+K");
        CHECK_FALSE(overrides.contains("file.save"));
        CHECK_FALSE(overrides.contains("bogus.action"));
        CHECK_FALSE(overrides.contains("edit.delete"));
        CHECK(overrides.at("file.new").empty());

        uiState->UndoHistoryCapacity = 321;
        ShortcutOverrideStore::Instance().Set({{"help.shortcuts", "F5"}});
        service->StopAsync().get();
    }

    ShortcutOverrideStore::Instance().Set({});
    uiState->UndoHistoryCapacity = kDefaultUndoCapacity;
    {
        const auto service = makeService();
        service->StartAsync({}).get();
        CHECK(uiState->UndoHistoryCapacity == 321);
        CHECK(ShortcutOverrideStore::Instance().Overrides() == ShortcutOverrides{{"help.shortcuts", "F5"}});
        CHECK(ActionShortcut("help.shortcuts") == "F5");
        service->StopAsync().get();
    }

    ShortcutOverrideStore::Instance().Set({});
    std::error_code error;
    fs::remove_all(root, error);
}
