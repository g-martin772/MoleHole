#include <catch2/catch_approx.hpp>
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
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::optional<SimulateArgs> Parse(std::vector<std::string> args)
    {
        args.insert(args.begin(), "molehole");
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        return ParseSimulateArgs(static_cast<int>(argv.size()), argv.data());
    }

    int Simulate(const std::string& scene, const std::string& ticks, const std::filesystem::path& out,
                 std::vector<std::string> extra = {})
    {
        std::vector<std::string> args{"--simulate", scene, "--ticks", ticks, "--out", out.string()};
        args.insert(args.end(), extra.begin(), extra.end());
        const auto parsed = Parse(args);
        REQUIRE(parsed.has_value());
        return RunSimulate(*parsed);
    }
}

TEST_CASE("Simulate flags parse and reject bad values", "[simulate][cli]")
{
    CHECK_FALSE(Parse({"--scene", "a.yaml"}).has_value());

    const auto good = Parse({"--simulate", "a.yaml", "--ticks", "10", "--dt", "0.01", "--seed", "5", "--out", "o.yaml"});
    REQUIRE(good.has_value());
    CHECK(good->Error.empty());
    CHECK(good->Path == "a.yaml");
    CHECK(*good->Ticks == 10);
    CHECK(*good->Dt == 0.01);
    CHECK(*good->Seed == 5);
    CHECK(good->Out == "o.yaml");

    CHECK_FALSE(Parse({"--simulate", "a.yaml", "--ticks", "x"})->Error.empty());
    CHECK_FALSE(Parse({"--simulate", "a.yaml", "--ticks", "-3"})->Error.empty());
    CHECK_FALSE(Parse({"--simulate", "a.yaml", "--ticks"})->Error.empty());
    CHECK_FALSE(Parse({"--simulate", "a.yaml", "--ticks", "1", "--seed", "0"})->Error.empty());

    CHECK_FALSE(good->Scripts);
    const auto scripts = Parse({"--simulate", "a.yaml", "--ticks", "1", "--scripts", "--scripts-dir", "s"});
    CHECK(scripts->Scripts);
    CHECK(scripts->ScriptsDir == "s");
}

TEST_CASE("Simulate runs a template deterministically and reports errors", "[simulate][cli]")
{
    RegisterComponents();
    const auto dir = std::filesystem::temp_directory_path() / "molehole_simulate_tests";
    std::filesystem::create_directories(dir);
    const auto scene = (std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "templates" / "dev.yaml").string();
    const auto first = dir / "first.yaml";
    const auto second = dir / "second.yaml";
    const auto zero = dir / "zero.yaml";

    REQUIRE(Simulate(scene, "30", first) == 0);
    REQUIRE(Simulate(scene, "30", second) == 0);
    CHECK(ReadFile(first) == ReadFile(second));
    CHECK_FALSE(ReadFile(first).empty());

    REQUIRE(Simulate(scene, "0", zero) == 0);
    Scene loaded;
    loaded.DeserializeFromYaml(ReadFile(scene));
    CHECK(ReadFile(zero) == loaded.SerializeToYaml());
    CHECK(ReadFile(zero) != ReadFile(first));

    CHECK(Simulate(scene, "1", first, {"--dt", "0"}) == 2);
    CHECK(Simulate((dir / "missing.yaml").string(), "1", first) == 1);
    CHECK(RunSimulate(*Parse({"--simulate", scene})) == 2);
    CHECK(RunSimulate(*Parse({"--simulate"})) == 2);
    CHECK(RunSimulate(*Parse({"--simulate", scene, "--ticks", "x"})) == 2);

    const auto bad = dir / "bad.yaml";
    std::ofstream(bad) << "Scene: Bad\nEntities:\n  - Guid: 1\n    Components:\n      Transform: {Position: oops}\n";
    CHECK(Simulate(bad.string(), "1", first) == 1);

    std::filesystem::remove_all(dir);
}

TEST_CASE("Simulate --scripts runs the scene's attached scripts", "[simulate][cli][scripting]")
{
    RegisterComponents();
    const auto dir = std::filesystem::temp_directory_path() / "molehole_simulate_scripts";
    std::filesystem::create_directories(dir / "scripts");
    std::ofstream(dir / "scripts" / "drift.luau") << "return { OnTick = function(self, dt)\n"
        "  local x, y, z = scene.get_xyz(self.entity, 'Transform', 'Position')\n"
        "  scene.set_xyz(self.entity, 'Transform', 'Position', x + 10 * dt, y, z)\n"
        "end }";
    const auto scene = dir / "scene.yaml";
    std::ofstream(scene) << "Scene: Drift\nSchemaVersion: 2\nEntities:\n  - Guid: 5\n    Components:\n"
        "      Metadata: {Guid: 5, Name: Drifter, TypeTag: Entity}\n"
        "      Transform: {Position: [0, 0, 0]}\n"
        "      Scripts:\n        - Name: drift\n";
    const auto plain = dir / "plain.yaml";
    const auto driven = dir / "driven.yaml";
    const std::vector<std::string> extra{"--scripts-dir", (dir / "scripts").string()};

    REQUIRE(Simulate(scene.string(), "60", plain) == 0);
    auto flags = extra;
    flags.push_back("--scripts");
    REQUIRE(Simulate(scene.string(), "60", driven, flags) == 0);
    CHECK(ReadFile(plain) != ReadFile(driven));

    Scene result;
    result.DeserializeFromYaml(ReadFile(driven));
    const auto position = result.Registry().get<TransformComponent>(result.FindByGuid(5)).Position;
    CHECK(position.x == Catch::Approx(10.0).margin(1e-2));

    std::ofstream(dir / "scripts" / "drift.luau", std::ios::trunc) << "return { OnTick = function() error('broken') end }";
    CHECK(Simulate(scene.string(), "2", driven, flags) == 1);
    std::filesystem::remove_all(dir);
}
