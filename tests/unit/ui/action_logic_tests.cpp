#include <catch2/catch_test_macros.hpp>

import MoleHole;
import std;

using namespace MoleHole;

namespace
{
    ActionDescriptor Make(const std::string& id, const std::string& label, const std::string& category = "Test",
                          const std::string& shortcut = {}, const std::string& scope = {}, const bool invokable = true)
    {
        ActionDescriptor action;
        action.Id = id;
        action.Label = label;
        action.Category = category;
        action.Shortcut = shortcut;
        action.Scope = scope;
        if (invokable) action.Invoke = [] {};
        return action;
    }

    std::vector<std::string> Ids(const std::vector<ActionDescriptor>& actions, const std::vector<RankedAction>& ranked)
    {
        std::vector<std::string> ids;
        for (const auto& item : ranked) ids.push_back(actions[item.Index].Id);
        return ids;
    }
}

TEST_CASE("NormalizeShortcut canonicalises modifier order and key names", "[ui][actions]")
{
    CHECK(NormalizeShortcut("ctrl+shift+z") == "Ctrl+Shift+Z");
    CHECK(NormalizeShortcut("Shift+Ctrl+S") == "Ctrl+Shift+S");
    CHECK(NormalizeShortcut(" control + alt + del ") == "Ctrl+Alt+Del");
    CHECK(NormalizeShortcut("escape") == "Esc");
    CHECK(NormalizeShortcut("f2") == "F2");
    CHECK(NormalizeShortcut("Num +") == "Num +");
    CHECK(NormalizeShortcut("Ctrl++") == "Ctrl++");
    CHECK(NormalizeShortcut("").empty());
    CHECK(NormalizeShortcut("Ctrl+").empty());
}

TEST_CASE("Shortcut lists split on slash and format uniformly", "[ui][actions]")
{
    CHECK(SplitShortcuts("t / 1") == std::vector<std::string>{"T", "1"});
    CHECK(FormatShortcuts("ctrl+shift+z/ctrl+y") == "Ctrl+Shift+Z / Ctrl+Y");
    CHECK(PrimaryShortcut("Ctrl+Shift+Z / Ctrl+Y") == "Ctrl+Shift+Z");
    CHECK(PrimaryShortcut("").empty());
    CHECK(SplitShortcuts("").empty());
}

TEST_CASE("DisabledReason reports the first unmet requirement", "[ui][actions]")
{
    ActionContext context;
    CHECK(DisabledReason(ActionNeed::None, context).empty());
    CHECK(DisabledReason(ActionNeed::Scene | ActionNeed::Selection, context) == "No scene loaded");
    context.HasScene = true;
    CHECK(DisabledReason(ActionNeed::Scene | ActionNeed::Selection, context) == "No entity selected");
    context.HasSelection = true;
    CHECK(DisabledReason(ActionNeed::Scene | ActionNeed::Selection, context).empty());
    context.Paused = false;
    CHECK(DisabledReason(ActionNeed::Scene | ActionNeed::Paused | ActionNeed::CanUndo, context) == "Pause the simulation first");
    context.Paused = true;
    CHECK(DisabledReason(ActionNeed::Scene | ActionNeed::Paused | ActionNeed::CanUndo, context) == "Nothing to undo");
    CHECK(DisabledReason(ActionNeed::CanRedo, context) == "Nothing to redo");
}

TEST_CASE("FindShortcutConflicts flags duplicates within a scope only", "[ui][actions]")
{
    const std::vector<ActionDescriptor> actions{
        Make("a", "A", "T", "Ctrl+Z", "Graph"), Make("b", "B", "T", "ctrl+z / Del", "Graph"),
        Make("c", "C", "T", "Ctrl+Z", ""), Make("d", "D", "T", "Del", "")};
    const auto conflicts = FindShortcutConflicts(actions);
    REQUIRE(conflicts.size() == 1);
    CHECK(conflicts[0].First == "a");
    CHECK(conflicts[0].Second == "b");
    CHECK(conflicts[0].Chord == "Ctrl+Z");
}

TEST_CASE("RankActions filters, skips reference-only entries and sorts disabled last", "[ui][actions]")
{
    auto disabled = Make("save", "Save Scene");
    disabled.Needs = ActionNeed::Scene;
    const std::vector<ActionDescriptor> actions{Make("sa", "Select All"), disabled, Make("hint", "Save Hint", "Test", "", "", false),
                                                Make("zoom", "Zoom")};
    const auto none = RankActions("sav", actions, {}, {});
    REQUIRE(none.size() == 1);
    CHECK(actions[none[0].Index].Id == "save");
    CHECK_FALSE(none[0].Enabled());
    CHECK(none[0].Reason == "No scene loaded");

    CHECK(Ids(actions, RankActions("sa", actions, {}, {})).front() == "sa");
    ActionContext context;
    context.HasScene = true;
    CHECK(Ids(actions, RankActions("sa", actions, context, {})).front() == "save");
    CHECK(RankActions("qqq", actions, context, {}).empty());

    const auto all = RankActions("", actions, {}, {});
    CHECK(Ids(actions, all) == std::vector<std::string>{"sa", "zoom", "save"});
}

TEST_CASE("RankActions matches category and keywords as a fallback", "[ui][actions]")
{
    auto window = Make("view.log", "Log", "View");
    window.Keywords = "console output";
    const std::vector<ActionDescriptor> actions{window, Make("other", "Other", "Edit")};
    CHECK(Ids(actions, RankActions("view log", actions, {}, {})) == std::vector<std::string>{"view.log"});
    CHECK(Ids(actions, RankActions("console", actions, {}, {})) == std::vector<std::string>{"view.log"});
    CHECK(RankActions("cnsl", actions, {}, {}).empty());
}

TEST_CASE("RankActions puts recent actions first on an empty query and nudges them on a query", "[ui][actions]")
{
    const std::vector<ActionDescriptor> actions{Make("a", "Alpha"), Make("b", "Beta"), Make("c", "Gamma")};
    const std::vector<std::string> recent{"c", "b"};
    CHECK(Ids(actions, RankActions("", actions, {}, recent)) == std::vector<std::string>{"c", "b", "a"});

    const std::vector<ActionDescriptor> twins{Make("x", "Move Camera"), Make("y", "Move Cube")};
    const std::vector<std::string> usedY{"y"};
    CHECK(Ids(twins, RankActions("move c", twins, {}, usedY)).front() == "y");
    CHECK(Ids(twins, RankActions("move c", twins, {}, {})).front() == "x");
}

TEST_CASE("RankActions prefers a prefix match over a scattered one despite a small recency bonus", "[ui][actions]")
{
    const std::vector<ActionDescriptor> actions{Make("a", "Set Item Number"), Make("b", "Sin")};
    const std::vector<std::string> recent{"a"};
    CHECK(Ids(actions, RankActions("sin", actions, {}, recent)).front() == "b");
}

TEST_CASE("RecentActions keeps most recent first, deduplicated and capped", "[ui][actions]")
{
    RecentActions recent;
    recent.Touch("a");
    recent.Touch("b");
    recent.Touch("a");
    CHECK(recent.Ids() == std::vector<std::string>{"a", "b"});
    for (int i = 0; i < 20; ++i) recent.Touch(std::to_string(i));
    CHECK(recent.Ids().size() == RecentActions::kCapacity);
    CHECK(recent.Ids().front() == "19");
}

TEST_CASE("ActionRequests hand each consumer only its own prefixes", "[ui][actions]")
{
    ActionRequests requests;
    requests.Push({"file.save", {}});
    requests.Push({"edit.undo", {}});
    requests.Push({"file.template", "x.yaml"});
    const auto files = requests.Take({"file."});
    REQUIRE(files.size() == 2);
    CHECK(files[1].Arg == "x.yaml");
    CHECK(requests.Take({"file."}).empty());
    CHECK(requests.Take({"edit.undo", "edit.redo"}).size() == 1);
}

TEST_CASE("BuildShortcutSheet groups rows by category and filters", "[ui][actions]")
{
    const std::vector<ActionDescriptor> actions{Make("a", "Save", "File", "ctrl+s"), Make("b", "Open", "File", "ctrl+o"),
                                                Make("c", "Undo", "Edit", "Ctrl+Z / ctrl+y", "Graph"),
                                                Make("d", "Play", "Sim")};
    const auto sheet = BuildShortcutSheet(actions, "");
    REQUIRE(sheet.size() == 2);
    CHECK(sheet[0].Category == "File");
    CHECK(sheet[0].Rows.size() == 2);
    CHECK(sheet[0].Rows[0].Shortcut == "Ctrl+S");
    CHECK(sheet[1].Rows[0].Shortcut == "Ctrl+Z / Ctrl+Y");
    CHECK(sheet[1].Rows[0].Scope == "Graph");

    const auto filtered = BuildShortcutSheet(actions, "undo");
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].Category == "Edit");
    CHECK(BuildShortcutSheet(actions, "zzzz").empty());
}

TEST_CASE("The built-in action registry has unique ids and no shortcut conflicts", "[ui][actions]")
{
    const auto& actions = StaticActions();
    REQUIRE_FALSE(actions.empty());
    std::set<std::string> ids;
    for (const auto& action : actions)
    {
        CHECK(ids.insert(action.Id).second);
        CHECK_FALSE(action.Label.empty());
        CHECK_FALSE(action.Category.empty());
    }
    for (const auto& conflict : FindShortcutConflicts(actions))
        FAIL(conflict.First << " and " << conflict.Second << " share " << conflict.Chord);
}

TEST_CASE("The built-in registry exposes the menu accelerators", "[ui][actions]")
{
    CHECK(ActionShortcut("file.save") == "Ctrl+S");
    CHECK(ActionShortcut("edit.redo") == "Ctrl+Shift+Z");
    CHECK(ActionShortcut("help.shortcuts") == "F1");
    CHECK(ActionShortcut("view.camera").empty());
    CHECK(ActionShortcut("missing").empty());

    ActionContext context;
    CHECK(ActionDisabledReason("edit.delete", context) == "No scene loaded");
    context.HasScene = true;
    CHECK(ActionDisabledReason("edit.delete", context) == "No entity selected");
    context.HasSelection = true;
    CHECK(ActionDisabledReason("edit.delete", context).empty());
}

TEST_CASE("BuildActions adds dynamic templates, profiles and create entries and invokes against UiState", "[ui][actions]")
{
    UiState ui;
    ActionSources sources;
    sources.Templates.push_back({"demo", "templates/demo.yaml"});
    sources.ProfileNames = {"Flat", "Traced"};
    const auto actions = BuildActions({&ui, nullptr, nullptr}, sources);

    const auto* tmpl = FindAction(actions, "file.template:demo");
    REQUIRE(tmpl);
    tmpl->Invoke();
    const auto taken = ui.Actions.Take({"file.template"});
    REQUIRE(taken.size() == 1);
    CHECK(taken[0].Arg == "templates/demo.yaml");

    CHECK(FindAction(actions, "render.profile:Traced"));

    const auto* camera = FindAction(actions, "view.camera");
    REQUIRE(camera);
    REQUIRE(camera->Toggle == &ui.ShowCameraWindow);
    const bool before = ui.ShowCameraWindow;
    camera->Invoke();
    CHECK(ui.ShowCameraWindow != before);

    FindAction(actions, "gizmo.scale")->Invoke();
    CHECK(ui.ActiveGizmoOperation == GizmoOperation::Scale);

    FindAction(actions, "edit.delete")->Invoke();
    CHECK(ui.Edits.Take({EditAction::Delete}).size() == 1);

    FindAction(actions, "edit.copy")->Invoke();
    FindAction(actions, "edit.paste")->Invoke();
    CHECK(ui.Edits.Take({EditAction::Copy, EditAction::Paste}).size() == 2);

    ActionContext context;
    context.HasScene = true;
    CHECK(ActionDisabledReason("edit.copy", context) == "No entity selected");
    CHECK(ActionDisabledReason("edit.paste", context).empty());

    FindAction(actions, "help.shortcuts")->Invoke();
    CHECK(ui.ShowShortcutSheet);
}
