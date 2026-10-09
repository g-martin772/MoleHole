#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;
using GPP::Scene;

TEST_CASE("Reroute nodes pass values and flow through", "[graph][reroute]")
{
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::String));
    graph.Nodes.back().ConstantValue = std::string("three");
    graph.Nodes.push_back(CreateRerouteNode(graph.AllocateId(), PinType::String));
    graph.Nodes.push_back(CreateStartEventNode(graph.AllocateId()));
    graph.Nodes.push_back(CreateRerouteNode(graph.AllocateId(), PinType::Flow));
    graph.Nodes.push_back(CreatePrintNode(graph.AllocateId()));
    REQUIRE(TryLink(graph, graph.Nodes[0].Outputs[0].Id, graph.Nodes[1].Inputs[0].Id));
    REQUIRE(TryLink(graph, graph.Nodes[1].Outputs[0].Id, graph.Nodes[4].Inputs[1].Id));
    REQUIRE(TryLink(graph, graph.Nodes[2].Outputs[0].Id, graph.Nodes[3].Inputs[0].Id));
    REQUIRE(TryLink(graph, graph.Nodes[3].Outputs[0].Id, graph.Nodes[4].Inputs[0].Id));

    std::vector<std::string> printed;
    GraphExecutor executor(graph, [&](std::string message) { printed.push_back(std::move(message)); });
    Scene scene("Reroute");
    (void)executor.ExecuteStartEvent(scene);
    REQUIRE(printed.size() == 1);
    CHECK(printed[0].find("three") != std::string::npos);
}

TEST_CASE("Reroute round-trips through YAML", "[graph][reroute]")
{
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateRerouteNode(graph.AllocateId(), PinType::Vec3));
    const auto restored = DeserializeFromYaml(SerializeToYaml(graph));
    REQUIRE(restored.Nodes.size() == 1);
    CHECK(restored.Nodes[0].Type == NodeType::Reroute);
    CHECK(restored.Nodes[0].Inputs[0].Type == PinType::Vec3);
}

TEST_CASE("Comments and variable defaults round-trip and old graphs still load", "[graph][persistence]")
{
    AnimationGraphData graph;
    Comment comment;
    comment.Id = graph.AllocateId();
    comment.Title = "Setup";
    comment.Position = {10.0f, 20.0f};
    comment.Size = {400.0f, 250.0f};
    comment.Color = {0.1f, 0.2f, 0.3f, 0.4f};
    graph.Comments.push_back(comment);
    AddVariable(graph, "Speed", PinType::Float).Default = 2.5f;

    const auto restored = DeserializeFromYaml(SerializeToYaml(graph));
    REQUIRE(restored.Comments.size() == 1);
    CHECK(restored.Comments[0].Title == "Setup");
    CHECK(restored.Comments[0].Size == glm::vec2(400.0f, 250.0f));
    CHECK(restored.Comments[0].Color == glm::vec4(0.1f, 0.2f, 0.3f, 0.4f));
    REQUIRE(restored.Variables.size() == 1);
    CHECK(GetValueAs<float>(restored.Variables[0].Default) == 2.5f);

    const auto legacy = DeserializeFromYaml("NextId: 3\nVariables:\n  - Name: Old\n    Type: Int\n");
    REQUIRE(legacy.Variables.size() == 1);
    CHECK(legacy.Comments.empty());
    CHECK(GetValueAs<int>(legacy.Variables[0].Default, -1) == 0);
}

TEST_CASE("Variable defaults seed the executor", "[graph][variables]")
{
    AnimationGraphData graph;
    AddVariable(graph, "Count", PinType::String).Default = std::string("seven");
    graph.Nodes.push_back(CreateStartEventNode(graph.AllocateId()));
    graph.Nodes.push_back(CreatePrintNode(graph.AllocateId()));
    graph.Nodes.push_back(CreateVariableGetNode(graph.AllocateId(), "Count", PinType::String));
    REQUIRE(TryLink(graph, graph.Nodes[0].Outputs[0].Id, graph.Nodes[1].Inputs[0].Id));
    REQUIRE(TryLink(graph, graph.Nodes[2].Outputs[0].Id, graph.Nodes[1].Inputs[1].Id));

    std::vector<std::string> printed;
    GraphExecutor executor(graph, [&](std::string message) { printed.push_back(std::move(message)); });
    Scene scene("Defaults");
    (void)executor.ExecuteStartEvent(scene);
    REQUIRE(printed.size() == 1);
    CHECK(printed[0].find("seven") != std::string::npos);
}

TEST_CASE("Variable rename and removal update referencing nodes", "[graph][variables]")
{
    AnimationGraphData graph;
    AddVariable(graph, "Speed", PinType::Float);
    graph.Nodes.push_back(CreateVariableGetNode(graph.AllocateId(), "Speed", PinType::Float));
    graph.Nodes.push_back(CreateVariableSetNode(graph.AllocateId(), "Speed", PinType::Float));
    AddVariable(graph, "Other", PinType::Int);

    CHECK_FALSE(RenameVariable(graph, "Speed", "Other"));
    CHECK_FALSE(RenameVariable(graph, "Speed", ""));
    CHECK(RenameVariable(graph, "Speed", "Velocity"));
    CHECK(graph.Variables[0].Name == "Velocity");
    CHECK(graph.Nodes[0].VariableName == "Velocity");
    CHECK(graph.Nodes[0].Name == "Get Velocity");
    CHECK(graph.Nodes[1].Name == "Set Velocity");
    CHECK(graph.Nodes[0].Outputs[0].Name == "Velocity");

    RemoveVariable(graph, "Velocity");
    CHECK(graph.Variables.size() == 1);
    CHECK(graph.Nodes[0].VariableName.empty());
}

TEST_CASE("AddVariable picks unique names with typed defaults", "[graph][variables]")
{
    AnimationGraphData graph;
    AddVariable(graph, "Value", PinType::Vec3);
    const auto& second = AddVariable(graph, "Value", PinType::Bool);
    CHECK(second.Name == "Value 2");
    CHECK(std::holds_alternative<bool>(second.Default));
}

TEST_CASE("RemoveItems deletes nodes and comments from one id list", "[graph][edit]")
{
    AnimationGraphData graph;
    graph.Nodes.push_back(CreatePrintNode(graph.AllocateId()));
    Comment comment;
    comment.Id = graph.AllocateId();
    graph.Comments.push_back(comment);
    RemoveItems(graph, {graph.Nodes[0].Id, comment.Id});
    CHECK(graph.Nodes.empty());
    CHECK(graph.Comments.empty());
}

TEST_CASE("Clipboard copies comments with fresh ids", "[graph][clipboard]")
{
    AnimationGraphData graph;
    Comment comment;
    comment.Id = graph.AllocateId();
    comment.Title = "Group";
    comment.Position = {50.0f, 60.0f};
    graph.Comments.push_back(comment);
    graph.Nodes.push_back(CreatePrintNode(graph.AllocateId()));
    graph.Nodes.back().Position = {80.0f, 90.0f};

    const auto result = PasteGraphSelection(graph, CopyGraphSelection(graph, {comment.Id, graph.Nodes[0].Id}), {500.0f, 500.0f});
    REQUIRE(result.CommentIds.size() == 1);
    CHECK(result.CommentIds[0] != comment.Id);
    CHECK(graph.FindComment(result.CommentIds[0])->Position == glm::vec2(500.0f, 500.0f));
    CHECK(graph.FindNode(result.NodeIds[0])->Position == glm::vec2(530.0f, 530.0f));
}

TEST_CASE("Palette offers Comment and typed Reroute entries", "[graph][palette]")
{
    RegisterComponents();
    const auto registry = BuildNodeRegistry();
    const auto comment = registry.Search("comment", std::nullopt, {});
    REQUIRE_FALSE(comment.empty());
    AnimationGraphData graph;
    const auto ids = comment.front().Entry->Spawn(graph);
    CHECK(graph.Comments.size() == 1);
    CHECK(ids.size() == 1);

    const auto filtered = registry.Search("reroute", PinFilter{PinType::Vec3, true}, {});
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].Entry->Name == "Reroute (Vec3)");
}
