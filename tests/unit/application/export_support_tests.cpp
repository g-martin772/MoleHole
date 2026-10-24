#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <yaml-cpp/yaml.h>

import MoleHole;
import std;

using namespace MoleHole;
using Catch::Matchers::WithinAbs;

TEST_CASE("Sequence frames are numbered from one with five digits", "[export]")
{
    CHECK(SequenceFramePath("out", "shot", 1) == std::filesystem::path("out/shot_00001.png"));
    CHECK(SequenceFramePath("out", "shot", 42) == std::filesystem::path("out/shot_00042.png"));
    CHECK(SequenceFramePath("out", "shot", 123456) == std::filesystem::path("out/shot_123456.png"));
}

TEST_CASE("Sequence names are made safe for file names", "[export]")
{
    CHECK(SanitizeSequenceName("a/b\\c") == "a_b_c");
    CHECK(SanitizeSequenceName("") == "frame");
    CHECK(SanitizeSequenceName("   ") == "frame");
    CHECK(SanitizeSequenceName("..hidden") == "hidden");
    CHECK(SanitizeSequenceName(" my shot ") == "my shot");
    CHECK(SequenceFramePath("d", "x/y", 3) == std::filesystem::path("d/x_y_00003.png"));
}

TEST_CASE("Next sequence directory skips existing ones", "[export]")
{
    const auto dir = std::filesystem::temp_directory_path() / "molehole_sequence_dir_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    CHECK(NextSequenceDirectory(dir).filename() == "sequence_001");
    std::filesystem::create_directories(dir / "sequence_001");
    std::filesystem::create_directories(dir / "sequence_002");
    CHECK(NextSequenceDirectory(dir).filename() == "sequence_003");
    std::filesystem::remove_all(dir);
}

TEST_CASE("Remaining time extrapolates from the frame interval", "[export]")
{
    CHECK_FALSE(EstimateRemainingSeconds(0.0, 0, 100));
    CHECK_FALSE(EstimateRemainingSeconds(5.0, 1, 100));
    CHECK_FALSE(EstimateRemainingSeconds(0.0, 5, 100));
    CHECK_FALSE(EstimateRemainingSeconds(5.0, 5, 0));
    CHECK_THAT(*EstimateRemainingSeconds(10.0, 11, 111), WithinAbs(100.0, 1e-9));
    CHECK_THAT(*EstimateRemainingSeconds(1.0, 2, 3), WithinAbs(1.0, 1e-9));
    CHECK_THAT(*EstimateRemainingSeconds(10.0, 100, 100), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Durations and timing lines are formatted", "[export]")
{
    CHECK(FormatDuration(0.0) == "0s");
    CHECK(FormatDuration(59.4) == "59s");
    CHECK(FormatDuration(61.0) == "1m 01s");
    CHECK(FormatDuration(3725.0) == "1h 02m 05s");
    CHECK(FormatDuration(-3.0) == "0s");
    CHECK(FormatExportTiming(12.0, -1.0) == "Elapsed 12s");
    CHECK(FormatExportTiming(12.0, 65.0) == "Elapsed 12s | ETA 1m 05s");
}

TEST_CASE("Export settings survive a YAML round trip", "[export]")
{
    ExportSettings settings;
    settings.Format = ExportFormat::Sequence;
    settings.Width = 1280;
    settings.Height = 720;
    settings.DurationSeconds = 2.5f;
    settings.Framerate = 24;
    settings.Profile = "Traced";
    settings.OverrideQuality = true;
    settings.RayStepSize = 0.005f;
    settings.MaxRaySteps = 50000;
    settings.SequenceFolder = "/tmp/frames";
    settings.SequenceName = "shot";

    YAML::Emitter out;
    out << settings.ToYaml();
    CHECK(ExportSettings::FromYaml(YAML::Load(out.c_str())) == settings);
}

TEST_CASE("Export settings fall back to defaults and clamp bad input", "[export]")
{
    CHECK(ExportSettings::FromYaml(YAML::Node()) == ExportSettings{});
    CHECK(ExportSettings::FromYaml(YAML::Load("[1, 2]")) == ExportSettings{});

    const auto bad = ExportSettings::FromYaml(YAML::Load(
        "Format: Hologram\nWidth: -5\nHeight: 99999999\nFramerate: abc\nDurationSeconds: 0\nMaxRaySteps: 3\nSequenceName: ''"));
    CHECK(bad.Format == ExportFormat::Image);
    CHECK(bad.Width == 1);
    CHECK(bad.Height == 16384);
    CHECK(bad.Framerate == ExportSettings{}.Framerate);
    CHECK(bad.DurationSeconds >= 0.1f);
    CHECK(bad.MaxRaySteps == 1000);
    CHECK(bad.SequenceName == "frame");

    const auto partial = ExportSettings::FromYaml(YAML::Load("Format: Video\nWidth: 640"));
    CHECK(partial.Format == ExportFormat::Video);
    CHECK(partial.Width == 640);
    CHECK(partial.Height == ExportSettings{}.Height);
}

TEST_CASE("Sequence export flags are parsed", "[export][cli]")
{
    std::array<const char*, 7> raw{"MoleHole", "--export-sequence", "out/frames", "--sequence-name", "a/b", "--fps", "24"};
    const auto args = ParseExportArgs(static_cast<int>(raw.size()), const_cast<char**>(raw.data()));
    REQUIRE(args);
    CHECK(args->Request.RequestKind == ExportRequest::Kind::Sequence);
    CHECK(args->Request.OutputPath == "out/frames");
    CHECK(args->Request.SequenceName == "a_b");
    CHECK(args->Request.Framerate == 24);
}
