#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;

namespace
{
    std::vector<CreateEntry> Registered()
    {
        RegisterComponents();
        return SupportedCreateEntries(BuiltinCreateEntries(), [](const std::string& name)
        {
            return GPP::ComponentRegistry::Instance().FindByName(name) != nullptr;
        });
    }

    const CreateEntry& Find(const std::vector<CreateEntry>& entries, const std::string_view name)
    {
        const auto it = std::ranges::find(entries, name, &CreateEntry::Name);
        REQUIRE(it != entries.end());
        return *it;
    }

    std::uint64_t Counter()
    {
        static std::uint64_t next = 100;
        return next++;
    }

    std::string YamlOf(const GPP::Command& command) { return std::get<GPP::SpawnEntityCommand>(command).Yaml; }
}

TEST_CASE("Entries have unique ids, tooltips and icons", "[ui][create]")
{
    const auto entries = Registered();
    REQUIRE_FALSE(entries.empty());
    std::set<std::string> ids;
    for (const auto& entry : entries)
    {
        CHECK(ids.insert(entry.Id).second);
        CHECK_FALSE(entry.Tooltip.empty());
        CHECK(entry.Icon != 0);
    }
    for (const auto name : {"Star", "Planet", "Moon", "Asteroid", "Schwarzschild", "Camera", "Point Light", "Empty"})
    {
        CHECK(std::ranges::contains(entries, name, &CreateEntry::Name));
    }
}

TEST_CASE("The tree nests folders and lists folders before entries", "[ui][create]")
{
    const auto entries = Registered();
    const auto tree = BuildCreateTree(entries);
    const auto top = std::ranges::find(tree.Children, "Bodies", &CreateNode::Name);
    REQUIRE(top != tree.Children.end());
    REQUIRE(top->IsFolder());
    const auto nested = std::ranges::find(top->Children, "Moons and Asteroids", &CreateNode::Name);
    REQUIRE(nested != top->Children.end());
    CHECK(nested->IsFolder());
    CHECK(nested->Path == "Bodies/Moons and Asteroids");
    CHECK(top->Children.front().IsFolder());
    CHECK_FALSE(top->Children.back().IsFolder());
    CHECK(tree.Children.back().Name == "Empty");
    CHECK_FALSE(tree.Children.back().IsFolder());
}

TEST_CASE("Search ranks by name, then folder and keywords", "[ui][create]")
{
    const auto entries = Registered();
    const auto planet = FilterCreateEntries(entries, "plan");
    REQUIRE_FALSE(planet.empty());
    CHECK(entries[planet.front()].Name == "Planet");

    const auto sgr = FilterCreateEntries(entries, "sgr");
    REQUIRE_FALSE(sgr.empty());
    CHECK(entries[sgr.front()].Name == "Sagittarius A*");

    CHECK(FilterCreateEntries(entries, "").size() == entries.size());
    CHECK(FilterCreateEntries(entries, "zzzzqq").empty());
}

TEST_CASE("Entries needing an unregistered component are hidden", "[ui][create]")
{
    auto entries = BuiltinCreateEntries();
    const auto all = entries.size();
    const auto none = SupportedCreateEntries(entries, [](const std::string&) { return false; });
    CHECK(none.size() < all);
    CHECK(std::ranges::none_of(none, [](const CreateEntry& entry) { return !entry.Requires.empty(); }));
}

TEST_CASE("Names are made unique per scene", "[ui][create]")
{
    CHECK(UniqueEntityName("Planet", {}) == "Planet");
    CHECK(UniqueEntityName("Planet", {"Planet"}) == "Planet 2");
    CHECK(UniqueEntityName("Planet", {"Planet", "Planet 2", "Planet 3"}) == "Planet 4");
    CHECK(UniqueEntityName("Planet", {"Planet 2"}) == "Planet");
}

TEST_CASE("Placement distance follows the body size and falls back to the camera speed", "[ui][create]")
{
    CHECK(CreateDistanceM(1000.0, 5.0) == 6000.0);
    CHECK(CreateDistanceM(0.0, 500.0) == 1000.0);
    CHECK(CreateDistanceM(0.0, 0.0) == 1.0);
}

TEST_CASE("Creating an entry spawns named entities in front of the view", "[ui][create]")
{
    const auto entries = Registered();
    GPP::Scene scene("Create");
    const CreatePlacement placement{glm::dvec3(10.0, 0.0, 0.0), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f), 100.0, std::nullopt};

    const auto& planet = Find(entries, "Planet");
    const auto first = BuildCreateCommands(scene, planet, placement, Counter);
    REQUIRE(first);
    REQUIRE(first->Commands.size() == 1);
    CHECK(first->Label == "Create Planet");
    CHECK(YamlOf(first->Commands.front()).find("Planet") != std::string::npos);

    GPP::Scene applied("Applied");
    REQUIRE(GPP::ApplyCommand(applied, first->Commands.front()));
    const auto second = BuildCreateCommands(applied, planet, placement, Counter);
    REQUIRE(second);
    CHECK(YamlOf(second->Commands.front()).find("Planet 2") != std::string::npos);

    const auto entity = applied.FindByGuid(first->Guids.front());
    REQUIRE(applied.IsValid(entity));
    const auto& transform = applied.Registry().get<GPP::TransformComponent>(entity);
    CHECK(transform.Position.x == 10.0);
    CHECK(transform.Position.z == -CreateDistanceM(planet.SizeM, 100.0));
    CHECK(applied.Registry().all_of<SphereComponent, NBodyComponent>(entity));
}

TEST_CASE("A multi-part entry is one batch and the first camera becomes primary", "[ui][create]")
{
    const auto entries = Registered();
    GPP::Scene scene("Batch");
    const CreatePlacement placement{};

    const auto binary = BuildCreateCommands(scene, Find(entries, "Binary Pair"), placement, Counter);
    REQUIRE(binary);
    CHECK(binary->Commands.size() == 2);
    CHECK(binary->Guids.size() == 2);
    CHECK(YamlOf(binary->Commands[0]).find("Binary A") != std::string::npos);
    CHECK(YamlOf(binary->Commands[1]).find("Binary B") != std::string::npos);

    const auto camera = BuildCreateCommands(scene, Find(entries, "Camera"), placement, Counter);
    REQUIRE(camera);
    CHECK(YamlOf(camera->Commands.front()).find("Primary: true") != std::string::npos);
    GPP::Scene withCamera("WithCamera");
    REQUIRE(GPP::ApplyCommand(withCamera, camera->Commands.front()));
    const auto next = BuildCreateCommands(withCamera, Find(entries, "Camera"), placement, Counter);
    REQUIRE(next);
    CHECK(YamlOf(next->Commands.front()).find("Primary: false") != std::string::npos);
}

TEST_CASE("File entries leave the file dialog to the caller", "[ui][create]")
{
    const auto entries = Registered();
    GPP::Scene scene("Mesh");
    CHECK_FALSE(BuildCreateCommands(scene, Find(entries, "From File..."), CreatePlacement{}, Counter));
}

TEST_CASE("Mesh entries nest by directory and name scene files after their folder", "[ui][create]")
{
    std::string name;
    CHECK(MeshEntryFolder("assets/models/star_trek/scene.gltf", name) == "Meshes");
    CHECK(name == "star_trek");
    CHECK(MeshEntryFolder("assets/models/ships/enterprise/scene.gltf", name) == "Meshes/ships");
    CHECK(name == "enterprise");
    CHECK(MeshEntryFolder("assets/models/ships/borg.glb", name) == "Meshes/ships");
    CHECK(name == "borg");
    CHECK(MeshEntryFolder("/home/user/models/thing.gltf", name) == "Meshes/External");
    CHECK(name == "thing");

    const std::vector<std::string> paths{"assets/models/a/scene.gltf", "assets/models/dir/b.glb"};
    const auto entries = MeshCreateEntries(paths);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].Name == "a");
    CHECK(entries[1].Folder == "Meshes/dir");
    CHECK(entries[1].Id == "Mesh:assets/models/dir/b.glb");
}

TEST_CASE("Script entries carry a Scripts component", "[ui][create]")
{
    RegisterComponents();
    const std::vector<std::string> names{"spring_arm_camera"};
    const auto entries = ScriptCreateEntries(names);
    REQUIRE(entries.size() == 1);
    GPP::Scene scene("Scripts");
    const auto built = BuildCreateCommands(scene, entries.front(), CreatePlacement{}, Counter);
    REQUIRE(built);
    CHECK(YamlOf(built->Commands.front()).find("spring_arm_camera") != std::string::npos);
}
