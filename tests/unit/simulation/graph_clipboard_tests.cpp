#include <catch2/catch_test_macros.hpp>

import MoleHole;
import glm;
import std;

using namespace MoleHole;

namespace
{
    struct Fixture
    {
        AnimationGraphData Graph;
        int Start{};
        int Print{};
        int Constant{};

        Fixture()
        {
            auto start = CreateStartEventNode(Graph.AllocateId());
            start.Position = {100.0f, 50.0f};
            auto print = CreatePrintNode(Graph.AllocateId());
            print.Position = {300.0f, 80.0f};
            auto constant = CreateConstantNode(Graph.AllocateId(), PinType::Float);
            constant.Position = {0.0f, 400.0f};
            Start = start.Id;
            Print = print.Id;
            Constant = constant.Id;
            Graph.Links.push_back(Link{Graph.AllocateId(), start.Outputs[0].Id, print.Inputs[0].Id});
            Graph.Links.push_back(Link{Graph.AllocateId(), constant.Outputs[0].Id, print.Inputs[1].Id});
            Graph.Nodes = {start, print, constant};
        }
    };
}

TEST_CASE("Clipboard paste re-ids nodes and pins and keeps internal links", "[graph][clipboard]")
{
    Fixture f;
    const auto text = CopyGraphSelection(f.Graph, {f.Start, f.Print});
    CHECK(IsGraphClipboardText(text));

    const auto result = PasteGraphSelection(f.Graph, text, {1000.0f, 1000.0f});
    REQUIRE(result.NodeIds.size() == 2);
    CHECK(f.Graph.Nodes.size() == 5);

    const Node* start = f.Graph.FindNode(result.NodeIds[0]);
    const Node* print = f.Graph.FindNode(result.NodeIds[1]);
    REQUIRE(start != nullptr);
    REQUIRE(print != nullptr);
    CHECK(start->Id != f.Start);
    CHECK(start->Outputs[0].Id == start->Id * kPinIdStride + (f.Graph.FindNode(f.Start)->Outputs[0].Id - f.Start * kPinIdStride));

    int pastedLinks = 0;
    for (const auto& link : f.Graph.Links)
    {
        if (link.StartPinId == start->Outputs[0].Id)
        {
            ++pastedLinks;
            CHECK(link.EndPinId == print->Inputs[0].Id);
        }
    }
    CHECK(pastedLinks == 1);
    CHECK(f.Graph.Links.size() == 3);
}

TEST_CASE("Clipboard drops links to unselected nodes and anchors the top-left", "[graph][clipboard]")
{
    Fixture f;
    const auto result = PasteGraphSelection(f.Graph, CopyGraphSelection(f.Graph, {f.Print, f.Start}), {10.0f, 20.0f});
    REQUIRE(result.NodeIds.size() == 2);
    glm::vec2 minPos{1e9f};
    for (const int id : result.NodeIds) minPos = glm::min(minPos, f.Graph.FindNode(id)->Position);
    CHECK(minPos == glm::vec2(10.0f, 20.0f));

    Fixture g;
    const auto lone = PasteGraphSelection(g.Graph, CopyGraphSelection(g.Graph, {g.Print}), {0.0f, 0.0f});
    REQUIRE(lone.NodeIds.size() == 1);
    CHECK(g.Graph.Links.size() == 2);
}

TEST_CASE("Clipboard pastes across graphs and carries variables", "[graph][clipboard]")
{
    AnimationGraphData source;
    source.Variables.push_back(Variable{"Speed", PinType::Float});
    source.Nodes.push_back(CreateVariableGetNode(source.AllocateId(), "Speed", PinType::Float));
    const auto text = CopyGraphSelection(source, {source.Nodes[0].Id});

    AnimationGraphData target;
    target.NextId = 40;
    const auto result = PasteGraphSelection(target, text, {});
    REQUIRE(result.NodeIds.size() == 1);
    CHECK(result.NodeIds[0] == 40);
    REQUIRE(target.Variables.size() == 1);
    CHECK(target.Variables[0].Name == "Speed");

    PasteGraphSelection(target, text, {});
    CHECK(target.Variables.size() == 1);
}

TEST_CASE("Clipboard ignores foreign or malformed text", "[graph][clipboard]")
{
    AnimationGraphData graph;
    CHECK(PasteGraphSelection(graph, "hello world", {}).NodeIds.empty());
    CHECK(PasteGraphSelection(graph, "MoleHoleGraphClipboard: [unclosed", {}).NodeIds.empty());
    CHECK(PasteGraphSelection(graph, "Format: Other\nGraph: {}", {}).NodeIds.empty());
    CHECK(graph.Nodes.empty());
}
