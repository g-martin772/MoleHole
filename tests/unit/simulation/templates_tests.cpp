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
