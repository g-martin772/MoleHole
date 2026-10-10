#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;
using GPP::Scene;

namespace
{
    void Connect(AnimationGraphData& graph, const int from, const int to)
    {
        graph.Links.push_back(Link{graph.AllocateId(), from, to});
    }

    // Function "AddOne"(x: Float) -> (y: Float) with y = x + 1.
    std::size_t MakeAddOne(SceneGraphs& graphs, const bool pure)
    {
        const auto index = AddFunctionGraph(graphs, "AddOne");
        auto& fn = graphs.Items[index];
        fn.Signature.Pure = pure;
        AddParam(fn.Signature, false, "x", PinType::Float);
        AddParam(fn.Signature, true, "y", PinType::Float);
        SyncFunction(graphs, fn.Name);

        auto& g = fn.Graph;
        Node one = CreateConstantNode(g.AllocateId(), PinType::Float);
        one.ConstantValue = 1.0f;
        g.Nodes.push_back(one);
        Node add = CreateMathNode(g.AllocateId(), NodeSubType::Add);
        g.Nodes.push_back(add);
        const Node& entry = *std::ranges::find(g.Nodes, NodeSubType::FunctionEntry, &Node::SubType);
        const Node& ret = *std::ranges::find(g.Nodes, NodeSubType::FunctionReturn, &Node::SubType);
        Connect(g, entry.Outputs.back().Id, add.Inputs[0].Id);
        Connect(g, one.Outputs[0].Id, add.Inputs[1].Id);
        Connect(g, add.Outputs[0].Id, ret.Inputs.back().Id);
        return index;
    }
}

TEST_CASE("New function graph has entry and return nodes joined by flow", "[graph][function]")
{
    SceneGraphs graphs;
    const auto index = AddFunctionGraph(graphs);
    const auto& fn = graphs.Items[index];
    CHECK(fn.IsFunction);
    REQUIRE(fn.Graph.Nodes.size() == 2);
    CHECK(fn.Graph.Nodes[0].SubType == NodeSubType::FunctionEntry);
    CHECK(fn.Graph.Nodes[1].SubType == NodeSubType::FunctionReturn);
    CHECK(fn.Graph.Links.size() == 1);
    CHECK(AddFunctionGraph(graphs) != index);
    CHECK(graphs.Items[1].Name != fn.Name);
}

TEST_CASE("Signature edits keep pin ids stable and update calls", "[graph][function]")
{
    SceneGraphs graphs;
    const auto fnIndex = AddFunctionGraph(graphs, "F");
    auto& sig = graphs.Items[fnIndex].Signature;
    AddParam(sig, false, "a", PinType::Float);
    AddParam(sig, false, "b", PinType::Vec3);
    SyncFunction(graphs, "F");

    NamedGraph caller{.Name = "Main"};
    caller.Graph.Nodes.push_back(CreateFunctionCallNode(caller.Graph.AllocateId(), "F", sig));
    caller.Graph.Nodes.push_back(CreateConstantNode(caller.Graph.AllocateId(), PinType::Vec3));
    graphs.Items.push_back(caller);
    auto& main = graphs.Items.back().Graph;
    const int bPin = main.Nodes[0].Inputs[2].Id;
    Connect(main, main.Nodes[1].Outputs[0].Id, bPin);

    SECTION("rename keeps the link")
    {
        RenameParam(graphs.Items[fnIndex].Signature, false, 1, "bee");
        SyncFunction(graphs, "F");
        CHECK(main.Links.size() == 1);
        CHECK(main.Nodes[0].Inputs[2].Name == "bee");
        CHECK(main.Nodes[0].Inputs[2].Id == bPin);
    }
    SECTION("removing an earlier param keeps the other pin id")
    {
        RemoveParam(graphs.Items[fnIndex].Signature, false, 0);
        SyncFunction(graphs, "F");
        CHECK(main.Links.size() == 1);
        CHECK(main.Nodes[0].Inputs.size() == 2);
        CHECK(main.Nodes[0].Inputs[1].Id == bPin);
    }
    SECTION("retyping to an incompatible type drops the link")
    {
        RetypeParam(graphs.Items[fnIndex].Signature, false, 1, PinType::String);
        SyncFunction(graphs, "F");
        CHECK(main.Links.empty());
    }
    SECTION("making it pure removes flow pins")
    {
        graphs.Items[fnIndex].Signature.Pure = true;
        SyncFunction(graphs, "F");
        CHECK(std::ranges::none_of(main.Nodes[0].Inputs, [](const Pin& p) { return p.Type == PinType::Flow; }));
        CHECK(CallMatchesSignature(main.Nodes[0], graphs.Items[fnIndex].Signature));
    }
    SECTION("renaming the function updates calls")
    {
        REQUIRE(RenameFunction(graphs, "F", "G"));
        CHECK(main.Nodes[0].FunctionName == "G");
        CHECK(main.Nodes[0].Name == "Call G");
        CHECK_FALSE(RenameFunction(graphs, "G", "Main"));
    }
    SECTION("params reject flow type and duplicate names")
    {
        CHECK(AddParam(graphs.Items[fnIndex].Signature, false, "x", PinType::Flow) == nullptr);
        CHECK(AddParam(graphs.Items[fnIndex].Signature, false, "a", PinType::Int)->Name == "a2");
    }
}

TEST_CASE("Functions persist in the scene YAML and old versions still load", "[graph][function][persistence]")
{
    SceneGraphs graphs;
    MakeAddOne(graphs, true);
    graphs.Items.push_back(NamedGraph{.Name = "Main"});

    const auto restored = SceneGraphsFromNode(SceneGraphsToNode(graphs));
    REQUIRE(restored.Items.size() == 2);
    CHECK(restored.Items[0].IsFunction);
    CHECK(restored.Items[0].Signature.Pure);
    REQUIRE(restored.Items[0].Signature.Inputs.size() == 1);
    CHECK(restored.Items[0].Signature.Inputs[0].Name == "x");
    CHECK(restored.Items[0].Signature.NextKey == 2);
    CHECK_FALSE(restored.Items[1].IsFunction);
    CHECK(restored.Version == kSceneGraphsVersion);

    YAML::Node legacy;
    legacy["Version"] = 2;
    YAML::Node item;
    item["Name"] = "Old";
    legacy["Items"].push_back(item);
    const auto old = SceneGraphsFromNode(legacy);
    REQUIRE(old.Items.size() == 1);
    CHECK_FALSE(old.Items[0].IsFunction);
}

TEST_CASE("Call nodes round-trip through graph YAML", "[graph][function][persistence]")
{
    FunctionSignature sig;
    AddParam(sig, true, "out", PinType::Int);
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateFunctionCallNode(graph.AllocateId(), "Fn", sig));
    const auto restored = DeserializeFromYaml(SerializeToYaml(graph));
    REQUIRE(restored.Nodes.size() == 1);
    CHECK(restored.Nodes[0].Type == NodeType::Call);
    CHECK(restored.Nodes[0].SubType == NodeSubType::FunctionCall);
    CHECK(restored.Nodes[0].FunctionName == "Fn");
    CHECK(CallMatchesSignature(restored.Nodes[0], sig));
}

TEST_CASE("Executor runs a flow function and reads its outputs", "[graph][function][executor]")
{
    SceneGraphs graphs;
    const auto fn = MakeAddOne(graphs, false);
    NamedGraph main{.Name = "Main"};
    auto& g = main.Graph;
    g.Nodes.push_back(CreateStartEventNode(g.AllocateId()));
    Node two = CreateConstantNode(g.AllocateId(), PinType::Float);
    two.ConstantValue = 2.0f;
    g.Nodes.push_back(two);
    g.Nodes.push_back(CreateFunctionCallNode(g.AllocateId(), "AddOne", graphs.Items[fn].Signature));
    g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
    Connect(g, g.Nodes[0].Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
    Connect(g, two.Outputs[0].Id, g.Nodes[2].Inputs[1].Id);
    Connect(g, g.Nodes[2].Outputs[0].Id, g.Nodes[3].Inputs[0].Id);
    Connect(g, g.Nodes[2].Outputs[1].Id, g.Nodes[3].Inputs[1].Id);
    graphs.Items.push_back(std::move(main));

    std::vector<std::string> printed;
    GraphSetExecutor executor(graphs, [&](std::string m) { printed.push_back(std::move(m)); });
    Scene scene("Fn");
    (void)executor.ExecuteStartEvent(scene);
    REQUIRE(printed.size() == 1);
    CHECK(printed[0].starts_with("3.0"));
}

TEST_CASE("Executor evaluates a pure function on demand", "[graph][function][executor]")
{
    SceneGraphs graphs;
    const auto fn = MakeAddOne(graphs, true);
    NamedGraph main{.Name = "Main"};
    auto& g = main.Graph;
    g.Nodes.push_back(CreateStartEventNode(g.AllocateId()));
    Node five = CreateConstantNode(g.AllocateId(), PinType::Float);
    five.ConstantValue = 5.0f;
    g.Nodes.push_back(five);
    g.Nodes.push_back(CreateFunctionCallNode(g.AllocateId(), "AddOne", graphs.Items[fn].Signature));
    g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
    Connect(g, g.Nodes[0].Outputs[0].Id, g.Nodes[3].Inputs[0].Id);
    Connect(g, five.Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
    Connect(g, g.Nodes[2].Outputs[0].Id, g.Nodes[3].Inputs[1].Id);
    graphs.Items.push_back(std::move(main));

    std::vector<std::string> printed;
    GraphSetExecutor executor(graphs, [&](std::string m) { printed.push_back(std::move(m)); });
    Scene scene("Fn");
    (void)executor.ExecuteStartEvent(scene);
    REQUIRE(printed.size() == 1);
    CHECK(printed[0].starts_with("6.0"));
}

TEST_CASE("Recursive and dangling calls terminate", "[graph][function][executor]")
{
    SceneGraphs graphs;
    const auto index = AddFunctionGraph(graphs, "Loop");
    auto& fn = graphs.Items[index];
    fn.Graph.Links.clear();
    fn.Graph.Nodes.push_back(CreateFunctionCallNode(fn.Graph.AllocateId(), "Loop", fn.Signature));
    fn.Graph.Nodes.push_back(CreatePrintNode(fn.Graph.AllocateId()));
    const Node& entry = fn.Graph.Nodes[0];
    Connect(fn.Graph, entry.Outputs[0].Id, fn.Graph.Nodes[2].Inputs[0].Id);
    Connect(fn.Graph, fn.Graph.Nodes[2].Outputs[0].Id, fn.Graph.Nodes[3].Inputs[0].Id);

    NamedGraph main{.Name = "Main"};
    auto& g = main.Graph;
    g.Nodes.push_back(CreateStartEventNode(g.AllocateId()));
    g.Nodes.push_back(CreateFunctionCallNode(g.AllocateId(), "Loop", fn.Signature));
    g.Nodes.push_back(CreateFunctionCallNode(g.AllocateId(), "Missing", FunctionSignature{}));
    g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
    Connect(g, g.Nodes[0].Outputs[0].Id, g.Nodes[1].Inputs[0].Id);
    Connect(g, g.Nodes[1].Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
    Connect(g, g.Nodes[2].Outputs[0].Id, g.Nodes[3].Inputs[0].Id);
    graphs.Items.push_back(std::move(main));

    int prints = 0;
    GraphSetExecutor executor(graphs, [&](std::string) { ++prints; });
    Scene scene("Fn");
    (void)executor.ExecuteStartEvent(scene);
    CHECK(prints >= 2);
    CHECK(prints < 100);
}

TEST_CASE("Function palette entries list every function plus Return inside one", "[graph][function][palette]")
{
    SceneGraphs graphs;
    MakeAddOne(graphs, false);
    graphs.Items.push_back(NamedGraph{.Name = "Main"});

    const auto fromMain = FunctionNodeEntries(graphs, &graphs.Items[1]);
    REQUIRE(fromMain.size() == 1);
    CHECK(fromMain[0].Name == "Call AddOne");
    CHECK(fromMain[0].Category == kFunctionsCategory);

    AnimationGraphData scratch;
    const auto ids = fromMain[0].Spawn(scratch);
    REQUIRE(ids.size() == 1);
    CHECK(scratch.FindNode(ids[0])->FunctionName == "AddOne");

    CHECK(FunctionNodeEntries(graphs, &graphs.Items[0]).size() == 2);
}

TEST_CASE("Math nodes accept any numeric pin type", "[graph][edit]")
{
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::Vec3));
    graph.Nodes.push_back(CreateMathNode(graph.AllocateId(), NodeSubType::Add));
    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::String));
    CHECK(CanLink(graph, graph.Nodes[0].Outputs[0].Id, graph.Nodes[1].Inputs[0].Id));
    CHECK_FALSE(CanLink(graph, graph.Nodes[2].Outputs[0].Id, graph.Nodes[1].Inputs[0].Id));
}
