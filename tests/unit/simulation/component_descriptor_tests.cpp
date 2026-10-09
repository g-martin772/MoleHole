#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct ThrusterComponent
    {
        float Thrust{3.0f};
        glm::vec3 Direction{0.0f, 0.0f, 1.0f};
        bool Armed{false};
    };

    // The only per-component declaration: no YAML::convert, inspector code, graph table or add button.
    void RegisterThruster()
    {
        static std::once_flag flag;
        std::call_once(flag, []
        {
            RegisterComponents();
            RegisterComponent<ThrusterComponent>("Thruster", ComponentDescription<ThrusterComponent>{
                .Fields = {
                    Field("Thrust", &ThrusterComponent::Thrust, {.Min = 0.0f, .Max = 100.0f}),
                    Field("Direction", &ThrusterComponent::Direction, {.Kind = FieldKind::Direction}),
                    Field("Armed", &ThrusterComponent::Armed),
                },
                .Defaults = {.Thrust = 9.0f}});
        });
    }
}

TEST_CASE("A single descriptor drives YAML, runtime fields, add factory and graph pins",
          "[simulation][reflection][animation]")
{
    RegisterThruster();

    Scene scene("OnePlace");
    const auto entity = scene.CreateEntity("Engine");
    const auto guid = scene.GuidOf(entity);
    const auto* info = ComponentRegistry::Instance().FindByName("Thruster");
    REQUIRE(info != nullptr);

    info->Add(scene.Registry(), entity);
    CHECK(scene.Registry().get<ThrusterComponent>(entity).Thrust == 9.0f);

    REQUIRE(info->Fields.size() == 3);
    CHECK(info->Fields[1].Meta.Kind == FieldKind::Direction);
    REQUIRE(info->Fields[2].Set(scene.Registry(), entity, FieldValue{true}));

    Scene loaded;
    loaded.DeserializeFromYaml(scene.SerializeToYaml());
    const auto& restored = loaded.Registry().get<ThrusterComponent>(loaded.FindByGuid(guid));
    CHECK(restored.Thrust == 9.0f);
    CHECK(restored.Armed);

    const auto* category = FindPropertyCategory("Thruster");
    REQUIRE(category != nullptr);
    REQUIRE(category->Properties.size() == 3);
    CHECK(category->Properties[0].Label == "Thrust");
    CHECK(category->Properties[0].Type == PinType::Float);
    CHECK(category->Properties[1].Type == PinType::Vec3);
    CHECK(category->Properties[2].Type == PinType::Bool);

    const auto setter = CreateSetterNode(1, "Thruster");
    CHECK(setter.SubType == NodeSubType::Component);
    CHECK(setter.Inputs.size() == 2 + 3);
    const auto decomposer = CreateDecomposerNode(2, "Thruster");
    CHECK(decomposer.Outputs.size() == 3);

    AnimationGraphData graph;
    graph.Nodes.push_back(setter);
    const auto restoredGraph = DeserializeFromYaml(SerializeToYaml(graph));
    REQUIRE(restoredGraph.Nodes.size() == 1);
    CHECK(restoredGraph.Nodes[0].Component == "Thruster");
    CHECK(restoredGraph.Nodes[0].SubType == NodeSubType::Component);

    category->Properties[0].Set(loaded, loaded.FindByGuid(guid), 42.0f);
    CHECK(loaded.Registry().get<ThrusterComponent>(loaded.FindByGuid(guid)).Thrust == 42.0f);
    CHECK(std::get<float>(category->Properties[0].Get(loaded, loaded.FindByGuid(guid))) == 42.0f);
}
