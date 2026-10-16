#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct TempDir
    {
        std::filesystem::path Path = std::filesystem::temp_directory_path() / "molehole_sidecar_tests";

        TempDir()
        {
            std::filesystem::remove_all(Path);
            std::filesystem::create_directories(Path);
        }

        ~TempDir() { std::filesystem::remove_all(Path); }

        void Write(const std::filesystem::path& relative, const std::string& text) const
        {
            const auto file = Path / relative;
            std::filesystem::create_directories(file.parent_path());
            std::ofstream(file) << text;
        }
    };
}

TEST_CASE("HashText is FNV-1a 64", "[rendering][sidecar]")
{
    CHECK(HashHex("") == "cbf29ce484222325");
    CHECK(HashHex("a") == "af63dc4c8601ec8c");
    CHECK(HashHex("foobar") == "85944171f73967e8");
    CHECK(HashText("scene") != HashText("scene "));
}

TEST_CASE("The git commit is read from a checkout without running git", "[rendering][sidecar]")
{
    const TempDir dir;
    CHECK(ReadGitCommit(dir.Path) == "unknown");

    dir.Write(".git/HEAD", "ref: refs/heads/main\n");
    CHECK(ReadGitCommit(dir.Path) == "unknown");

    dir.Write(".git/packed-refs", "# pack-refs\n0123456789abcdef0123456789abcdef01234567 refs/heads/other\n"
                                  "89abcdef0123456789abcdef0123456789abcdef refs/heads/main\n^ffff\n");
    CHECK(ReadGitCommit(dir.Path) == "89abcdef0123");

    dir.Write(".git/refs/heads/main", "fedcba9876543210fedcba9876543210fedcba98\n");
    CHECK(ReadGitCommit(dir.Path) == "fedcba987654");

    dir.Write(".git/HEAD", "1234567890abcdef1234567890abcdef12345678\n");
    CHECK(ReadGitCommit(dir.Path) == "1234567890ab");
}

TEST_CASE("The sidecar records what is needed to reproduce the image", "[rendering][sidecar]")
{
    RenderSidecarInfo info;
    info.OutputPath = "/out/render_001.png";
    info.Width = 1920;
    info.Height = 1080;
    info.ScenePath = "templates/dev.yaml";
    info.SceneName = "Dev";
    info.SceneYaml = "Entities: []";
    info.ProfileName = "Reference";
    info.Settings.TransportMode = Transport::Integrated;
    info.Settings.RayStepSize = 0.02f;
    info.Overrides = {{"post.bloom", "off"}};
    info.CameraPositionM = {1.0, 2.0, 3.0};
    info.GitCommit = "abc123";
    info.WallSeconds = 12.5;
    info.GpuMs = 41.5;
    info.Timestamp = "2026-10-16T12:00:00Z";
    info.Gpu.DeviceName = "Test GPU";
    info.Gpu.RayQuery = true;
    info.Gpu.Reason = "only suitable device";

    const auto node = YAML::Load(BuildRenderSidecar(info));
    CHECK(node["GitCommit"].as<std::string>() == "abc123");
    CHECK(node["GpuMs"].as<double>() == 41.5);
    CHECK(node["Output"]["Width"].as<int>() == 1920);
    CHECK(node["Output"]["Kind"].as<std::string>() == "image");
    CHECK_FALSE(node["Output"]["Framerate"]);
    CHECK(node["Scene"]["Hash"].as<std::string>() == HashHex("Entities: []"));
    CHECK(node["Camera"]["PositionM"][2].as<double>() == 3.0);
    CHECK(node["Profile"]["Name"].as<std::string>() == "Reference");
    CHECK(node["Profile"]["Overrides"]["post.bloom"].as<std::string>() == "off");
    CHECK(node["Gpu"]["Capabilities"]["rayQuery"].as<bool>());
    CHECK_FALSE(node["Gpu"]["Capabilities"]["shaderFloat64"].as<bool>());

    // The settings are complete: loading them reproduces the render without any profile
    RenderSettings loaded;
    CHECK(RenderSettingRegistry().Decode(loaded, node["Settings"]).empty());
    CHECK(RenderSettingRegistry().Diff(loaded, info.Settings).empty());
    CHECK(node["Settings"].size() == RenderSettingRegistry().All().size());

    info.Video = true;
    info.Framerate = 24;
    CHECK(YAML::Load(BuildRenderSidecar(info))["Output"]["Framerate"].as<int>() == 24);
}

TEST_CASE("The sidecar sits next to the output and replaces its extension", "[rendering][sidecar]")
{
    CHECK(SidecarPathFor("/out/render_001.png") == "/out/render_001.render.yaml");
    CHECK(SidecarPathFor("clip.mp4") == "clip.render.yaml");

    const TempDir dir;
    RenderSidecarInfo info;
    info.OutputPath = (dir.Path / "shot.png").string();
    REQUIRE(WriteRenderSidecar(dir.Path / "shot.png", info));
    CHECK(std::filesystem::exists(dir.Path / "shot.render.yaml"));
    CHECK_FALSE(WriteRenderSidecar(dir.Path / "missing" / "shot.png", info));
}
