#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;
using Catch::Matchers::WithinAbs;

TEST_CASE("Mesh instance records carry the material and the inverse transpose", "[rendering][mesh-trace]")
{
    const glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f))
                            * glm::scale(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 0.5f));
    const auto plain = MakeMeshInstance(model, nullptr, -1, 0x1000, 0x2000);
    CHECK(plain.BaseColorFactor == glm::vec4(0.8f, 0.8f, 0.8f, 1.0f));
    CHECK(plain.MetallicFactor == 0.5f);
    CHECK(plain.TextureSlot == -1);
    CHECK(plain.VertexAddress == 0x1000);
    CHECK(plain.IndexAddress == 0x2000);
    CHECK_THAT(plain.NormalMatrix[0][0], WithinAbs(0.5f, 1e-6f));
    CHECK_THAT(plain.NormalMatrix[2][2], WithinAbs(2.0f, 1e-6f));
    CHECK(plain.NormalMatrix[3] == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    GPP::GltfMaterial material;
    material.BaseColorFactor = {0.1f, 0.2f, 0.3f, 1.0f};
    material.MetallicFactor = 0.9f;
    material.RoughnessFactor = 0.2f;
    const auto textured = MakeMeshInstance(model, &material, 3, 0, 0);
    CHECK(textured.BaseColorFactor == material.BaseColorFactor);
    CHECK(textured.MetallicFactor == 0.9f);
    CHECK(textured.RoughnessFactor == 0.2f);
    CHECK(textured.TextureSlot == 3);
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

TEST_CASE("Mesh textures get one slot each until the array is full", "[rendering][mesh-trace]")
{
    MeshTextureSlots slots;
    std::array<int, kMeshTextureSlots + 1> fake{};
    const auto image = [&](std::size_t i) { return reinterpret_cast<const GPP::VulkanImage*>(&fake[i]); };
    CHECK(slots.Assign(nullptr) == -1);
    CHECK(slots.Assign(image(0)) == 0);
    CHECK(slots.Assign(image(1)) == 1);
    CHECK(slots.Assign(image(0)) == 0);
    for (std::size_t i = 2; i < kMeshTextureSlots; ++i) CHECK(slots.Assign(image(i)) == static_cast<int>(i));
    CHECK_FALSE(slots.Overflowed());
    CHECK(slots.Assign(image(kMeshTextureSlots)) == -1);
    CHECK(slots.Overflowed());
    slots.Clear();
    CHECK(slots.Count() == 0);
    CHECK(slots.Assign(image(5)) == 0);
}

TEST_CASE("The mesh shader block mirrors the C++ record", "[rendering][mesh-trace]")
{
    std::ifstream file(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "shaders" / "mesh_trace.glsl");
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(text.find(std::format("MESH_TEXTURE_SLOTS = {};", kMeshTextureSlots)) != std::string::npos);
    CHECK(text.find(std::format("MESH_VERTEX_FLOATS = {};", sizeof(GPP::GltfVertex) / sizeof(float))) != std::string::npos);
}
