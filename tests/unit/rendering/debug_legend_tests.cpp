#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <common/PxRenderBuffer.h>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    std::string ReadSource(const std::string& relative)
    {
        std::ifstream stream(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / relative);
        REQUIRE(stream.good());
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::string ReadShaders()
    {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::directory_iterator(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "shaders"))
        {
            const auto extension = entry.path().extension();
            if (extension == ".glsl" || extension == ".comp") files.push_back(entry.path());
        }
        std::ranges::sort(files);
        std::string all;
        for (const auto& file : files)
        {
            std::ifstream stream(file);
            all.append(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
            all += '\n';
        }
        return all;
    }

    LegendColor ParseTriple(const std::string& text)
    {
        static const std::regex number(R"([-+]?[0-9]*\.?[0-9]+)");
        std::array<float, 3> values{};
        std::size_t count = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), number); it != std::sregex_iterator() && count < 3; ++it)
        {
            values[count++] = std::stof(it->str());
        }
        REQUIRE(count == 3);
        return {values[0], values[1], values[2]};
    }

    // "name{a, b, c}" or "vec3(a, b, c)" following `name`
    LegendColor ColorAfter(const std::string& source, const std::string& name, const char open, const char close)
    {
        const auto at = source.find(name + open);
        REQUIRE(at != std::string::npos);
        const auto begin = at + name.size() + 1;
        const auto end = source.find(close, begin);
        REQUIRE(end != std::string::npos);
        return ParseTriple(source.substr(begin, end - begin));
    }

    struct ShaderTermination
    {
        std::string Id;
        LegendColor Color;
    };

    std::vector<ShaderTermination> ParseTerminationColors(const std::string& shader)
    {
        const auto start = shader.find("if (u_debugMode == 2) {");
        REQUIRE(start != std::string::npos);
        const auto end = shader.find("return shaded;", start);
        REQUIRE(end != std::string::npos);

        std::vector<ShaderTermination> out;
        std::istringstream block(shader.substr(start, end - start));
        std::string line;
        static const std::regex term(R"(TERM_[A-Z]+)");
        while (std::getline(block, line))
        {
            const auto ret = line.find("return vec3(");
            if (ret == std::string::npos) continue;
            std::smatch match;
            const std::string id = std::regex_search(line, match, term) ? match.str() : "TERM_CROSSHAIR";
            const auto open = ret + std::string("return vec3(").size();
            out.push_back({id, ParseTriple(line.substr(open, line.find(')', open) - open))});
        }
        return out;
    }
}

TEST_CASE("The termination legend lists the colours debugViewColor returns", "[rendering][legend]")
{
    const auto shader = ReadShaders();
    const auto parsed = ParseTerminationColors(shader);
    REQUIRE(parsed.size() == kTerminationLegend.size());
    for (std::size_t i = 0; i < parsed.size(); ++i)
    {
        INFO(parsed[i].Id);
        CHECK(kTerminationLegend[i].Id == parsed[i].Id);
        CHECK(kTerminationLegend[i].Color.R == Approx(parsed[i].Color.R));
        CHECK(kTerminationLegend[i].Color.G == Approx(parsed[i].Color.G));
        CHECK(kTerminationLegend[i].Color.B == Approx(parsed[i].Color.B));
    }
}

TEST_CASE("Every termination reason of the shader has a legend entry", "[rendering][legend]")
{
    const auto shader = ReadShaders();
    std::set<std::string> declared;
    static const std::regex declaration(R"(const int (TERM_[A-Z]+) =)");
    for (auto it = std::sregex_iterator(shader.begin(), shader.end(), declaration); it != std::sregex_iterator(); ++it)
    {
        declared.insert((*it)[1].str());
    }
    std::set<std::string> listed;
    for (const auto& entry : kTerminationLegend) listed.insert(std::string(entry.Id));
    CHECK(declared == listed);
}

TEST_CASE("DebugHeat reproduces debugHeat of the shader", "[rendering][legend]")
{
    const auto shader = ReadShaders();
    CHECK(shader.contains("clamp(vec3(1.5 * t - 0.25, 1.5 - abs(3.0 * t - 1.5), 1.25 - 1.5 * t), 0.0, 1.0)"));
    CHECK(shader.contains("debugHeat(sqrt(float(g_stepCount) / float(max(g_maxStepCount, 1))))"));

    const auto cold = DebugHeat(0.0f);
    CHECK(cold.R == Approx(0.0f));
    CHECK(cold.G == Approx(0.0f));
    CHECK(cold.B == Approx(1.0f));

    const auto middle = DebugHeat(0.5f);
    CHECK(middle.R == Approx(0.5f));
    CHECK(middle.G == Approx(1.0f));
    CHECK(middle.B == Approx(0.5f));

    const auto hot = DebugHeat(1.0f);
    CHECK(hot.R == Approx(1.0f));
    CHECK(hot.G == Approx(0.0f));
    CHECK(hot.B == Approx(0.0f));

    CHECK(DebugHeat(-3.0f) == DebugHeat(0.0f));
    CHECK(DebugHeat(7.0f) == DebugHeat(1.0f));
}

TEST_CASE("Overlay legend colours match the render pipeline", "[rendering][legend]")
{
    const auto pipeline = ReadSource("src/rendering/render_pipeline.cpp");
    const auto expect = [&](const std::span<const LegendEntry> entries, const std::string_view id, const char open,
                            const char close)
    {
        const auto it = std::ranges::find(entries, id, &LegendEntry::Id);
        REQUIRE(it != entries.end());
        const auto color = ColorAfter(pipeline, std::string(id), open, close);
        INFO(id);
        CHECK(it->Color.R == Approx(color.R));
        CHECK(it->Color.G == Approx(color.G));
        CHECK(it->Color.B == Approx(color.B));
    };
    expect(kGravityGridLegend, "kGravityGridColor", '{', '}');
    expect(kObjectPathLegend, "kMeshColor", '{', '}');
    expect(kObjectPathLegend, "kSphereColor", '{', '}');
}

TEST_CASE("Overlay legends point at boolean render settings", "[rendering][legend]")
{
    const auto& registry = RenderSettingRegistry();
    for (const auto& legend : kOverlayLegends)
    {
        INFO(legend.SettingKey);
        const auto* setting = registry.Find(legend.SettingKey);
        REQUIRE(setting != nullptr);
        CHECK(setting->Meta.Group == "Overlays");
        CHECK(setting->Type == FieldType::Bool);
    }
    for (const auto* setting : registry.InGroup("Overlays"))
    {
        INFO(setting->Key);
        CHECK(std::ranges::any_of(kOverlayLegends, [&](const OverlayLegend& legend) { return legend.SettingKey == setting->Key; }));
    }
}

TEST_CASE("Collider legend colours are PhysX debug colours", "[rendering][legend]")
{
    const auto channels = [](const physx::PxU32 argb)
    {
        return LegendColor{static_cast<float>((argb >> 16) & 0xFF) / 255.0f, static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
                           static_cast<float>(argb & 0xFF) / 255.0f};
    };
    const std::map<std::string_view, physx::PxU32> known{
        {"eARGB_MAGENTA", physx::PxDebugColor::eARGB_MAGENTA}, {"eARGB_YELLOW", physx::PxDebugColor::eARGB_YELLOW},
        {"eARGB_RED", physx::PxDebugColor::eARGB_RED},         {"eARGB_GREEN", physx::PxDebugColor::eARGB_GREEN},
        {"eARGB_BLUE", physx::PxDebugColor::eARGB_BLUE},       {"eARGB_WHITE", physx::PxDebugColor::eARGB_WHITE}};
    for (const auto& entry : kColliderLegend)
    {
        INFO(entry.Id);
        const auto it = known.find(entry.Id);
        REQUIRE(it != known.end());
        CHECK(entry.Color == channels(it->second));
    }
}
