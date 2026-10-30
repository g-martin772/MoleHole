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
