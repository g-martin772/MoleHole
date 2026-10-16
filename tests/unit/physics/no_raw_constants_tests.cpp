#include <catch2/catch_test_macros.hpp>

import std;

namespace fs = std::filesystem;

namespace
{
    // Leading digits of physical constants that must only appear in gpp/src/core/units/units.cppm.
    constexpr std::array<std::string_view, 14> kForbidden{
        "2.99792458", "299792458", "6.6743", "6.67430", "6.674", "1.98840", "1.9884", "1.989e30", "5.670374",
        "1.380649", "6.62607015", "8.854187", "1476.6", "3.0856775"};

    bool IsSourceFile(const fs::path& path)
    {
        const auto ext = path.extension().string();
        return ext == ".cpp" || ext == ".cppm" || ext == ".glsl" || ext == ".comp" || ext == ".frag" || ext == ".vert"
               || ext == ".luau";
    }

    std::vector<std::string> Offenders(const fs::path& root)
    {
        std::vector<std::string> found;
        if (!fs::exists(root)) return found;
        for (const auto& entry : fs::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file() || !IsSourceFile(entry.path())) continue;
            if (entry.path().filename() == "units.cppm") continue;
            std::ifstream stream(entry.path());
            std::string line;
            int number = 0;
            while (std::getline(stream, line))
            {
                ++number;
                for (const auto pattern : kForbidden)
                {
                    if (line.find(pattern) != std::string::npos)
                    {
                        found.push_back(std::format("{}:{}: {}", entry.path().string(), number, pattern));
                    }
                }
            }
        }
        return found;
    }
}

TEST_CASE("Physical constants live only in the units header", "[units][lint]")
{
    const fs::path root(MOLEHOLE_SOURCE_DIR);
    std::vector<std::string> offenders;
    for (const char* directory : {"src", "plugins", "shaders", "scripts", "dependencies/gpp/src"})
    {
        const auto found = Offenders(root / directory);
        offenders.insert(offenders.end(), found.begin(), found.end());
    }
    for (const auto& offender : offenders) FAIL_CHECK(offender);
    CHECK(offenders.empty());
}

TEST_CASE("Shaders receive no physical literals except through defines", "[units][lint]")
{
    const fs::path shaders = fs::path(MOLEHOLE_SOURCE_DIR) / "shaders";
    std::ifstream physics(shaders / "physics.glsl");
    const std::string text{std::istreambuf_iterator<char>(physics), std::istreambuf_iterator<char>()};
    CHECK(text.find("GPP_STEFAN_BOLTZMANN") != std::string::npos);
}
