#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;

namespace
{
    const std::filesystem::path kRoot(MOLEHOLE_SOURCE_DIR);

    ShaderCompileOptions Options()
    {
        ShaderCompileOptions options;
        options.enableCache = false;
        options.includeDirectories = {kRoot / "shaders", kRoot / "dependencies/gpp/shaders/pbr"};
        return options;
    }

    CompiledShader Compile(const std::string& file, const ShaderStage stage,
                           const GpuCapabilities& caps, const std::uint32_t capacity, std::unordered_map<std::string, std::string> extra = {})
    {
        SetGlobalShaderDefines(Units::ShaderDefines());
        auto defines = PbrShaderDefines(caps, capacity);
        for (auto& [key, value] : extra) defines[key] = value;
        ShaderCompiler compiler(std::make_shared<FileSystem>(), std::make_shared<Logger>());
        return compiler.Compile(ShaderSource{.path = kRoot / "shaders" / file, .stage = stage, .defines = std::move(defines)},
                                Options());
    }

    GpuCapabilities Caps(const bool indexing)
    {
        GpuCapabilities caps;
        caps.DescriptorIndexing = indexing;
        caps.RayQuery = true;
        caps.AccelerationStructure = true;
        return caps;
    }

    bool HasBinding(const CompiledShader& shader, const std::uint32_t binding, const std::uint32_t count = 0)
    {
        return std::ranges::any_of(shader.reflection.descriptorBindings, [&](const auto& b)
        {
            return b.set == 0 && b.binding == binding && (count == 0 || b.descriptorCount == count);
        });
    }
}

TEST_CASE("The raster mesh shaders compile with and without descriptor indexing and shadow rays", "[rendering][shaders]")
{
    for (const bool indexing : {true, false})
    {
        for (const bool shadows : {true, false})
        {
            const auto capacity = GltfTextureCapacity(Caps(indexing));
            std::unordered_map<std::string, std::string> extra;
            if (shadows) extra.emplace("MESH_SHADOWS", "1");
            INFO("indexing " << indexing << " shadows " << shadows);
            const auto vertex = Compile("mesh.vert", ShaderStage::Vertex, Caps(indexing), capacity, extra);
            const auto fragment = Compile("mesh.frag", ShaderStage::Fragment, Caps(indexing), capacity, extra);
            CHECK_FALSE(vertex.spirv.empty());
            REQUIRE_FALSE(fragment.spirv.empty());
            CHECK(HasBinding(fragment, 1));
            CHECK(HasBinding(fragment, 3, capacity));
            CHECK(HasBinding(fragment, 4));
            CHECK(HasBinding(fragment, 5));
            CHECK(HasBinding(fragment, 7));
            CHECK(HasBinding(fragment, 6) == shadows);
        }
    }
}

TEST_CASE("The traced black hole kernel with meshes compiles in both precisions", "[rendering][shaders]")
{
    const auto caps = Caps(true);
    for (const bool f64 : {false, true})
    {
        std::unordered_map<std::string, std::string> extra{{"GPP_MESH_TRACE", "1"}};
        if (f64) extra.emplace("GPP_PRECISION_F64", "1");
        const auto shader = Compile("black_hole_rendering.comp", ShaderStage::Compute, caps, GltfTextureCapacity(caps), extra);
        REQUIRE_FALSE(shader.spirv.empty());
        for (const std::uint32_t binding : {6u, 7u, 9u, 10u, 11u, 12u}) CHECK(HasBinding(shader, binding));
        CHECK(HasBinding(shader, 8, kGltfIndexedTextureCapacity));
    }
}

TEST_CASE("The exposure shader compiles", "[rendering][shaders]")
{
    const auto shader = Compile("exposure.comp", ShaderStage::Compute, Caps(true), 32);
    CHECK_FALSE(shader.spirv.empty());
}
