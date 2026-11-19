#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

TEST_CASE("Setting overrides parse as key=value pairs separated by ';' or newlines", "[rendering][compare]")
{
    std::vector<std::string> errors;
    const auto parsed = ParseSettingOverrides(" radiometry.doppler = false ;post.bloom=false\n\ndisk.height=0.3; nonsense ;=x", &errors);
    REQUIRE(parsed.size() == 3);
    CHECK(parsed[0] == std::pair<std::string, std::string>{"radiometry.doppler", "false"});
    CHECK(parsed[1] == std::pair<std::string, std::string>{"post.bloom", "false"});
    CHECK(parsed[2] == std::pair<std::string, std::string>{"disk.height", "0.3"});
    CHECK(errors.size() == 2);
    CHECK(ParseSettingOverrides("").empty());
}

TEST_CASE("Comparing needs a profile or overrides", "[rendering][compare]")
{
    RenderSettings settings;
    CHECK_FALSE(settings.Comparing());
    settings.CompareOverrides = "radiometry.doppler=false";
    CHECK(settings.Comparing());
    settings.CompareOverrides.clear();
    settings.CompareProfile = "Traced";
    CHECK(settings.Comparing());
}

TEST_CASE("The right side takes the compare profile, the left side's view settings and the overrides", "[rendering][compare]")
{
    const RenderProfileSet profiles;
    RenderSettings left;
    left.ResolutionScale = 50.0f;
    left.Debug = DebugView::Redshift;
    left.Stage = StageView::Mosaic;
    left.LengthUnit = FrameMode::SolarMass;
    left.CompareProfile = "Traced";
    left.CompareOverrides = "radiometry.doppler=false; disk.volumetric=true";
    left.CompareSplit = 0.3f;

    const auto right = ResolveCompareSettings(left, profiles);
    CHECK(right.Errors.empty());
    CHECK(right.Settings.TransportMode == Transport::Integrated);
    CHECK(right.Settings.MaxRaySteps == profiles.Find("Traced")->Settings.MaxRaySteps);
    CHECK_FALSE(right.Settings.DopplerBeaming);
    CHECK(right.Settings.AccretionDiskVolumetric);
    CHECK(right.Settings.ResolutionScale == Approx(50.0f));
    CHECK(right.Settings.Debug == DebugView::Redshift);
    CHECK(right.Settings.Stage == StageView::Mosaic);
    CHECK(right.Settings.LengthUnit == FrameMode::SolarMass);
    CHECK_FALSE(right.Settings.Comparing());
    CHECK(right.Settings.CompareSplit == Approx(RenderSettings{}.CompareSplit));
    CHECK(right.Label == "Traced + radiometry.doppler=false, disk.volumetric=true");
}

TEST_CASE("Without a compare profile the right side starts from the left side's settings", "[rendering][compare]")
{
    RenderSettings left;
    left.TransportMode = Transport::Integrated;
    left.CompareOverrides = "radiometry.redshift=false";
    const auto right = ResolveCompareSettings(left, RenderProfileSet{});
    CHECK(right.Errors.empty());
    CHECK(right.Settings.TransportMode == Transport::Integrated);
    CHECK_FALSE(right.Settings.GravitationalRedshift);
    CHECK(right.Settings.DopplerBeaming);
    CHECK(right.Label == "Current + radiometry.redshift=false");
}

TEST_CASE("Unknown profiles, bad overrides and compare keys are reported and skipped", "[rendering][compare]")
{
    RenderSettings left;
    left.CompareProfile = "NoSuchProfile";
    left.CompareOverrides = "radiometry.doppler=maybe; no.such=1; compare.split=0.1; post.bloom=false";
    const auto right = ResolveCompareSettings(left, RenderProfileSet{});
    CHECK(right.Errors.size() == 4);
    CHECK_FALSE(right.Settings.Bloom);
    CHECK(right.Settings.DopplerBeaming);
    CHECK(right.Settings.TransportMode == left.TransportMode);
}

TEST_CASE("The new debug and compare settings are registered and round-trip", "[rendering][settings][compare]")
{
    const auto& registry = RenderSettingRegistry();
    for (const auto* key : {"debug.sky", "debug.stage", "compare.profile", "compare.overrides", "compare.split"})
    {
        INFO(key);
        CHECK(registry.Find(key) != nullptr);
    }
    CHECK(registry.Find("compare.profile")->Type == FieldType::String);
    CHECK(registry.Find("debug.view")->Meta.Options.size() == 9);
    CHECK(registry.Find("debug.view")->Meta.Options[static_cast<std::size_t>(DebugView::DopplerShift)] == "DopplerShift");
    CHECK(registry.Find("debug.stage")->Meta.Options.size() == 7);

    RenderSettings changed;
    changed.Sky = SkyBackground::Checkerboard;
    changed.Stage = StageView::Bloom;
    changed.Debug = DebugView::ImageOrder;
    changed.CompareProfile = "Traced";
    changed.CompareOverrides = "radiometry.doppler=false; post.bloom=false";
    changed.CompareSplit = 0.25f;
    RenderSettings loaded;
    CHECK(registry.Decode(loaded, YAML::Load(YAML::Dump(registry.Encode(changed)))).empty());
    CHECK(registry.Diff(changed, loaded).empty());
    RenderSettings parsed;
    REQUIRE(registry.SetFromText(parsed, "compare.overrides", registry.ToText(changed, *registry.Find("compare.overrides"))));
    CHECK(parsed.CompareOverrides == changed.CompareOverrides);
}
