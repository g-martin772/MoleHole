#include <catch2/catch_test_macros.hpp>

import MoleHole;
import std;

using namespace MoleHole;

TEST_CASE("Layout names are sanitised and built-in names reserved", "[ui][layout]")
{
    CHECK(SanitizeLayoutName("  My Layout  ") == "My Layout");
    CHECK(SanitizeLayoutName("a/b\\c:d") == "a_b_c_d");
    CHECK(SanitizeLayoutName("..") == "__");
    CHECK_FALSE(SanitizeLayoutName("   "));
    CHECK_FALSE(SanitizeLayoutName(""));
    CHECK(SanitizeLayoutName(std::string(200, 'x'))->size() == kMaxLayoutNameLength);
    CHECK(IsReservedLayoutName("Edit"));
    CHECK(IsReservedLayoutName("render"));
    CHECK(IsReservedLayoutName("GRAPH"));
    CHECK_FALSE(IsReservedLayoutName("Mine"));
}

TEST_CASE("Layout files carry the visible window set in a trailing section", "[ui][layout]")
{
    const std::string ini = "[Window][Scene]\nPos=0,0\n\n[Docking][Data]\nDockSpace ID=0x1\n\n";
    const std::vector<std::string> visible{"Scene", "Animation Graph"};
    const auto encoded = EncodeLayout(ini, visible);
    CHECK(encoded.starts_with(ini));
    CHECK(DecodeLayoutVisible(encoded) == visible);
    CHECK_FALSE(DecodeLayoutVisible(ini));

    const std::vector<std::string> other{"Log"};
    const auto reencoded = EncodeLayout(encoded, other);
    CHECK(DecodeLayoutVisible(reencoded) == other);
    CHECK(reencoded.starts_with(ini));
    CHECK(reencoded.find(kLayoutSection) == reencoded.rfind(kLayoutSection));
    CHECK(StripLayoutSection(reencoded) == ini);

    CHECK(DecodeLayoutVisible(EncodeLayout(ini, {}))->empty());
}

TEST_CASE("Applying a layout toggles exactly the listed windows", "[ui][layout]")
{
    UiState ui;
    ui.ShowSceneWindow = true;
    ui.ShowLogWindow = true;
    const std::vector<std::string> titles{"Camera", "Animation Graph"};
    ApplyVisibleWindows(ui, titles);
    CHECK(ui.ShowCameraWindow);
    CHECK(ui.ShowAnimationGraphWindow);
    CHECK_FALSE(ui.ShowSceneWindow);
    CHECK_FALSE(ui.ShowLogWindow);
    CHECK(CollectVisibleWindows(ui) == std::vector<std::string>{"Camera", "Animation Graph"});

    const auto* graph = FindBuiltInLayout("Graph");
    REQUIRE(graph);
    ApplyVisibleWindows(ui, std::span<const std::string_view>(graph->Windows));
    CHECK(ui.ShowAnimationGraphWindow);
    CHECK(ui.ShowSceneWindow);
    CHECK_FALSE(ui.ShowCameraWindow);
}

TEST_CASE("Built-in layouts only name known windows", "[ui][layout]")
{
    for (const auto& layout : BuiltInLayouts())
    {
        for (const auto title : layout.Windows)
        {
            CHECK(std::ranges::contains(kLayoutWindows, title, &LayoutWindow::Title));
        }
    }
    CHECK(FindBuiltInLayout("Nope") == nullptr);
}

TEST_CASE("User layouts are listed from the directory without built-in names", "[ui][layout]")
{
    const auto dir = std::filesystem::temp_directory_path() / "molehole_layout_test";
    std::filesystem::remove_all(dir);
    CHECK(ListUserLayouts(dir).empty());
    std::filesystem::create_directories(dir);
    for (const char* name : {"zeta.ini", "alpha.ini", "Edit.ini", "notes.txt"}) std::ofstream(dir / name) << "x";
    CHECK(ListUserLayouts(dir) == std::vector<std::string>{"alpha", "zeta"});
    const auto choices = LayoutChoices(dir);
    CHECK(choices == std::vector<std::string>{"Edit", "Render", "Graph", "alpha", "zeta"});
    CHECK(LayoutPath(dir, "alpha") == dir / "alpha.ini");
    std::filesystem::remove_all(dir);
}

TEST_CASE("Layout actions are registered and queue requests", "[ui][layout][actions]")
{
    UiState ui;
    ActionSources sources;
    sources.LayoutNames = {"Edit", "Mine"};
    const auto actions = BuildActions({&ui, nullptr, nullptr}, sources);

    REQUIRE(FindAction(actions, "view.graph"));
    FindAction(actions, "layout.reset")->Invoke();
    FindAction(actions, "layout.apply:Mine")->Invoke();
    const auto taken = ui.Actions.Take({"layout."});
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].Id == "layout.reset");
    CHECK(taken[1].Id == "layout.apply");
    CHECK(taken[1].Arg == "Mine");

    CHECK_FALSE(ui.LayoutSaveRequested);
    FindAction(actions, "layout.save")->Invoke();
    CHECK(ui.LayoutSaveRequested);
}
