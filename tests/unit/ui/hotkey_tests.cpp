#include <catch2/catch_test_macros.hpp>

import MoleHole;
import imgui;
import std;

using namespace MoleHole;

namespace
{
    struct StoreGuard
    {
        StoreGuard() { ShortcutOverrideStore::Instance().Set({}); }
        ~StoreGuard() { ShortcutOverrideStore::Instance().Set({}); }
    };

    const std::vector<ActionDescriptor>& Defaults() { return DefaultActions(); }

    std::string ShortcutOf(const std::vector<ActionDescriptor>& actions, const std::string_view id)
    {
        const auto* action = FindAction(actions, id);
        return action ? action->Shortcut : std::string("<missing>");
    }
}

TEST_CASE("ChordProblem rejects unusable chords", "[ui][hotkeys]")
{
    CHECK(ChordProblem("Ctrl+K").empty());
    CHECK(ChordProblem("F5").empty());
    CHECK(ChordProblem("Shift+.").empty());
    CHECK(ChordProblem("Ctrl+Space").empty());
    CHECK_FALSE(ChordProblem("").empty());
    CHECK_FALSE(ChordProblem("Ctrl+").empty());
    CHECK_FALSE(ChordProblem("Ctrl+A / B").empty());
    CHECK_FALSE(ChordProblem("Ctrl+Pause").empty());
    CHECK_FALSE(ChordProblem("W").empty());
    CHECK_FALSE(ChordProblem("Shift+Q").empty());
    CHECK_FALSE(ChordProblem("Enter").empty());
    CHECK_FALSE(ChordProblem("Up").empty());
    CHECK(ChordProblem("Ctrl+W").empty());
}

TEST_CASE("ParseChord splits modifiers from the key", "[ui][hotkeys]")
{
    const auto chord = ParseChord("shift+ctrl+pageup");
    CHECK(chord.Ctrl);
    CHECK(chord.Shift);
    CHECK_FALSE(chord.Alt);
    CHECK(chord.Key == "PageUp");
    CHECK(ParseChord("Num +").Key == "Num +");
    CHECK(ParseChord("Ctrl+Num -").Key == "Num -");
}

TEST_CASE("The key table and the bindable key names agree", "[ui][hotkeys]")
{
    const auto& table = KeyTable();
    CHECK(table.size() == kBindableKeys.size());
    for (const auto name : kBindableKeys) CHECK(FindKey(name));
    for (const auto& entry : table) CHECK(std::ranges::contains(kBindableKeys, entry.Name));
}

TEST_CASE("Every default global shortcut is valid and has an ImGui chord", "[ui][hotkeys]")
{
    int checked = 0;
    for (const auto& action : Defaults())
    {
        if (!IsRebindable(action)) continue;
        for (const auto& chord : SplitShortcuts(action.Shortcut))
        {
            INFO(action.Id << " " << chord);
            CHECK(ChordProblem(chord).empty());
            CHECK(ToImGuiChord(chord));
            ++checked;
        }
    }
    CHECK(checked >= 12);
}

TEST_CASE("Only global registry actions are rebindable", "[ui][hotkeys]")
{
    CHECK(IsRebindable(*FindAction(Defaults(), "file.save")));
    CHECK(IsRebindable(*FindAction(Defaults(), "view.camera")));
    CHECK(IsRebindable(*FindAction(Defaults(), "sim.step")));
    CHECK_FALSE(IsRebindable(*FindAction(Defaults(), "edit.delete")));
    CHECK_FALSE(IsRebindable(*FindAction(Defaults(), "gizmo.translate")));
    CHECK_FALSE(IsRebindable(*FindAction(Defaults(), "graph.copy")));
}

TEST_CASE("TrySetShortcut applies free chords and reports holders", "[ui][hotkeys]")
{
    ShortcutOverrides overrides;
    auto outcome = TrySetShortcut(overrides, Defaults(), "file.save", "ctrl+k");
    CHECK(outcome.Applied);
    CHECK(overrides.at("file.save") == "Ctrl+K");

    outcome = TrySetShortcut(overrides, Defaults(), "file.open", "Ctrl+K");
    CHECK_FALSE(outcome.Applied);
    REQUIRE(outcome.Holders.size() == 1);
    CHECK(outcome.Holders[0].find("Save Scene") != std::string::npos);

    outcome = TrySetShortcut(overrides, Defaults(), "file.open", "Ctrl+S");
    CHECK(outcome.Applied);
    CHECK(overrides.contains("file.open"));

    outcome = TrySetShortcut(overrides, Defaults(), "file.new", "Ctrl+S");
    CHECK_FALSE(outcome.Applied);
    CHECK(outcome.Holders.size() == 1);
}

TEST_CASE("TrySetShortcut collides with scoped window shortcuts but not with the graph editor", "[ui][hotkeys]")
{
    ShortcutOverrides overrides;
    CHECK_FALSE(TrySetShortcut(overrides, Defaults(), "file.close", "Ctrl+D").Applied);
    CHECK_FALSE(TrySetShortcut(overrides, Defaults(), "file.close", "F2").Applied);
    CHECK_FALSE(TrySetShortcut(overrides, Defaults(), "file.close", "T").Applied);
    CHECK(TrySetShortcut(overrides, Defaults(), "file.close", "Ctrl+C").Applied);
    CHECK_FALSE(TrySetShortcut(overrides, Defaults(), "file.close", "Ctrl+").Problem.empty());
    CHECK(TrySetShortcut(overrides, Defaults(), "file.close", "W").Problem.find("Reserved") != std::string::npos);
    CHECK_FALSE(TrySetShortcut(overrides, Defaults(), "edit.delete", "Ctrl+K").Applied);
    CHECK_FALSE(TrySetShortcut(overrides, Defaults(), "missing", "Ctrl+K").Applied);
}

TEST_CASE("Rebinding back to the default removes the override", "[ui][hotkeys]")
{
    ShortcutOverrides overrides;
    REQUIRE(TrySetShortcut(overrides, Defaults(), "file.save", "Ctrl+K").Applied);
    REQUIRE(TrySetShortcut(overrides, Defaults(), "file.save", "Ctrl+S").Applied);
    CHECK(overrides.empty());

    REQUIRE(TrySetShortcut(overrides, Defaults(), "help.shortcuts", "F5").Applied);
    ResetShortcut(overrides, "help.shortcuts");
    CHECK(overrides.empty());
}

TEST_CASE("Unbinding frees the chord for another action", "[ui][hotkeys]")
{
    ShortcutOverrides overrides;
    ClearShortcut(overrides, Defaults(), "file.new");
    CHECK(overrides.at("file.new").empty());
    CHECK(TrySetShortcut(overrides, Defaults(), "file.open", "Ctrl+N").Applied);

    ClearShortcut(overrides, Defaults(), "view.camera");
    CHECK_FALSE(overrides.contains("view.camera"));

    auto effective = Defaults();
    ApplyShortcutOverrides(effective, overrides);
    CHECK(ShortcutOf(effective, "file.new").empty());
    CHECK(ShortcutOf(effective, "file.open") == "Ctrl+N");
    CHECK(ShortcutOf(effective, "file.save") == "Ctrl+S");
}

TEST_CASE("Default rebinding does not override a multi-chord default accidentally", "[ui][hotkeys]")
{
    ShortcutOverrides overrides;
    REQUIRE(TrySetShortcut(overrides, Defaults(), "edit.redo", "Ctrl+Y").Applied);
    CHECK(overrides.at("edit.redo") == "Ctrl+Y");
    auto effective = Defaults();
    ApplyShortcutOverrides(effective, overrides);
    CHECK(SplitShortcuts(ShortcutOf(effective, "edit.redo")).size() == 1);
}

TEST_CASE("SanitizeShortcutOverrides drops stale, fixed, invalid and clashing entries", "[ui][hotkeys]")
{
    const ShortcutOverrides raw{
        {"file.save", "Ctrl+K"},
        {"file.open", "Ctrl+K"},
        {"nonexistent", "Ctrl+J"},
        {"edit.delete", "Ctrl+J"},
        {"file.new", ""},
        {"help.shortcuts", "Ctrl+Pause"},
        {"view.camera", ""},
    };
    const auto clean = SanitizeShortcutOverrides(raw, Defaults());
    CHECK(clean.at("file.open") == "Ctrl+K");
    CHECK_FALSE(clean.contains("file.save"));
    CHECK_FALSE(clean.contains("nonexistent"));
    CHECK_FALSE(clean.contains("edit.delete"));
    CHECK(clean.at("file.new").empty());
    CHECK_FALSE(clean.contains("help.shortcuts"));
    CHECK_FALSE(clean.contains("view.camera"));
}

TEST_CASE("The override store drives the built actions and shortcut lookups", "[ui][hotkeys]")
{
    StoreGuard guard;
    auto& store = ShortcutOverrideStore::Instance();
    CHECK(ActionShortcut("file.save") == "Ctrl+S");
    const auto before = store.Revision();

    store.Set({{"file.save", "Ctrl+K"}, {"file.new", ""}});
    CHECK(store.Revision() == before + 1);
    store.Set({{"file.save", "Ctrl+K"}, {"file.new", ""}});
    CHECK(store.Revision() == before + 1);

    CHECK(ActionShortcut("file.save") == "Ctrl+K");
    CHECK(ActionShortcut("file.new").empty());
    CHECK(ShortcutOf(BuildActions({}), "file.save") == "Ctrl+K");
    CHECK(ShortcutOf(Defaults(), "file.save") == "Ctrl+S");
    CHECK(ShortcutOf(BuildActions({}), "edit.delete") == "Del");
}

TEST_CASE("HotkeyAllowed gates by focus and context", "[ui][hotkeys]")
{
    HotkeyBinding plain{"a", 0, false, false, false};
    HotkeyBinding command{"b", 0, false, true, false};
    HotkeyBinding file{"c", 0, false, true, true};

    HotkeyFrame idle;
    CHECK(HotkeyAllowed(plain, idle));

    HotkeyFrame typing;
    typing.TextInput = true;
    CHECK_FALSE(HotkeyAllowed(command, typing));

    HotkeyFrame popup;
    popup.PopupOpen = true;
    CHECK_FALSE(HotkeyAllowed(file, popup));

    HotkeyFrame active;
    active.AnyItemActive = true;
    CHECK_FALSE(HotkeyAllowed(plain, active));
    CHECK(HotkeyAllowed(command, active));

    HotkeyFrame graph;
    graph.GraphEditorFocused = true;
    CHECK_FALSE(HotkeyAllowed(command, graph));
    CHECK(HotkeyAllowed(file, graph));
}

TEST_CASE("CollectHotkeys expands every chord of rebindable actions and honours overrides", "[ui][hotkeys]")
{
    const auto bindings = CollectHotkeys(Defaults());
    const auto count = [&](const std::string_view id)
    {
        return std::ranges::count(bindings, id, &HotkeyBinding::Id);
    };
    CHECK(count("edit.redo") == 2);
    CHECK(count("file.save") == 1);
    CHECK(count("edit.delete") == 0);
    CHECK(count("view.camera") == 0);

    const auto undo = std::ranges::find(bindings, "edit.undo", &HotkeyBinding::Id);
    REQUIRE(undo != bindings.end());
    CHECK(undo->Repeat);
    CHECK(undo->Command);
    const auto step = std::ranges::find(bindings, "sim.step", &HotkeyBinding::Id);
    REQUIRE(step != bindings.end());
    CHECK_FALSE(step->Command);
    CHECK(step->Repeat);
    CHECK(std::ranges::find(bindings, "file.save", &HotkeyBinding::Id)->GraphSafe);

    auto effective = Defaults();
    ApplyShortcutOverrides(effective, {{"edit.undo", "Ctrl+K"}, {"file.new", ""}});
    const auto changed = CollectHotkeys(effective);
    CHECK(std::ranges::count(changed, "edit.undo", &HotkeyBinding::Id) == 1);
    CHECK(std::ranges::count(changed, "file.new", &HotkeyBinding::Id) == 0);
    CHECK(std::ranges::find(changed, "edit.undo", &HotkeyBinding::Id)->Chord == *ToImGuiChord("Ctrl+K"));
}

TEST_CASE("ToImGuiChord combines modifiers and key", "[ui][hotkeys]")
{
    CHECK(ToImGuiChord("Ctrl+S") == (ImGuiMod_Ctrl | ImGuiKey_S));
    CHECK(ToImGuiChord("Ctrl+Shift+Z") == (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z));
    CHECK(ToImGuiChord("Alt+F4") == (ImGuiMod_Alt | ImGuiKey_F4));
    CHECK(ToImGuiChord(".") == static_cast<ImGuiKeyChord>(ImGuiKey_Period));
    CHECK_FALSE(ToImGuiChord("Pause"));
    CHECK_FALSE(ToImGuiChord("W"));
    CHECK(ChordText(true, false, true, "K") == "Ctrl+Shift+K");
}

TEST_CASE("Undo capacity is clamped to the supported range", "[ui][settings]")
{
    CHECK(ClampUndoCapacity(0) == kMinUndoCapacity);
    CHECK(ClampUndoCapacity(-5) == kMinUndoCapacity);
    CHECK(ClampUndoCapacity(500) == 500);
    CHECK(ClampUndoCapacity(10'000'000) == kMaxUndoCapacity);
    CHECK(UiState().UndoHistoryCapacity == kDefaultUndoCapacity);
    CHECK(FormatAutosaveDelay(5) == "5 s");
    CHECK(std::ranges::contains(kAutosaveDelaysSeconds, kDefaultAutosaveDelaySeconds));
}

TEST_CASE("Default render profiles per context can be set and reset", "[ui][settings][rendering]")
{
    RenderSettingsService service;
    const RenderProfileSet factory;
    CHECK(service.Defaults().ProfileName(RenderContext::Edit) == factory.ProfileName(RenderContext::Edit));

    service.SetDefaultProfile(RenderContext::Edit, "Flat");
    service.SetDefaultProfile(RenderContext::Export, "Reference");
    CHECK(service.Defaults().ProfileName(RenderContext::Edit) == "Flat");
    CHECK(service.Defaults().ProfileName(RenderContext::Export) == "Reference");
    CHECK(service.Profiles().ProfileName(RenderContext::Edit) == "Flat");

    service.SetDefaultProfile(RenderContext::Play, "No such profile");
    CHECK(service.Defaults().ProfileName(RenderContext::Play) == factory.ProfileName(RenderContext::Play));

    service.ResetDefaultProfiles();
    for (int i = 0; i < kRenderContextCount; ++i)
    {
        const auto context = static_cast<RenderContext>(i);
        CHECK(service.Defaults().ProfileName(context) == factory.ProfileName(context));
        CHECK(service.Profiles().ProfileName(context) == factory.ProfileName(context));
    }
}

TEST_CASE("Default profile changes leave a scene's own render settings alone", "[ui][settings][rendering]")
{
    RenderSettingsService service;
    service.LoadScene({});
    service.SetDefaultProfile(RenderContext::Edit, "Flat");
    CHECK(service.Profiles().ProfileName(RenderContext::Edit) == "Flat");

    service.Assign(RenderContext::Edit, "Lensed");
    REQUIRE(service.PersistsToScene());
    service.SetDefaultProfile(RenderContext::Edit, "Traced");
    CHECK(service.Defaults().ProfileName(RenderContext::Edit) == "Traced");
    CHECK(service.Profiles().ProfileName(RenderContext::Edit) == "Lensed");
}
