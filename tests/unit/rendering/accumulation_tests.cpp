#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

import MoleHole;
import GPP;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Matchers::WithinAbs;

namespace
{
    AccumulationRequest Still(const std::uint64_t signature, const bool accumulate, const std::uint32_t max = 4,
                              const bool settled = true)
    {
        return {.Signature = signature, .Accumulate = accumulate, .MaxSamples = max, .Settled = settled};
    }
}

TEST_CASE("Halton sequences are the radical inverse in their base", "[rendering][accumulation]")
{
    CHECK(Halton(0, 2) == 0.0);
    CHECK(Halton(1, 2) == 0.5);
    CHECK(Halton(2, 2) == 0.25);
    CHECK(Halton(3, 2) == 0.75);
    CHECK(Halton(4, 2) == 0.125);
    CHECK_THAT(Halton(1, 3), WithinAbs(1.0 / 3.0, 1e-12));
    CHECK_THAT(Halton(2, 3), WithinAbs(2.0 / 3.0, 1e-12));
    CHECK_THAT(Halton(3, 3), WithinAbs(1.0 / 9.0, 1e-12));
}

TEST_CASE("The jitter starts at the pixel centre and then covers the pixel evenly", "[rendering][accumulation]")
{
    CHECK(AccumulationJitter(0) == glm::vec2(0.0f));

    glm::dvec2 sum{0.0};
    std::set<std::pair<float, float>> distinct;
    constexpr std::uint32_t kCount = 255;
    for (std::uint32_t i = 1; i <= kCount; ++i)
    {
        const auto jitter = AccumulationJitter(i);
        CHECK(jitter.x >= -0.5f);
        CHECK(jitter.x < 0.5f);
        CHECK(jitter.y >= -0.5f);
        CHECK(jitter.y < 0.5f);
        distinct.emplace(jitter.x, jitter.y);
        sum += glm::dvec2(jitter);
    }
    CHECK(distinct.size() == kCount);
    CHECK_THAT(sum.x / kCount, WithinAbs(0.0, 0.01));
    CHECK_THAT(sum.y / kCount, WithinAbs(0.0, 0.01));
}

TEST_CASE("Blending with 1/N weights is the arithmetic mean", "[rendering][accumulation]")
{
    CHECK(AccumulationWeight(0) == 1.0f);
    CHECK(AccumulationWeight(1) == 0.5f);

    const std::array<float, 5> samples{3.0f, 1.0f, 4.0f, 1.0f, 5.0f};
    float history = 0.0f;
    for (std::uint32_t i = 0; i < samples.size(); ++i)
    {
        const float weight = AccumulationWeight(i);
        history = weight >= 1.0f ? samples[i] : history + (samples[i] - history) * weight;
    }
    CHECK_THAT(history, WithinAbs(2.8, 1e-5));
}

TEST_CASE("The signature changes with any byte, any order and the buffer boundaries", "[rendering][accumulation]")
{
    const auto digest = [](const auto& fill)
    {
        SignatureHasher hasher;
        fill(hasher);
        return hasher.Digest();
    };
    const std::array<float, 3> a{1.0f, 2.0f, 3.0f};
    const std::array<float, 3> b{1.0f, 2.0f, 3.5f};
    const auto base = digest([&](SignatureHasher& h) { h.Value(a); });
    CHECK(base == digest([&](SignatureHasher& h) { h.Value(a); }));
    CHECK(base != digest([&](SignatureHasher& h) { h.Value(b); }));
    CHECK(digest([&](SignatureHasher& h) { h.Value(1.0f); h.Value(2.0f); }) != digest([&](SignatureHasher& h) { h.Value(2.0f); h.Value(1.0f); }));

    const std::array<std::uint8_t, 4> bytes{1, 2, 3, 4};
    CHECK(digest([&](SignatureHasher& h) { h.Bytes(bytes.data(), 2); h.Bytes(bytes.data() + 2, 2); })
          != digest([&](SignatureHasher& h) { h.Bytes(bytes.data(), 4); }));
    CHECK(digest([&](SignatureHasher& h) { h.Bytes(bytes.data(), 3); }) != digest([&](SignatureHasher& h) { h.Bytes(bytes.data(), 4); }));

    SignatureHasher reset;
    reset.Value(a);
    reset.Reset();
    CHECK(reset.Digest() == SignatureHasher{}.Digest());
}

TEST_CASE("The settings hash follows every setting", "[rendering][accumulation]")
{
    const auto digest = [](const RenderSettings& settings)
    {
        SignatureHasher hasher;
        HashSettings(hasher, settings);
        return hasher.Digest();
    };
    const RenderSettings base;
    CHECK(digest(base) == digest(RenderSettings{}));
    for (const auto& setting : RenderSettingRegistry().All())
    {
        INFO(setting.Key);
        RenderSettings changed = base;
        const auto text = RenderSettingRegistry().ToText(base, setting);
        std::string other = text == "true" ? "false" : "true";
        if (setting.Type == FieldType::Float || setting.Type == FieldType::Int) other = std::format("{}", setting.Meta.Max);
        if (setting.Type == FieldType::Enum) other = setting.Meta.Options.back();
        if (other == text) other = setting.Type == FieldType::Enum ? setting.Meta.Options.front() : std::format("{}", setting.Meta.Min);
        if (!RenderSettingRegistry().SetFromText(changed, setting.Key, other)) continue;
        if (RenderSettingRegistry().Diff(base, changed).empty()) continue;
        CHECK(digest(changed) != digest(base));
    }
}

TEST_CASE("Accumulation is a default of the interactive profiles and off for the reference one", "[rendering][settings]")
{
    const auto* accumulate = RenderSettingRegistry().Find("sampling.accumulate");
    const auto* limit = RenderSettingRegistry().Find("sampling.accumulate_max");
    REQUIRE(accumulate != nullptr);
    REQUIRE(limit != nullptr);
    CHECK(accumulate->Meta.Group == "Sampling");
    CHECK(limit->Meta.Min >= 2);
    CHECK_FALSE(limit->Meta.VisibleWhen.empty());

    const RenderProfileSet set;
    for (const auto* name : {"Flat", "Lensed", "Traced"})
    {
        INFO(name);
        CHECK(set.Find(name)->Settings.Accumulate);
    }
    CHECK_FALSE(set.Find("Reference")->Settings.Accumulate);
    CHECK(RenderSettings{}.AccumulateMax >= 2);
}

TEST_CASE("A frame that differs from the last one is drawn plainly", "[rendering][accumulation]")
{
    AccumulationPlanner planner;
    for (std::uint64_t signature = 1; signature <= 3; ++signature)
    {
        const auto plan = planner.Next(Still(signature, true));
        CHECK(plan.Render);
        CHECK_FALSE(plan.Accumulate);
        CHECK(plan.Jitter == glm::vec2(0.0f));
        CHECK(plan.Weight == 1.0f);
    }
}

TEST_CASE("Without accumulation a still view is drawn once and then skipped", "[rendering][accumulation]")
{
    AccumulationPlanner planner;
    CHECK(planner.Next(Still(7, false)).Render);
    for (int i = 0; i < 5; ++i) CHECK_FALSE(planner.Next(Still(7, false)).Render);
    CHECK(planner.Next(Still(8, false)).Render);
    CHECK_FALSE(planner.Next(Still(8, false)).Render);
}

TEST_CASE("A still view refines with Halton offsets and 1/N weights, then stops", "[rendering][accumulation]")
{
    AccumulationPlanner planner;
    CHECK(planner.Next(Still(5, true, 4)).Render);

    for (std::uint32_t n = 1; n <= 4; ++n)
    {
        const auto plan = planner.Next(Still(5, true, 4));
        INFO(n);
        REQUIRE(plan.Render);
        CHECK(plan.Accumulate);
        CHECK(plan.Samples == n);
        CHECK(plan.Weight == AccumulationWeight(n - 1));
        CHECK(plan.Jitter == AccumulationJitter(n));
        CHECK(plan.Converged == (n == 4));
    }
    const auto done = planner.Next(Still(5, true, 4));
    CHECK_FALSE(done.Render);
    CHECK(done.Converged);
    CHECK_FALSE(planner.Next(Still(5, true, 4)).Render);

    const auto restart = planner.Next(Still(6, true, 4));
    CHECK(restart.Render);
    CHECK_FALSE(restart.Accumulate);
    CHECK(planner.Next(Still(6, true, 4)).Accumulate);
    CHECK(planner.Samples() == 1);
}

TEST_CASE("Frames are drawn plainly until what they depend on has settled", "[rendering][accumulation]")
{
    AccumulationPlanner planner;
    for (int i = 0; i < 3; ++i)
    {
        const auto plan = planner.Next(Still(9, true, 4, false));
        CHECK(plan.Render);
        CHECK_FALSE(plan.Accumulate);
    }
    const auto settled = planner.Next(Still(9, true, 4, true));
    CHECK(settled.Render);
    CHECK_FALSE(settled.Accumulate);
    const auto first = planner.Next(Still(9, true, 4, true));
    CHECK(first.Accumulate);
    CHECK(first.Weight == 1.0f);

    AccumulationPlanner idle;
    CHECK(idle.Next(Still(9, false, 4, false)).Render);
    CHECK(idle.Next(Still(9, false, 4, true)).Render);
    CHECK_FALSE(idle.Next(Still(9, false, 4, true)).Render);
}

TEST_CASE("Reset forgets the last frame", "[rendering][accumulation]")
{
    AccumulationPlanner planner;
    (void)planner.Next(Still(1, false));
    CHECK_FALSE(planner.Next(Still(1, false)).Render);
    planner.Reset();
    CHECK(planner.Next(Still(1, false)).Render);
}

namespace
{
    LadderRequest Ladder(const bool enabled, const float interactive, const double stillMs, const std::uint64_t frames)
    {
        return {.Enabled = enabled, .InteractiveScale = interactive, .StillMs = stillMs, .FramesDone = frames};
    }
}

TEST_CASE("The ladder runs from the interactive scale up to full resolution", "[rendering][ladder]")
{
    CHECK(LadderRungs(0.4f) == std::vector<float>{0.4f, 0.5f, 0.75f, 1.0f});
    CHECK(LadderRungs(0.6f) == std::vector<float>{0.6f, 0.75f, 1.0f});
    CHECK(LadderRungs(0.9f) == std::vector<float>{0.9f, 1.0f});
    CHECK(LadderRungs(1.0f) == std::vector<float>{1.0f});
    CHECK(LadderRungs(0.49f) == std::vector<float>{0.49f, 0.75f, 1.0f});
}

TEST_CASE("A moving view stays at the governor scale without accumulating", "[rendering][ladder]")
{
    RefinementLadder ladder;
    const auto step = ladder.Next(Ladder(true, 0.4f, 50.0, 10));
    CHECK(step.Scale == 0.4f);
    CHECK_FALSE(step.Accumulate);
    CHECK(step.Rung == 0);
    CHECK_FALSE(step.Climbing);
}

TEST_CASE("Once still the ladder climbs one rung per finished frame and accumulates on the last", "[rendering][ladder]")
{
    RefinementLadder ladder;
    std::uint64_t frames = 100;
    std::vector<float> scales;
    for (int i = 0; i < 12; ++i)
    {
        const auto step = ladder.Next(Ladder(true, 0.4f, 500.0, frames));
        if (scales.empty() || scales.back() != step.Scale) scales.push_back(step.Scale);
        CHECK(step.Accumulate == (step.Scale == 1.0f));
        CHECK(step.Climbing == (step.Scale < 1.0f));
        if (i % 2 == 1) ++frames;
    }
    CHECK(scales == std::vector<float>{0.5f, 0.75f, 1.0f});
}

TEST_CASE("The ladder waits for a frame at the current rung before climbing", "[rendering][ladder]")
{
    RefinementLadder ladder;
    CHECK(ladder.Next(Ladder(true, 0.4f, 300.0, 5)).Scale == 0.5f);
    CHECK(ladder.Next(Ladder(true, 0.4f, 320.0, 5)).Scale == 0.5f);
    CHECK(ladder.Next(Ladder(true, 0.4f, 340.0, 6)).Scale == 0.75f);
    CHECK(ladder.Next(Ladder(true, 0.4f, 360.0, 6)).Scale == 0.75f);
}

TEST_CASE("Any movement drops the ladder straight back to the interactive scale", "[rendering][ladder]")
{
    RefinementLadder ladder;
    (void)ladder.Next(Ladder(true, 0.4f, 300.0, 1));
    (void)ladder.Next(Ladder(true, 0.4f, 320.0, 2));
    CHECK(ladder.Next(Ladder(true, 0.4f, 340.0, 3)).Scale == 1.0f);
    const auto moved = ladder.Next(Ladder(true, 0.4f, 10.0, 4));
    CHECK(moved.Scale == 0.4f);
    CHECK_FALSE(moved.Accumulate);
    CHECK(ladder.Next(Ladder(true, 0.4f, 300.0, 4)).Scale == 0.5f);
}

TEST_CASE("Without a governor scale or outside interactive contexts there is nothing to climb", "[rendering][ladder]")
{
    RefinementLadder ladder;
    const auto full = ladder.Next(Ladder(true, 1.0f, 500.0, 1));
    CHECK(full.Scale == 1.0f);
    CHECK(full.Accumulate);
    CHECK_FALSE(full.Climbing);

    const auto bypass = ladder.Next(Ladder(false, 0.4f, 500.0, 1));
    CHECK(bypass.Scale == 0.4f);
    CHECK(bypass.Accumulate);
    CHECK(bypass.Rung == 0);
}

TEST_CASE("The governor ignores frame times while held and resumes from its last scale", "[rendering][governor]")
{
    using namespace std::chrono_literals;
    FrameGovernor governor;
    const auto t0 = std::chrono::steady_clock::now();
    float scale = 1.0f;
    for (int i = 1; i <= 4; ++i) scale = governor.Update(true, 100.0, 33.3, t0 + i * 600ms);
    CHECK(scale < 1.0f);

    const float interactive = scale;
    governor.Hold(t0 + 6000ms);
    CHECK(governor.Update(true, 400.0, 33.3, t0 + 3000ms) == interactive);
    CHECK(governor.Update(true, 400.0, 33.3, t0 + 5900ms) == interactive);
    CHECK(governor.Update(true, 30.0, 33.3, t0 + 6100ms) == interactive);
    CHECK(governor.Update(true, 100.0, 33.3, t0 + 6700ms) < interactive);
}
