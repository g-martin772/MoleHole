#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

TEST_CASE("Luau script tests under tests/scripts pass", "[scripting][script-tests]")
{
    const std::filesystem::path source(MOLEHOLE_SOURCE_DIR);
    const AssetDirectories assets(MakeScriptAssetOptions(source / "scripts"));
    auto options = MakeScriptTestOptions(assets);
    options.DisplayRoot = source;

    const auto files = FindScriptTests(source / "tests" / "scripts");
    REQUIRE_FALSE(files.empty());
    for (const auto& file : RunScriptTests(files, options).Files)
    {
        INFO(file.Path);
        CHECK(file.LoadError.empty());
        CHECK_FALSE(file.Tests.empty());
        for (const auto& test : file.Tests)
        {
            INFO(test.Name);
            INFO(test.Message);
            CHECK(test.Passed);
        }
    }
}

TEST_CASE("The test-scripts flag parses its paths and options", "[scripting][script-tests][cli]")
{
    const auto parse = [](std::vector<std::string> args)
    {
        args.insert(args.begin(), "molehole");
        std::vector<char*> argv;
        for (auto& arg : args) argv.push_back(arg.data());
        return ParseScriptTestArgs(static_cast<int>(argv.size()), argv.data());
    };

    CHECK_FALSE(parse({"--simulate", "a.yaml"}).has_value());
    const auto bare = parse({"--test-scripts"});
    REQUIRE(bare.has_value());
    CHECK(bare->Paths.empty());
    CHECK(bare->ScriptsDir == "scripts");

    const auto full = parse({"--test-scripts", "a.luau", "dir", "--verbose", "--seed", "9", "--scripts-dir", "other"});
    REQUIRE(full.has_value());
    CHECK(full->Paths == std::vector<std::string>{"a.luau", "dir"});
    CHECK(full->Verbose);
    CHECK(*full->Seed == 9);
    CHECK(full->ScriptsDir == "other");
    CHECK(full->Error.empty());
    CHECK_FALSE(parse({"--test-scripts", "--seed", "0"})->Error.empty());
    CHECK_FALSE(parse({"--test-scripts", "--scripts-dir"})->Error.empty());
}

TEST_CASE("The test-scripts runner exits non-zero for a failing file", "[scripting][script-tests][cli]")
{
    const auto dir = std::filesystem::temp_directory_path() / "molehole_script_tests_cli";
    std::filesystem::create_directories(dir);
    const auto passing = dir / "passing.luau";
    const auto failing = dir / "failing.luau";
    std::ofstream(passing) << "test('ok', function() assert_eq(1, 1) end)";
    std::ofstream(failing) << "test('bad', function()\n  assert_eq(1, 2)\nend)";

    const auto run = [&](const std::filesystem::path& file)
    {
        ScriptTestArgs args;
        args.ScriptsDir = (std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "scripts").string();
        args.Paths = {file.string()};
        return RunScriptTestsCli(args);
    };
    CHECK(run(passing) == 0);
    CHECK(run(failing) == 1);
    CHECK(run(dir / "missing.luau") == 2);
    std::filesystem::remove_all(dir);
}
