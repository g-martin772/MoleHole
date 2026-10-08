#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

// Covers the scene-file persistence half of the animation-graph UI pass: AnimationGraphComponent
// round-trips through GPP::Scene's generic YAML serializer exactly like BlackHoleComponent/
// SphereComponent already do (see components_tests.cpp), and the GraphYaml string it carries
// round-trips through animation_graph.cppm's own SerializeToYaml/DeserializeFromYaml into an
// equivalent AnimationGraphData. UI drawing/interaction (the node-editor canvas itself) isn't
// unit-testable here -- this only covers what's testable without ImGui/a live window.

TEST_CASE("AnimationGraphComponent round-trips through scene YAML", "[simulation][scene][animation]")
{
    RegisterComponents();

    // Build a small, non-trivial graph (Tick -> Decomposer(BlackHole).Mass + DeltaTime -> Add ->
    // Setter(BlackHole).Mass), matching the executor tests' own shape, so this exercises every node
    // kind's Id/Name/Type/SubType/TargetGuid fields plus a Link through the string round-trip.
    AnimationGraphData graph;
    graph.Variables.push_back(Variable{.Name = "counter", .Type = PinType::Float});

    Node tickNode = CreateTickEventNode(graph.AllocateId());
    const int tickFlowOut = tickNode.Outputs[0].Id;
    graph.Nodes.push_back(tickNode);

    Node getterNode = CreateGetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    getterNode.TargetGuid = 42;
    graph.Nodes.push_back(getterNode);

    Node setterNode = CreateSetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    const int setterFlowIn = setterNode.Inputs[0].Id;
    graph.Nodes.push_back(setterNode);

    graph.Links.push_back(Link{graph.AllocateId(), tickFlowOut, setterFlowIn});

    const std::string graphYaml = SerializeToYaml(graph);

    Scene scene("AnimGraphPersistenceTest");
    const auto entity = scene.CreateEntity("", kAnimationGraphDataTypeTag);
    scene.Registry().emplace<AnimationGraphComponent>(entity, AnimationGraphComponent{.GraphYaml = graphYaml});
    const auto guid = scene.GuidOf(entity);

    Scene loaded;
    loaded.DeserializeFromYaml(scene.SerializeToYaml());

    const auto loadedEntity = loaded.FindByGuid(guid);
    REQUIRE(loaded.IsValid(loadedEntity));
    REQUIRE(loaded.Registry().all_of<AnimationGraphComponent>(loadedEntity));

    const auto& component = loaded.Registry().get<AnimationGraphComponent>(loadedEntity);
    const AnimationGraphData restored = DeserializeFromYaml(component.GraphYaml);

    REQUIRE(restored.Nodes.size() == graph.Nodes.size());
    REQUIRE(restored.Links.size() == graph.Links.size());
    REQUIRE(restored.Variables.size() == 1);
    CHECK(restored.Variables[0].Name == "counter");

    const Node* restoredGetter = restored.FindNode(getterNode.Id);
    REQUIRE(restoredGetter != nullptr);
    CHECK(restoredGetter->Type == NodeType::Getter);
    CHECK(restoredGetter->SubType == NodeSubType::BlackHole);
    CHECK(restoredGetter->TargetGuid == 42);

    CHECK(restored.Links[0].StartPinId == tickFlowOut);
    CHECK(restored.Links[0].EndPinId == setterFlowIn);
}

TEST_CASE("A scene with no AnimationGraphComponent has none findable via the registry view",
          "[simulation][scene][animation]")
{
    RegisterComponents();
    Scene scene("AnimGraphAbsentTest");
    scene.CreateEntity("BlackHole", "BlackHole");

    int count = 0;
    for (auto [entity, component] : scene.Registry().view<const AnimationGraphComponent>().each())
    {
        (void)entity;
        (void)component;
        ++count;
    }
    CHECK(count == 0);
}

TEST_CASE("Node::Position round-trips through SerializeToYaml/DeserializeFromYaml", "[simulation][animation]")
{
    AnimationGraphData graph;
    Node node = CreateStartEventNode(graph.AllocateId());
    node.Position = glm::vec2(123.5f, -45.0f);
    const int nodeId = node.Id;
    graph.Nodes.push_back(node);

    const AnimationGraphData restored = DeserializeFromYaml(SerializeToYaml(graph));
    const Node* restoredNode = restored.FindNode(nodeId);
    REQUIRE(restoredNode != nullptr);
    CHECK(restoredNode->Position.x == Catch::Approx(123.5f));
    CHECK(restoredNode->Position.y == Catch::Approx(-45.0f));
}
