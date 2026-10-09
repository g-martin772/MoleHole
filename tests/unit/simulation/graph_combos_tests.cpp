#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;
using Catch::Approx;
using GPP::Scene;

namespace
{
    std::vector<ComboSource> BuiltInSources()
    {
        std::vector<ComboSource> sources;
        const std::filesystem::path dir = std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "combos";
        for (const auto& file : std::filesystem::directory_iterator(dir))
        {
            if (file.path().extension() != ".yaml") continue;
            std::ifstream in(file.path());
            std::stringstream text;
            text << in.rdbuf();
            sources.push_back({file.path().stem().string(), text.str()});
        }
        return sources;
    }

    const NodeEntry& Builtin(const ComboLibrary& library, const std::string& name)
    {
        const auto it = std::ranges::find(library.Entries(), name, &NodeEntry::Name);
        REQUIRE(it != library.Entries().end());
        return *it;
    }

    struct World
    {
        Scene scene{"ComboWorld"};
        std::uint64_t Target{0};
        std::uint64_t Camera{0};

        World()
        {
            RegisterComponents();
            const auto target = scene.CreateEntity("Target");
            scene.Registry().emplace_or_replace<GPP::TransformComponent>(target);
            scene.Registry().emplace<BlackHoleComponent>(target, BlackHoleComponent{.Mass = 0.0f});
            Target = scene.GuidOf(target);
            const auto camera = scene.CreateEntity("Camera");
            scene.Registry().emplace_or_replace<GPP::TransformComponent>(camera);
            Camera = scene.GuidOf(camera);
        }

        GPP::TransformComponent& Transform(const std::uint64_t guid)
        {
            return scene.Registry().get<GPP::TransformComponent>(scene.FindByGuid(guid));
        }
    };

    void Apply(PendingWrites writes, Scene& scene)
    {
        for (auto& write : writes) write(scene);
    }

    void Tick(GraphExecutor& executor, Scene& scene, const float dt)
    {
        Apply(executor.ExecuteTickEvent(scene, dt), scene);
    }
}

TEST_CASE("Every shipped combo loads and spawns", "[graph][combo]")
{
    RegisterComponents();
    ComboLibrary library;
    const auto sources = BuiltInSources();
    library.Load(sources);
    for (const auto& error : library.Errors()) FAIL_CHECK(error);
    CHECK(library.Entries().size() == sources.size());
    for (const char* name : {"Move Object To", "Orbit Camera Around", "Spawn Prefab At", "Fade Property", "On Start Set Property"})
    {
        const auto& entry = Builtin(library, name);
        AnimationGraphData graph;
        const auto ids = entry.Spawn(graph);
        CHECK_FALSE(ids.empty());
    }
}

TEST_CASE("Broken combo templates are reported instead of loaded", "[graph][combo]")
{
    RegisterComponents();
    ComboLibrary library;
    library.Load({
        {"garbage", "just: [text"},
        {"nokind", "Name: A\nNodes:\n  - {Key: a, Kind: Nope}\n"},
        {"badlink", "Name: B\nNodes:\n  - {Key: a, Kind: Start}\nLinks:\n  - [a.Out, a.Missing]\n"},
        {"good", "Name: C\nNodes:\n  - {Key: a, Kind: Start}\n"},
    });
    CHECK(library.Errors().size() == 3);
    REQUIRE(library.Entries().size() == 1);
    CHECK(library.Entries()[0].Name == "C");
}

TEST_CASE("Spawning a combo groups the nodes in a comment and puts the entry first", "[graph][combo]")
{
    RegisterComponents();
    ComboLibrary library;
    library.Load(BuiltInSources());
    const auto& entry = Builtin(library, "Spawn Prefab At");

    std::string error;
    const auto combo = ParseCombo("Name: Pair\nNodes:\n  - {Key: s, Kind: Start, Pos: [0, 0]}\n  - {Key: p, Kind: Print, Pos: [300, 0]}\nLinks:\n  - [s.Out, p.In]\n", error);
    REQUIRE(combo);
    const auto registry = BuildNodeRegistry();
    AnimationGraphData graph;
    const auto result = InstantiateCombo(*combo, graph, registry);
    REQUIRE(result.Ok);
    REQUIRE(result.Ids.size() == 3);
    CHECK(graph.FindNode(result.Ids[0])->SubType == NodeSubType::Start);
    REQUIRE(graph.Comments.size() == 1);
    CHECK(graph.Comments[0].Title == "Pair");
    CHECK(graph.Comments[0].Id == result.Ids.back());
    for (const auto& node : graph.Nodes)
    {
        CHECK(CommentContains(graph.Comments[0], {node.Id, node.Position, EstimateNodeSize(node)}));
    }
    CHECK(graph.Links.size() == 1);

    AnimationGraphData target;
    target.Nodes.push_back(CreateStartEventNode(target.AllocateId()));
    const auto ids = entry.Spawn(target);
    REQUIRE_FALSE(ids.empty());
    CHECK(target.Comments.empty());
    CHECK(ConnectNewNode(target, ids.front(), target.Nodes[0].Outputs[0].Id));
    CHECK(target.Links.size() == 4);
}

TEST_CASE("Combo variables are uniquified per instance", "[graph][combo]")
{
    RegisterComponents();
    ComboLibrary library;
    library.Load(BuiltInSources());
    const auto& entry = Builtin(library, "Fade Property");
    AnimationGraphData graph;
    entry.Spawn(graph);
    entry.Spawn(graph);
    REQUIRE(graph.Variables.size() == 2);
    CHECK(graph.Variables[0].Name == "Elapsed");
    CHECK(graph.Variables[1].Name == "Elapsed 2");
    const auto uses = [&](const std::string& name)
    {
        return std::ranges::count_if(graph.Nodes, [&](const Node& n) { return n.VariableName == name; });
    };
    CHECK(uses("Elapsed") == 2);
    CHECK(uses("Elapsed 2") == 2);
    CHECK(graph.Comments.size() == 2);
}

TEST_CASE("Move Object To slides an entity with the chosen easing", "[graph][combo][executor]")
{
    World world;
    ComboLibrary library;
    library.Load(BuiltInSources());
    const auto& entry = Builtin(library, "Move Object To");
    REQUIRE(entry.Fields.size() == 4);
    CHECK(entry.Fields[3].Choice);

    SECTION("linear")
    {
        AnimationGraphData graph;
        entry.SpawnWith(graph, {Value{world.Target}, Value{glm::vec3(0, 10, 0)}, Value{2.0f}, Value{0}});
        GraphExecutor executor(graph);
        Apply(executor.ExecuteStartEvent(world.scene), world.scene);
        Tick(executor, world.scene, 0.5f);
        Tick(executor, world.scene, 0.5f);
        CHECK(world.Transform(world.Target).Position.y == Approx(5.0f));
        Tick(executor, world.scene, 0.5f);
        Tick(executor, world.scene, 0.5f);
        Tick(executor, world.scene, 0.5f);
        CHECK(world.Transform(world.Target).Position.y == Approx(10.0f));
    }
    SECTION("ease in is slower at the start")
    {
        AnimationGraphData graph;
        entry.SpawnWith(graph, {Value{world.Target}, Value{glm::vec3(0, 8, 0)}, Value{2.0f}, Value{1}});
        GraphExecutor executor(graph);
        Apply(executor.ExecuteStartEvent(world.scene), world.scene);
        Tick(executor, world.scene, 1.0f);
        CHECK(world.Transform(world.Target).Position.y == Approx(2.0f));
    }
    SECTION("smooth step")
    {
        AnimationGraphData graph;
        entry.SpawnWith(graph, {Value{world.Target}, Value{glm::vec3(0, 8, 0)}, Value{2.0f}, Value{3}});
        GraphExecutor executor(graph);
        Apply(executor.ExecuteStartEvent(world.scene), world.scene);
        Tick(executor, world.scene, 0.5f);
        CHECK(world.Transform(world.Target).Position.y == Approx(8.0f * (3 * 0.0625f - 2 * 0.015625f)));
    }
}

TEST_CASE("Orbit Camera Around keeps the camera on a circle around the target", "[graph][combo][executor]")
{
    World world;
    world.Transform(world.Target).Position = glm::vec3(1.0f, 2.0f, 3.0f);
    ComboLibrary library;
    library.Load(BuiltInSources());
    AnimationGraphData graph;
    Builtin(library, "Orbit Camera Around")
        .SpawnWith(graph, {Value{world.Camera}, Value{world.Target}, Value{10.0f}, Value{2.0f}});
    GraphExecutor executor(graph);
    for (int i = 0; i < 5; ++i)
    {
        Tick(executor, world.scene, 0.25f);
        const glm::vec3 offset = world.Transform(world.Camera).Position - glm::vec3(1.0f, 2.0f, 3.0f);
        CHECK(glm::length(glm::vec2(offset.x, offset.z)) == Approx(10.0f).margin(0.001f));
        CHECK(offset.y == Approx(0.0f).margin(0.001f));
    }
    const float angle = 2.0f * 0.25f * 5;
    const glm::vec3 expected = glm::vec3(1.0f + 10.0f * std::cos(angle), 2.0f, 3.0f + 10.0f * std::sin(angle));
    CHECK(world.Transform(world.Camera).Position.x == Approx(expected.x).margin(0.001f));
    CHECK(world.Transform(world.Camera).Position.z == Approx(expected.z).margin(0.001f));
}

TEST_CASE("Fade Property and On Start Set Property drive component fields", "[graph][combo][executor]")
{
    World world;
    ComboLibrary library;
    library.Load(BuiltInSources());

    SECTION("fade black hole mass")
    {
        AnimationGraphData graph;
        Builtin(library, "Fade Property")
            .SpawnWith(graph, {Value{world.Target}, Value{0}, Value{2.0f}, Value{6.0f}, Value{2.0f}});
        GraphExecutor executor(graph);
        Tick(executor, world.scene, 1.0f);
        const auto mass = [&] { return world.scene.Registry().get<BlackHoleComponent>(world.scene.FindByGuid(world.Target)).Mass; };
        CHECK(mass() == Approx(4.0f));
        Tick(executor, world.scene, 1.0f);
        Tick(executor, world.scene, 1.0f);
        CHECK(mass() == Approx(6.0f));
    }
    SECTION("on start sets a property chosen by the form")
    {
        AnimationGraphData graph;
        Builtin(library, "On Start Set Property").SpawnWith(graph, {Value{world.Target}, Value{2}, Value{0.75f}});
        GraphExecutor executor(graph);
        Apply(executor.ExecuteStartEvent(world.scene), world.scene);
        CHECK(world.scene.Registry().get<BlackHoleComponent>(world.scene.FindByGuid(world.Target)).Charge == Approx(0.75f));
    }
}

TEST_CASE("Spawn Prefab At spawns when its flow input fires", "[graph][combo][executor]")
{
    World world;
    ComboLibrary library;
    library.Load(BuiltInSources());
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateStartEventNode(graph.AllocateId()));
    const auto& entry = Builtin(library, "Spawn Prefab At");
    const auto ids = entry.SpawnWith(graph, {Value{std::string("Sphere")}, Value{glm::vec3(4, 5, 6)}, Value{std::string("Pebble")}});
    REQUIRE(ConnectNewNode(graph, ids.front(), graph.Nodes[0].Outputs[0].Id));

    const auto before = world.scene.Registry().view<GPP::MetadataComponent>().size();
    GraphExecutor executor(graph);
    Apply(executor.ExecuteStartEvent(world.scene), world.scene);
    CHECK(world.scene.Registry().view<GPP::MetadataComponent>().size() == before + 1);
    bool found = false;
    for (auto [entity, metadata] : world.scene.Registry().view<const GPP::MetadataComponent>().each())
    {
        if (metadata.Name == "Pebble")
        {
            found = true;
            CHECK(world.scene.Registry().get<GPP::TransformComponent>(entity).Position == glm::vec3(4, 5, 6));
        }
    }
    CHECK(found);
}

TEST_CASE("Saving a selection produces a template that rebuilds the same cluster", "[graph][combo]")
{
    RegisterComponents();
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateStartEventNode(graph.AllocateId()));
    graph.Nodes.push_back(CreatePrintNode(graph.AllocateId()));
    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::String));
    graph.Nodes.back().ConstantValue = std::string("hello");
    graph.Nodes.push_back(CreateGetterNode(graph.AllocateId(), "Transform"));
    graph.Nodes.push_back(CreateStartEventNode(graph.AllocateId()));
    AddVariable(graph, "Counter", PinType::Float);
    graph.Nodes.push_back(CreateVariableGetNode(graph.AllocateId(), "Counter", PinType::Float));
    TryLink(graph, graph.Nodes[0].Outputs[0].Id, graph.Nodes[1].Inputs[0].Id);
    TryLink(graph, graph.Nodes[2].Outputs[0].Id, graph.Nodes[1].Inputs[1].Id);
    const std::vector<int> selection{graph.Nodes[0].Id, graph.Nodes[1].Id, graph.Nodes[2].Id, graph.Nodes[3].Id, graph.Nodes[5].Id};

    const auto yaml = SelectionToComboYaml(graph, selection, "My Combo", "Says hello");
    std::string error;
    const auto combo = ParseCombo(yaml, error);
    REQUIRE(combo);
    CHECK(error.empty());
    CHECK(combo->Name == "My Combo");
    REQUIRE(combo->Fields.size() == 2);
    CHECK(combo->Fields[0].Label == "Value");
    CHECK(std::get<std::string>(combo->Fields[0].Default) == "hello");
    CHECK(combo->Fields[1].Type == PinType::Object);
    REQUIRE(combo->Variables.size() == 1);

    AnimationGraphData rebuilt;
    const auto registry = BuildNodeRegistry();
    const auto result = InstantiateCombo(*combo, rebuilt, registry, {Value{std::string("changed")}, Value{std::uint64_t{7}}});
    REQUIRE(result.Ok);
    CHECK(rebuilt.Nodes.size() == 5);
    CHECK(rebuilt.Links.size() == 2);
    const auto constant = std::ranges::find(rebuilt.Nodes, NodeType::Constant, &Node::Type);
    CHECK(std::get<std::string>(constant->ConstantValue) == "changed");
    const auto getter = std::ranges::find(rebuilt.Nodes, NodeType::Getter, &Node::Type);
    CHECK(getter->TargetGuid == 7);

    const auto copy = DeserializeFromYaml(SerializeToYaml(rebuilt));
    CHECK(copy.Nodes.size() == rebuilt.Nodes.size());
}

TEST_CASE("Combo files are written without overwriting and hot reload through the asset service", "[graph][combo][assets]")
{
    RegisterComponents();
    const auto dir = std::filesystem::temp_directory_path() / "molehole_combo_test";
    std::filesystem::remove_all(dir);

    const auto first = SaveComboFile(dir, "Say Hi!", "Name: Say Hi\nNodes:\n  - {Key: s, Kind: Start}\n");
    REQUIRE(first);
    CHECK(first->filename() == "say_hi_.yaml");

    GPP::AssetOptions options;
    options.Kinds["Combos"] = {{dir}, {".yaml"}};
    GPP::AssetDirectories assets(std::move(options));
    assets.Poll();
    const auto version = assets.Version();

    ComboLibrary library;
    library.Load(ReadComboSources(assets));
    REQUIRE(library.Entries().size() == 1);
    CHECK(library.Entries()[0].Name == "Say Hi");

    const auto second = SaveComboFile(dir, "Say Hi!", "Name: Say Bye\nNodes:\n  - {Key: s, Kind: Start}\n");
    REQUIRE(second);
    CHECK(*second != *first);
    assets.Poll();
    CHECK(assets.Version() != version);
    library.Load(ReadComboSources(assets));
    CHECK(library.Entries().size() == 2);

    auto registry = BuildNodeRegistry();
    const auto base = registry.Entries().size();
    registry.ReplaceCombos(library.Entries());
    CHECK(registry.Entries().size() == base + 2);
    registry.ReplaceCombos({library.Entries()[0]});
    CHECK(registry.Entries().size() == base + 1);
    const auto results = registry.Search("say", std::nullopt, {});
    REQUIRE_FALSE(results.empty());
    CHECK(results.front().Entry->Category == kCombosCategory);

    std::filesystem::remove_all(dir);
}
