#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace MoleHole;

namespace
{
    std::vector<std::string> Names(const std::vector<PaletteResult>& results)
    {
        std::vector<std::string> names;
        for (const auto& r : results) names.push_back(r.Entry->Name);
        return names;
    }

    bool Contains(const std::vector<PaletteResult>& results, const std::string& name)
    {
        return std::ranges::contains(Names(results), name);
    }
}

TEST_CASE("Registry search ranks the best name match first", "[graph][palette]")
{
    RegisterComponents();
    const auto registry = BuildNodeRegistry();
    const auto results = registry.Search("branch", std::nullopt, {});
    REQUIRE_FALSE(results.empty());
    CHECK(results.front().Entry->Name == "Branch");
}

TEST_CASE("Registry search finds nodes through keywords", "[graph][palette]")
{
    RegisterComponents();
    const auto registry = BuildNodeRegistry();
    CHECK(Contains(registry.Search("loop", std::nullopt, {}), "For Loop"));
    CHECK(Contains(registry.Search("instantiate", std::nullopt, {}), "Spawn Entity"));
    CHECK(registry.Search("zzzzqq", std::nullopt, {}).empty());
}

TEST_CASE("Registry includes component descriptor nodes", "[graph][palette]")
{
    RegisterComponents();
    const auto registry = BuildNodeRegistry();
    const auto categories = GetPropertyCategories();
    REQUIRE_FALSE(categories.empty());
    CHECK(Contains(registry.Search("Get " + categories.front()->DisplayName, std::nullopt, {}),
                   "Get " + categories.front()->DisplayName));
}

TEST_CASE("Pin filter keeps only nodes with a compatible pin on the opposite side", "[graph][palette]")
{
    RegisterComponents();
    const auto registry = BuildNodeRegistry();

    const auto fromFlowOutput = registry.Search("", PinFilter{PinType::Flow, true}, {});
    CHECK(Contains(fromFlowOutput, "Branch"));
    CHECK(Contains(fromFlowOutput, "Print"));
    CHECK_FALSE(Contains(fromFlowOutput, "Add"));
    CHECK_FALSE(Contains(fromFlowOutput, "Start"));

    const auto fromFloatInput = registry.Search("", PinFilter{PinType::Float, false}, {});
    CHECK(Contains(fromFloatInput, "Float Constant"));
    CHECK(Contains(fromFloatInput, "Add"));
    CHECK_FALSE(Contains(fromFloatInput, "Print"));
}

TEST_CASE("Empty query lists recents first, then categories in order", "[graph][palette]")
{
    RegisterComponents();
    const auto registry = BuildNodeRegistry();
    const auto results = registry.Search("", std::nullopt, {"Print", "Add"});
    REQUIRE(results.size() > 3);
    CHECK(results[0].Entry->Name == "Print");
    CHECK(results[1].Entry->Name == "Add");
    CHECK(results[2].Entry->Category == "Events");
}

TEST_CASE("Combo entries appear under the Combos category", "[graph][palette]")
{
    NodeRegistry registry;
    registry.AddCombo(SingleNodeEntry("Move To", "ignored", "Moves an object.", {"tween"}, CreatePrintNode));
    const auto results = registry.Search("tween", std::nullopt, {});
    REQUIRE(results.size() == 1);
    CHECK(results[0].Entry->Category == kCombosCategory);
}

TEST_CASE("Variable entries come from the graph and spawn typed nodes", "[graph][palette]")
{
    AnimationGraphData graph;
    graph.Variables.push_back(Variable{"Speed", PinType::Float});
    const auto extra = VariableNodeEntries(graph);
    const NodeRegistry registry;
    const auto results = registry.Search("speed", std::nullopt, {}, extra);
    REQUIRE(results.size() == 2);

    AnimationGraphData target;
    const auto ids = results.front().Entry->Spawn(target);
    REQUIRE(ids.size() == 1);
    CHECK(target.FindNode(ids[0])->VariableName == "Speed");
}

TEST_CASE("Registry describes nodes by kind", "[graph][palette]")
{
    RegisterComponents();
    const auto registry = BuildNodeRegistry();
    AnimationGraphData graph;
    CHECK_FALSE(registry.Describe(CreateBranchNode(graph.AllocateId())).empty());
}

TEST_CASE("Recents are de-duplicated, most recent first and capped", "[graph][palette]")
{
    std::vector<std::string> recents;
    for (int i = 0; i < 12; ++i) recents = PushRecent(recents, "Node " + std::to_string(i));
    CHECK(recents.size() == kMaxRecentNodes);
    CHECK(recents.front() == "Node 11");
    recents = PushRecent(recents, "Node 8");
    CHECK(recents.front() == "Node 8");
    CHECK(recents.size() == kMaxRecentNodes);
}

TEST_CASE("MoveSelection wraps around", "[graph][palette]")
{
    CHECK(MoveSelection(0, -1, 5) == 4);
    CHECK(MoveSelection(4, 1, 5) == 0);
    CHECK(MoveSelection(2, 1, 0) == 0);
}

TEST_CASE("ConnectNewNode wires the first compatible pin", "[graph][palette]")
{
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateStartEventNode(graph.AllocateId()));
    const int startOut = graph.Nodes[0].Outputs[0].Id;
    graph.Nodes.push_back(CreatePrintNode(graph.AllocateId()));
    const int print = graph.Nodes[1].Id;

    CHECK(ConnectNewNode(graph, print, startOut));
    REQUIRE(graph.Links.size() == 1);
    CHECK(graph.Links[0].EndPinId == graph.Nodes[1].Inputs[0].Id);

    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::String));
    const int constant = graph.Nodes[2].Id;
    CHECK(ConnectNewNode(graph, constant, graph.Nodes[1].Inputs[1].Id));
    CHECK(graph.Links.size() == 2);

    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::String));
    CHECK(ConnectNewNode(graph, graph.Nodes[3].Id, graph.Nodes[1].Inputs[1].Id));
    CHECK(graph.Links.size() == 2);

    CHECK_FALSE(ConnectNewNode(graph, constant, startOut));
}

TEST_CASE("CanLink rejects used inputs, self links and incompatible types", "[graph][edit]")
{
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::Float));
    graph.Nodes.push_back(CreateMathNode(graph.AllocateId(), NodeSubType::Add));
    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::Bool));
    const int floatOut = graph.Nodes[0].Outputs[0].Id;
    const int boolOut = graph.Nodes[2].Outputs[0].Id;
    const int addA = graph.Nodes[1].Inputs[0].Id;

    CHECK_FALSE(CanLink(graph, boolOut, addA));
    CHECK(TryLink(graph, floatOut, addA));
    CHECK_FALSE(CanLink(graph, floatOut, addA));
    CHECK_FALSE(CanLink(graph, graph.Nodes[1].Outputs[0].Id, graph.Nodes[1].Inputs[1].Id));
}
