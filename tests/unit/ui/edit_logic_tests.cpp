#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;
using namespace std::chrono_literals;

TEST_CASE("Undo menu item shows the label of the next step and its shortcut", "[ui][edit]")
{
    const auto undo = MakeHistoryMenuItem(false, true, false, true, "Move Earth");
    CHECK(undo.Enabled);
    CHECK(undo.Text == "Undo Move Earth");
    CHECK(undo.Shortcut == "Ctrl+Z");
    CHECK(undo.Reason.empty());

    const auto redo = MakeHistoryMenuItem(true, true, false, true, "Set Mass");
    CHECK(redo.Text == "Redo Set Mass");
    CHECK(redo.Shortcut == "Ctrl+Shift+Z");

    CHECK(MakeHistoryMenuItem(false, true, false, true, "").Text == "Undo");
}

TEST_CASE("Disabled history items explain why", "[ui][edit]")
{
    CHECK(MakeHistoryMenuItem(false, true, false, false, "").Reason == "Nothing to undo");
    CHECK(MakeHistoryMenuItem(true, true, false, false, "").Reason == "Nothing to redo");
    CHECK(MakeHistoryMenuItem(false, false, false, false, "").Reason == "No scene loaded");

    const auto playing = MakeHistoryMenuItem(false, true, true, true, "Move Earth");
    CHECK_FALSE(playing.Enabled);
    CHECK(playing.Reason == "Undo is disabled while the simulation is playing");
    CHECK(playing.Text == "Undo Move Earth");
    CHECK(MakeHistoryMenuItem(true, true, true, true, "x").Reason == "Redo is disabled while the simulation is playing");
    CHECK(MakeHistoryMenuItem(false, true, true, false, "").Reason == "Undo is disabled while the simulation is playing");
}

TEST_CASE("Long labels are shortened without splitting UTF-8", "[ui][edit]")
{
    const std::string longLabel(100, 'a');
    const auto shortened = ShortenLabel(longLabel);
    CHECK(shortened.size() == kMaxMenuLabel);
    CHECK(shortened.ends_with("..."));
    CHECK(ShortenLabel("short") == "short");

    const std::string accented = std::string(kMaxMenuLabel - 4, 'a') + "\xC3\xA9\xC3\xA9\xC3\xA9";
    const auto cut = ShortenLabel(accented);
    CHECK(cut.ends_with("..."));
    CHECK(cut.size() <= kMaxMenuLabel);
    for (std::size_t i = 0; i + 3 < cut.size(); ++i) CHECK((static_cast<unsigned char>(cut[i]) & 0xC0) != 0x80);
}

TEST_CASE("A toast is requested only when playing blocks the attempt", "[ui][edit]")
{
    CHECK(PlayingToast(false, true, true) == "Pause the simulation to undo");
    CHECK(PlayingToast(true, true, true) == "Pause the simulation to redo");
    CHECK_FALSE(PlayingToast(false, true, false).has_value());
    CHECK_FALSE(PlayingToast(false, false, true).has_value());
}

TEST_CASE("Ages are shown relative to now", "[ui][edit]")
{
    CHECK(FormatAge(0s) == "just now");
    CHECK(FormatAge(5s) == "5 s ago");
    CHECK(FormatAge(120s) == "2 min ago");
    CHECK(FormatAge(3h) == "3 h ago");
    CHECK(FormatAge(50h) == "2 d ago");
}

TEST_CASE("History rows put the newest step first and mark the current state", "[ui][edit]")
{
    const auto now = std::chrono::steady_clock::now();
    GPP::HistoryView view;
    view.Version = 1;
    view.UndoCount = 3;
    view.RedoCount = 2;
    view.Undo = {{"c", now - 10s}, {"b", now - 20s}, {"a", now - 30s}};
    view.Redo = {{"d", now - 5s}, {"e", now - 2s}};

    const auto rows = BuildHistoryRows(view, now);
    REQUIRE(rows.size() == 6);
    CHECK(rows[0].Label == "e");
    CHECK(rows[0].Future);
    CHECK(rows[0].Steps == 2);
    CHECK(rows[1].Label == "d");
    CHECK(rows[1].Steps == 1);
    CHECK(rows[2].Label == "c");
    CHECK(rows[2].Current);
    CHECK_FALSE(rows[2].Future);
    CHECK(rows[2].Steps == 0);
    CHECK(rows[3].Steps == 1);
    CHECK(rows[4].Steps == 2);
    CHECK(rows[5].Label == "Original state");
    CHECK(rows[5].Steps == 3);
    CHECK_FALSE(rows[5].Current);
}

TEST_CASE("History rows handle an empty history and truncation", "[ui][edit]")
{
    const auto now = std::chrono::steady_clock::now();
    GPP::HistoryView empty;
    auto rows = BuildHistoryRows(empty, now);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].Label == "Original state");
    CHECK(rows[0].Current);

    GPP::HistoryView truncated;
    truncated.UndoCount = 5000;
    truncated.Undo = {{"", now}, {"x", now}};
    rows = BuildHistoryRows(truncated, now);
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].Label == "(unnamed edit)");
}

TEST_CASE("Edit labels name the entities involved", "[ui][edit]")
{
    CHECK(CountLabel("Delete", {}) == "Delete Entity");
    CHECK(CountLabel("Delete", {"Earth"}) == "Delete Earth");
    CHECK(CountLabel("Duplicate", {"A", "B", "C"}) == "Duplicate 3 Entities");
    CHECK(SetFieldLabel("Mass", "Earth") == "Set Mass (Earth)");
    CHECK(SetFieldLabel("Mass", "") == "Set Mass");
}

TEST_CASE("Edit requests are taken only by the layer that handles them", "[ui][edit]")
{
    EditRequests requests;
    requests.Push(EditAction::Delete);
    requests.Push(EditAction::FrameSelected);
    requests.Push(EditAction::Duplicate);

    const auto scene = requests.Take({EditAction::Delete, EditAction::Duplicate});
    REQUIRE(scene.size() == 2);
    CHECK(scene[0] == EditAction::Delete);
    CHECK(scene[1] == EditAction::Duplicate);
    CHECK(requests.Take({EditAction::Delete}).empty());
    CHECK(requests.Take({EditAction::FrameSelected}).size() == 1);
    CHECK(requests.Take({EditAction::FrameSelected}).empty());
}

TEST_CASE("Gizmo matrix change ignores float noise but not real moves", "[ui][edit]")
{
    glm::mat4 before(1.0f);
    before[3] = glm::vec4(120.0f, -4.0f, 77.0f, 1.0f);
    CHECK_FALSE(GizmoMatrixChanged(before, before));

    auto noisy = before;
    noisy[3][0] += 1e-5f;
    CHECK_FALSE(GizmoMatrixChanged(before, noisy));

    auto moved = before;
    moved[3][0] += 0.05f;
    CHECK(GizmoMatrixChanged(before, moved));

    auto scaled = before;
    scaled[1][1] = 1.01f;
    CHECK(GizmoMatrixChanged(before, scaled));
}
