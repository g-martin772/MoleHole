#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

TEST_CASE("Model showcase references existing models at a common display size", "[rendering][templates][gltf]")
{
    RegisterComponents();
    const auto root = std::filesystem::path(MOLEHOLE_SOURCE_DIR);
    std::ifstream stream(root / "templates" / "model-showcase.yaml");
    REQUIRE(stream);
    Scene scene;
    scene.DeserializeFromYaml({std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()});

    std::size_t meshes = 0;
    for (auto [entity, mesh, transform] : scene.Registry().view<const MeshComponent, const TransformComponent>().each())
    {
        INFO(mesh.AssetPath);
        CHECK(std::filesystem::exists(root / mesh.AssetPath));
        CHECK(mesh.MetresPerUnit > 0.0);
        CHECK(std::abs(transform.Position.y) < 100.0);
        ++meshes;
    }
    CHECK(meshes >= 17);
    CHECK(scene.FindExtension("EditorCamera") != nullptr);
    CHECK(scene.FindExtension("Render") != nullptr);
}
