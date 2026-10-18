#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;
using Catch::Matchers::WithinAbs;

TEST_CASE("Mesh instance records carry the material index and the inverse transpose", "[rendering][mesh-trace]")
{
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f))
                            * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.5f));
    const auto record = MakeMeshInstance(model, 7, 0x1000, 0x2000, 0x3000);
    CHECK(record.MaterialIndex == 7);
    CHECK(record.VertexAddress == 0x1000);
    CHECK(record.IndexAddress == 0x2000);
    CHECK(record.ExtraAddress == 0x3000);
    CHECK_THAT(record.NormalMatrix[0][0], WithinAbs(0.5f, 1e-6f));
    CHECK_THAT(record.NormalMatrix[2][2], WithinAbs(2.0f, 1e-6f));
    CHECK(record.NormalMatrix[3] == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

TEST_CASE("Mesh bounds merge to a sphere that contains both", "[rendering][mesh-trace]")
{
    const glm::vec4 a{0.0f, 0.0f, 0.0f, 1.0f};
    const glm::vec4 b{10.0f, 0.0f, 0.0f, 2.0f};
    CHECK(MergeBounds(glm::vec4(0.0f), a) == a);
    CHECK(MergeBounds(a, glm::vec4(0.0f)) == a);
    CHECK(MergeBounds(glm::vec4(0.0f, 0.0f, 0.0f, 5.0f), a) == glm::vec4(0.0f, 0.0f, 0.0f, 5.0f));
    const auto merged = MergeBounds(a, b);
    CHECK_THAT(merged.w, WithinAbs(6.5f, 1e-5f));
    CHECK_THAT(merged.x, WithinAbs(5.5f, 1e-5f));

    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 5.0f, 6.0f))
                            * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 3.0f, 2.0f));
    const auto bounds = TransformBounds(model, 2.0f);
    CHECK(glm::vec3(bounds) == glm::vec3(4.0f, 5.0f, 6.0f));
    CHECK_THAT(bounds.w, WithinAbs(6.0f, 1e-5f));
}

TEST_CASE("Transparent materials are hidden from shadow rays and need a per-hit decision", "[rendering][mesh-trace]")
{
    GPP::GltfMaterial opaque;
    opaque.DoubleSided = true;
    GPP::GltfMaterial singleSided = opaque;
    singleSided.DoubleSided = false;
    GPP::GltfMaterial masked = opaque;
    masked.AlphaMode = GPP::GltfAlphaMode::Mask;
    GPP::GltfMaterial blended = opaque;
    blended.AlphaMode = GPP::GltfAlphaMode::Blend;

    const auto pack = [](const GPP::GltfMaterial& m)
    {
        return GPP::PackGltfMaterial(m, [](const std::shared_ptr<GPP::VulkanImage>&) { return -1; });
    };
    CHECK(MeshInstanceMask(pack(opaque)) == kMeshMaskOpaque);
    CHECK(MeshInstanceMask(pack(singleSided)) == kMeshMaskOpaque);
    CHECK(MeshInstanceMask(pack(masked)) == kMeshMaskTransparent);
    CHECK(MeshInstanceMask(pack(blended)) == kMeshMaskTransparent);
    CHECK_FALSE(MeshInstanceNeedsHitTest(pack(opaque)));
    CHECK(MeshInstanceNeedsHitTest(pack(singleSided)));
    CHECK(MeshInstanceNeedsHitTest(pack(masked)));
    CHECK(MeshInstanceNeedsHitTest(pack(blended)));
}

TEST_CASE("Material indices offset by the scene base and fall back to material 0", "[rendering][mesh-trace]")
{
    CHECK(MeshMaterialTable::MaterialIndex(5, 0) == 5);
    CHECK(MeshMaterialTable::MaterialIndex(5, 3) == 8);
    CHECK(MeshMaterialTable::MaterialIndex(5, -1) == 0);
}

TEST_CASE("Two scenes share one material table with offset indices and a shared texture table", "[rendering][mesh-trace]")
{
    GPP::GltfSceneData a;
    GPP::GltfSceneData b;
    GPP::GltfMaterial plain;
    a.Materials = {plain, plain};
    b.Materials = {plain, plain, plain};
    GPP::GltfMaterialLibrary library(GPP::kGltfFallbackTextureCapacity);
    library.AddMaterial(plain);
    const auto baseA = library.Add(a);
    const auto baseB = library.Add(b);
    CHECK(baseA == 1);
    CHECK(baseB == 3);
    CHECK(library.Add(a) == baseA);
    CHECK(library.Materials().size() == 6);
    CHECK(GPP::GltfTextureCapacity(GPP::GpuCapabilities{.DescriptorIndexing = true}) == GPP::kGltfIndexedTextureCapacity);
    CHECK(GPP::GltfTextureCapacity(GPP::GpuCapabilities{}) == GPP::kGltfFallbackTextureCapacity);
}

TEST_CASE("The mesh shader blocks mirror the C++ records", "[rendering][mesh-trace]")
{
    std::ifstream file(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "shaders" / "mesh_trace.glsl");
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(text.find(std::format("MESH_VERTEX_FLOATS = {};", sizeof(GPP::GltfVertex) / sizeof(float))) != std::string::npos);
    CHECK(text.find(std::format("MESH_EXTRA_FLOATS = {};", sizeof(GPP::GltfVertexExtra) / sizeof(float))) != std::string::npos);
    CHECK(sizeof(MeshInstanceGpu) == 160);
    CHECK(sizeof(MeshPushConstantsGpu) == 80);
    CHECK(sizeof(MeshLightingParamsGpu) == 32);
}
