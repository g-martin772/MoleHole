#include <catch2/catch_test_macros.hpp>

import MoleHole;
import GPP;
import std;

using namespace MoleHole;

namespace
{
    float Mono(const std::string& text) { return static_cast<float>(text.size()); }

    GPP::Notification Toast(const std::uint64_t id, const std::string& title, const float age = 0.0f,
                            const std::string& message = {})
    {
        GPP::Notification toast;
        toast.Id = id;
        toast.Title = title;
        toast.Message = message;
        toast.AgeSeconds = age;
        return toast;
    }

    bool Has(const std::vector<StatusItem>& items, const std::string& id)
    {
        return std::ranges::any_of(items, [&](const StatusItem& item) { return item.Id == id; });
    }

    StatusBarModel Busy()
    {
        StatusBarModel model;
        model.Sim = StatusBarModel::SimState::Playing;
        model.Tick = 123456;
        model.SimTimeSeconds = 75.0;
        model.TickRateHz = 60.0;
        model.GpuMs = 4.2;
        model.Fps = 58.0;
        model.ResolutionLowered = true;
        model.TracerPercent = 70.0f;
        model.Dirty = true;
        model.AutosaveEnabled = true;
        model.SecondsSinceSave = 30.0;
        model.ShaderLabel = "Shader error: a.comp:7";
        model.ShaderShortLabel = "Shader error";
        return model;
    }
}

TEST_CASE("Status bar time formats", "[ui][status]")
{
    CHECK(FormatSimTime(0.0) == "0 s");
    CHECK(FormatSimTime(0.5) == "500.000 ms");
    CHECK(FormatSimTime(75.0) == "00:01:15");
    CHECK(FormatSimTime(90061.0) == "1 d 01:01:01");
    CHECK(FormatAgo(1.0) == "just now");
    CHECK(FormatAgo(12.0) == "12 s ago");
    CHECK(FormatAgo(125.0) == "2 min ago");
    CHECK(FormatAgo(7300.0) == "2 h ago");
    CHECK(FormatAgo(200000.0) == "2 d ago");
    CHECK(FormatCount(0) == "0");
    CHECK(FormatCount(999) == "999");
    CHECK(FormatCount(1234567) == "1,234,567");
    CHECK(FormatRate(60.0) == "60 Hz");
    CHECK(FormatRate(59.5) == "59.5 Hz");
}

TEST_CASE("FirstLines keeps the leading lines", "[ui][status]")
{
    bool truncated = false;
    CHECK(FirstLines("a\nb\nc", 2, &truncated) == "a\nb");
    CHECK(truncated);
    CHECK(FirstLines("a\nb\n", 5, &truncated) == "a\nb");
    CHECK_FALSE(truncated);
    CHECK(FirstLines("", 3).empty());
}

TEST_CASE("TruncateToWidth fits the width with an ellipsis", "[ui][status]")
{
    CHECK(TruncateToWidth("hello", 10.0f, Mono) == "hello");
    CHECK(TruncateToWidth("hello world", 8.0f, Mono) == "hello...");
    CHECK(TruncateToWidth("hello", 2.0f, Mono).empty());
    CHECK(TruncateToWidth("hello", 0.0f, Mono).empty());
    for (float width = 0.0f; width < 20.0f; width += 1.0f)
        CHECK(Mono(TruncateToWidth("some longer notification text", width, Mono)) <= width);
}

TEST_CASE("Status items describe the simulation and autosave state", "[ui][status]")
{
    StatusBarModel model;
    auto items = BuildStatusItems(model);
    REQUIRE(items.Left.size() == 1);
    CHECK(items.Left[0].Text[0] == "No scene");
    CHECK_FALSE(Has(items.Right, "autosave"));

    model = Busy();
    items = BuildStatusItems(model);
    CHECK(items.Left[0].Text[0] == "Playing");
    CHECK(items.Left[1].Text[0] == "t = 00:01:15");
    CHECK(items.Left[2].Text[0] == "tick 123,456");
    const auto autosave = std::ranges::find(items.Right, "autosave", &StatusItem::Id);
    REQUIRE(autosave != items.Right.end());
    CHECK(autosave->Text[0] == "Autosave on, saved 30 s ago");
    CHECK(autosave->Text.back() == "Autosave on");

    model.AutosaveFailing = true;
    items = BuildStatusItems(model);
    const auto failing = std::ranges::find(items.Right, "autosave", &StatusItem::Id);
    CHECK(failing->Tone == StatusTone::Error);

    model.Dirty = false;
    model.ResolutionLowered = false;
    model.ShaderLabel.clear();
    items = BuildStatusItems(model);
    CHECK_FALSE(Has(items.Right, "dirty"));
    CHECK_FALSE(Has(items.Right, "governor"));
    CHECK_FALSE(Has(items.Right, "shader-error"));
}

TEST_CASE("Status layout drops low priority items first and keeps the shader error", "[ui][status]")
{
    const auto items = BuildStatusItems(Busy());
    const auto ids = [&](const StatusLayout& layout)
    {
        std::vector<std::string> result;
        for (const auto i : layout.Left) result.push_back(items.Left[i].Id);
        for (const auto i : layout.Right) result.push_back(items.Right[i].Id);
        return result;
    };
    const auto contains = [&](const StatusLayout& layout, const std::string& id)
    {
        return std::ranges::contains(ids(layout), id);
    };

    const auto wide = LayoutStatusBar(items, 2000.0f, 2.0f, 10.0f, 2.0f, Mono);
    CHECK(ids(wide).size() == items.Left.size() + items.Right.size());
    CHECK(wide.MiddleWidth > 10.0f);

    const auto medium = LayoutStatusBar(items, 90.0f, 2.0f, 10.0f, 2.0f, Mono);
    CHECK(contains(medium, "shader-error"));
    CHECK(contains(medium, "sim-state"));
    CHECK_FALSE(contains(medium, "tick-rate"));
    CHECK_FALSE(contains(medium, "autosave"));

    const auto narrow = LayoutStatusBar(items, 25.0f, 2.0f, 4.0f, 2.0f, Mono);
    CHECK(contains(narrow, "shader-error"));
    CHECK_FALSE(contains(narrow, "tick"));

    const auto tiny = LayoutStatusBar(items, 0.0f, 2.0f, 4.0f, 2.0f, Mono);
    CHECK(ids(tiny).empty());
    CHECK(tiny.MiddleWidth == 0.0f);

    for (float width = 0.0f; width < 400.0f; width += 7.0f)
    {
        const auto layout = LayoutStatusBar(items, width, 2.0f, 10.0f, 2.0f, Mono);
        float used = 0.0f;
        int count = 0;
        for (std::size_t n = 0; n < layout.Left.size(); ++n, ++count)
            used += Mono(items.Left[layout.Left[n]].Text[layout.LeftVariant[n]]) + (items.Left[layout.Left[n]].Chip ? 2.0f : 0.0f);
        for (std::size_t n = 0; n < layout.Right.size(); ++n, ++count)
            used += Mono(items.Right[layout.Right[n]].Text[layout.RightVariant[n]]) + (items.Right[layout.Right[n]].Chip ? 2.0f : 0.0f);
        CHECK(used + (count > 1 ? static_cast<float>(count - 1) * 2.0f : 0.0f) <= width);
    }
}

TEST_CASE("Shader failures summarise to one chip label", "[ui][status]")
{
    CHECK_FALSE(SummarizeShaderFailures({}).has_value());

    std::vector<GPP::ShaderFailure> failures(3);
    failures[0].File = "/s/flat.comp";
    failures[0].Line = 42;
    failures[1].File = "/other/flat.comp";
    failures[1].Line = 42;
    failures[2].File = "/s/common.glsl";
    failures[2].Line = 3;
    const auto summary = SummarizeShaderFailures(failures);
    REQUIRE(summary);
    CHECK(summary->File == "flat.comp");
    CHECK(summary->Line == 42);
    CHECK(summary->Extra == 1);

    StatusBarModel model;
    FillShaderLabels(model, *summary);
    CHECK(model.ShaderLabel == "Shader error: flat.comp:42 (+1)");
    CHECK(model.ShaderShortLabel == "Shader error (+1)");

    FillShaderLabels(model, {"a.comp", 0, 0});
    CHECK(model.ShaderLabel == "Shader error: a.comp");
}

TEST_CASE("Notification tracker follows the newest post and fades", "[ui][status]")
{
    NotificationTracker tracker;
    CHECK_FALSE(tracker.Current(0.0).has_value());

    std::vector<GPP::Notification> toasts{Toast(1, "Saved", 0.0f, "/a/b\nsecond")};
    tracker.Observe(toasts, 10.0);
    REQUIRE(tracker.Current(10.0));
    CHECK(tracker.Current(10.0)->Text == "Saved: /a/b");

    toasts = {Toast(1, "Saved", 2.0f, "/a/b\nsecond")};
    tracker.Observe(toasts, 12.0);
    CHECK(tracker.Current(12.0)->Text == "Saved: /a/b");

    toasts = {Toast(1, "Saved", 3.0f), Toast(2, "Failed", 0.0f)};
    tracker.Observe(toasts, 13.0);
    CHECK(tracker.Current(13.0)->Text == "Failed");

    toasts = {Toast(1, "Saved", 0.0f), Toast(2, "Failed", 0.5f)};
    tracker.Observe(toasts, 14.0);
    CHECK(tracker.Current(14.0)->Text == "Saved");

    tracker.Observe({}, 15.0);
    CHECK(tracker.Current(14.0 + NotificationTracker::kHoldSeconds - 0.1)->Alpha == 1.0f);
    const auto fading = tracker.Current(14.0 + NotificationTracker::kHoldSeconds + NotificationTracker::kFadeSeconds * 0.5);
    REQUIRE(fading);
    CHECK(fading->Alpha < 1.0f);
    CHECK(fading->Alpha > 0.0f);
    CHECK_FALSE(tracker.Current(14.0 + NotificationTracker::kHoldSeconds + NotificationTracker::kFadeSeconds + 0.1).has_value());
}
