#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::vector<std::filesystem::path> TemplatePaths()
    {
        std::vector<std::filesystem::path> result;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "templates"))
        {
            if (entry.path().extension() == ".yaml") result.push_back(entry.path());
        }
        std::ranges::sort(result);
        return result;
    }
}

TEST_CASE("Every scene template loads and re-serializes stably", "[simulation][scene][templates]")
{
    RegisterComponents();

    const auto paths = TemplatePaths();
    REQUIRE_FALSE(paths.empty());

    for (const auto& path : paths)
    {
        INFO(path.string());
        Scene first;
        first.DeserializeFromYaml(ReadFile(path));
        const auto firstYaml = first.SerializeToYaml();

        Scene second;
        second.DeserializeFromYaml(firstYaml);
        CHECK(second.SerializeToYaml() == firstYaml);

        if (const char* dump = std::getenv("MOLEHOLE_DUMP_TEMPLATES"))
        {
            std::ofstream(std::filesystem::path(dump) / path.filename()) << firstYaml;
        }
    }
}

TEST_CASE("Every scene template validates without errors", "[simulation][scene][templates][validation]")
{
    RegisterComponents();

    for (const auto& path : TemplatePaths())
    {
        const auto diagnostics = ValidateSceneFile(path);
        INFO(path.string());
        for (const auto& diagnostic : diagnostics) UNSCOPED_INFO(diagnostic.Format(path.filename().string()));
        CHECK(CountDiagnostics(diagnostics, DiagnosticSeverity::Error) == 0);
        if (std::getenv("MOLEHOLE_LIST_TEMPLATE_WARNINGS"))
        {
            for (const auto& diagnostic : diagnostics) std::println("{}", diagnostic.Format(path.filename().string()));
        }
    }
}

TEST_CASE("Validation CLI exit codes", "[application][validation]")
{
    RegisterComponents();
    const auto dir = std::filesystem::temp_directory_path() / "molehole_validate_cli";
    std::filesystem::create_directories(dir);
    const auto clean = dir / "clean.yaml";
    const auto bad = dir / "bad.yaml";
    { std::ofstream(clean) << "Scene: C\nSchemaVersion: 2\nEntities:\n  - Guid: 1\n    Components:\n      Metadata: {Name: A}\n"; }
    { std::ofstream(bad) << "Scene: B\nEntities: [\n"; }

    CHECK(RunValidateScene(clean.string()) == 0);
    CHECK(RunValidateScene(bad.string()) == 1);
    CHECK(RunValidateScene((dir / "missing.yaml").string()) == 1);
    CHECK(RunValidateScene("") == 2);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Template meshes use relative asset paths that exist", "[simulation][scene][templates][install]")
{
    RegisterComponents();

    for (const auto& path : TemplatePaths())
    {
        Scene scene;
        scene.DeserializeFromYaml(ReadFile(path));
        for (auto [entity, mesh] : scene.Registry().view<const MeshComponent>().each())
        {
            if (mesh.AssetPath.empty()) continue;
            INFO(path.filename().string() << ": " << mesh.AssetPath);
            CHECK_FALSE(std::filesystem::path(mesh.AssetPath).is_absolute());
            CHECK(std::filesystem::exists(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / mesh.AssetPath));
        }
    }
}

TEST_CASE("The default scene holds a black hole of astrophysical mass", "[simulation][scene]")
{
    RegisterComponents();

    Scene scene("Default");
    PopulateDefaultScene(scene, "Black Hole");

    std::size_t holes = 0;
    for (auto [entity, hole] : scene.Registry().view<const BlackHoleComponent>().each())
    {
        ++holes;
        CHECK(hole.Mass >= 0.5 * Units::kSolarMass);
    }
    CHECK(holes == 1);
}
