#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;
using GPP::Scene;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    struct ScriptDir
    {
        fs::path Root;
        GPP::AssetOptions Options;

        explicit ScriptDir(const std::string& name)
        {
            Root = fs::temp_directory_path() / ("molehole_script_components_" + name);
            fs::remove_all(Root);
            fs::create_directories(Root);
            Options.Kinds["Scripts"] = GPP::AssetKindOptions{{Root}, {".luau"}};
        }
        ~ScriptDir() { fs::remove_all(Root); }
        void Write(const std::string& script, const std::string& source) const { std::ofstream(Root / (script + ".luau")) << source; }
    };

    struct World
    {
        Scene Level{"Components"};

        World()
        {
            RegisterComponents();
        }

        entt::entity Spawn(const std::string& name, std::vector<GPP::ScriptEntry> entries, const glm::vec3& at = {})
        {
            const auto entity = Level.CreateEntity(name);
            Level.Registry().emplace<GPP::TransformComponent>(entity).Position = at;
            Level.Registry().emplace<GPP::ScriptsComponent>(entity, GPP::ScriptsComponent{std::move(entries)});
            return entity;
        }

        glm::vec3 Position(const entt::entity entity) { return Level.Registry().get<GPP::TransformComponent>(entity).Position; }
        std::uint64_t Guid(const entt::entity entity) { return Level.GuidOf(entity); }
    };

    void Apply(Scene& scene, PendingWrites writes)
    {
        for (auto& write : writes) write(scene);
    }
}

TEST_CASE("Luau script components run through the graph runtime with props and scene writes", "[scripts][runtime]")
{
    ScriptDir dir("luau");
    dir.Write("shifter", R"(return {
        properties = { { name = "Step", type = "vec3", default = vector.create(1, 0, 0) }, { name = "Buddy", type = "entity" } },
        OnTick = function(self, dt)
            local at = scene.get(self.entity, "Transform", "Position")
            scene.set(self.entity, "Transform", "Position", at + self.props.Step * dt)
            if self.props.Buddy then scene.set(self.props.Buddy, "Transform", "Scale", vector.create(2, 2, 2)) end
        end })");
    GPP::AssetDirectories assets(dir.Options);
    World world;
    const auto buddy = world.Spawn("Buddy", {});
    const auto mover = world.Spawn("Mover", {GPP::ScriptEntry{"shifter", {{"Step", glm::vec3(0, 4, 0)}, {"Buddy", world.Guid(buddy)}}}});
    SceneGraphs graphs;
    ScriptCache cache;
    ScriptRuntime runtime(graphs, cache, nullptr, {}, {}, &assets);
    REQUIRE(runtime.Ok());
    Apply(world.Level, runtime.ExecuteStartEvent(world.Level));
    for (int i = 0; i < 4; ++i) Apply(world.Level, runtime.ExecuteTickEvent(world.Level, 0.25f));
    CHECK(world.Position(mover).y == Approx(4.0f));
    CHECK(world.Level.Registry().get<GPP::TransformComponent>(buddy).Scale == glm::vec3(2.0f));
    CHECK(runtime.TakeScriptErrors().empty());
    REQUIRE(runtime.ScriptStatuses().size() == 1);
    CHECK(runtime.ScriptStatuses()[0].Message.empty());
}

TEST_CASE("A failing script component is reported with its entity and shows in the statuses", "[scripts][runtime]")
{
    ScriptDir dir("failing");
    dir.Write("bad", "return { OnTick = function(self, dt) error('kaput') end }");
    GPP::AssetDirectories assets(dir.Options);
    World world;
    const auto entity = world.Spawn("Bad", {GPP::ScriptEntry{"bad", {}}});
    SceneGraphs graphs;
    ScriptCache cache;
    ScriptRuntime runtime(graphs, cache, nullptr, {}, {}, &assets);
    Apply(world.Level, runtime.ExecuteStartEvent(world.Level));
    for (int i = 0; i < 3; ++i) Apply(world.Level, runtime.ExecuteTickEvent(world.Level, 0.1f));
    const auto errors = runtime.TakeScriptErrors();
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].Entity == world.Guid(entity));
    CHECK(errors[0].Message.find("kaput") != std::string::npos);
    REQUIRE(runtime.ScriptStatuses().size() == 1);
    CHECK_FALSE(runtime.ScriptStatuses()[0].Message.empty());
    CHECK(runtime.Ok());
}

TEST_CASE("Component graphs run once per attached entity with their variables as properties", "[scripts][runtime][graph]")
{
    NamedGraph spinner;
    spinner.Name = "Placer";
    spinner.IsComponent = true;
    auto& graph = spinner.Graph;
    graph.Variables.push_back(Variable{"Where", PinType::Vec3, glm::vec3(0.0f)});
    graph.Variables.push_back(Variable{"Hidden", PinType::Vec2, glm::vec2(0.0f)});
    graph.Nodes.push_back(CreateTickEventNode(graph.AllocateId()));
    const int tick = graph.Nodes.back().Id;
    graph.Nodes.push_back(CreateGetterNode(graph.AllocateId(), "Transform"));
    const int self = graph.Nodes.back().Id;
    graph.Nodes.push_back(CreateSetterNode(graph.AllocateId(), "Transform"));
    const int setter = graph.Nodes.back().Id;
    graph.Nodes.push_back(CreateVariableGetNode(graph.AllocateId(), "Where", PinType::Vec3));
    const int where = graph.Nodes.back().Id;
    REQUIRE(TryLink(graph, graph.FindNode(tick)->Outputs[0].Id, graph.FindNode(setter)->Inputs[0].Id));
    REQUIRE(TryLink(graph, graph.FindNode(self)->Outputs[0].Id, graph.FindNode(setter)->Inputs[1].Id));
    REQUIRE(TryLink(graph, graph.FindNode(where)->Outputs[0].Id, graph.FindNode(setter)->Inputs[2].Id));

    const auto descriptor = DescribeComponentGraph(spinner);
    REQUIRE(descriptor.Properties.size() == 1);
    CHECK(descriptor.Properties[0].Name == "Where");
    CHECK(descriptor.Properties[0].Type == GPP::ScriptPropType::Vec3);

    SceneGraphs graphs;
    graphs.Items.push_back(spinner);
    World world;
    const auto a = world.Spawn("A", {GPP::ScriptEntry{"Placer", {{"Where", glm::vec3(1, 2, 3)}}}});
    const auto b = world.Spawn("B", {GPP::ScriptEntry{"Placer", {{"Where", glm::vec3(7, 8, 9)}}}});
    const auto c = world.Spawn("C", {GPP::ScriptEntry{"Placer", {}}}, glm::vec3(5.0f));
    ScriptCache cache;
    ScriptRuntime runtime(graphs, cache);
    REQUIRE(runtime.Ok());
    Apply(world.Level, runtime.ExecuteStartEvent(world.Level));
    Apply(world.Level, runtime.ExecuteTickEvent(world.Level, 0.1f));
    CHECK(world.Position(a) == glm::vec3(1, 2, 3));
    CHECK(world.Position(b) == glm::vec3(7, 8, 9));
    CHECK(world.Position(c) == glm::vec3(0.0f));

    world.Level.DestroyEntity(b);
    Apply(world.Level, runtime.ExecuteTickEvent(world.Level, 0.1f));
    CHECK(runtime.ScriptStatuses().size() == 2);
}

TEST_CASE("Component graph flags and names survive scene serialization", "[scripts][persistence]")
{
    SceneGraphs graphs;
    graphs.Items.push_back(NamedGraph{.Name = "Placer", .IsComponent = true});
    graphs.Items.push_back(NamedGraph{.Name = "Plain"});
    const auto loaded = SceneGraphsFromNode(SceneGraphsToNode(graphs));
    REQUIRE(loaded.Items.size() == 2);
    CHECK(loaded.Items[0].IsComponent);
    CHECK_FALSE(loaded.Items[1].IsComponent);
}

TEST_CASE("Component graphs are skipped by the free-running graph set and cache separately", "[scripts][runtime][graph]")
{
    NamedGraph component;
    component.Name = "Comp";
    component.IsComponent = true;
    component.Graph.Nodes.push_back(CreateStartEventNode(component.Graph.AllocateId()));
    SceneGraphs graphs;
    graphs.Items.push_back(component);
    ScriptCache cache;
    ScriptRuntime runtime(graphs, cache);
    CHECK(runtime.Ok());
    CHECK(runtime.ScriptStatuses().empty());
    CHECK(cache.Get(graphs, graphs.Items[0])->IsComponent);
}

TEST_CASE("Script statuses are published to the board the inspector reads", "[scripts][ui]")
{
    ScriptStatusBoard board;
    CHECK(board.Error(1, 0).empty());
    board.Publish({GPP::ScriptStatus{1, 0, "a", "broken"}, GPP::ScriptStatus{1, 1, "b", ""}});
    CHECK(board.Error(1, 0) == "broken");
    CHECK(board.Error(1, 1).empty());
    board.Publish({});
    CHECK(board.Error(1, 0).empty());
}

TEST_CASE("Keys map to the names On Key nodes use", "[latent][events]")
{
    CHECK(KeyEventName(GPP::KeyCode::Space) == "Space");
    CHECK(KeyEventName(GPP::KeyCode::W) == "W");
    CHECK(KeyEventName(GPP::KeyCode::Num5) == "5");
    CHECK(KeyEventName(GPP::KeyCode::Return) == "Enter");
    CHECK_FALSE(KeyEventName(GPP::KeyCode::Unknown).has_value());
}
