#include <catch2/catch_test_macros.hpp>

import MoleHole;
import glm;
import std;

using namespace MoleHole;

namespace
{
    int AddNode(AnimationGraphData& graph)
    {
        graph.Nodes.push_back(CreateBranchNode(graph.AllocateId()));
        return graph.Nodes.back().Id;
    }
}

TEST_CASE("GraphHistory undoes and redoes structural edits", "[graph][history]")
{
    AnimationGraphData graph;
    GraphHistory history;
    history.Reset(graph);
    CHECK_FALSE(history.CanUndo());

    AddNode(graph);
    CHECK(history.Record(graph));
    AddNode(graph);
    CHECK(history.Record(graph));
    REQUIRE(graph.Nodes.size() == 2);

    CHECK(history.Undo(graph));
    CHECK(graph.Nodes.size() == 1);
    CHECK(history.Undo(graph));
    CHECK(graph.Nodes.empty());
    CHECK_FALSE(history.Undo(graph));

    CHECK(history.Redo(graph));
    CHECK(history.Redo(graph));
    CHECK(graph.Nodes.size() == 2);
    CHECK_FALSE(history.Redo(graph));
}

TEST_CASE("GraphHistory ignores unchanged graphs and drops redo after a new edit", "[graph][history]")
{
    AnimationGraphData graph;
    GraphHistory history;
    history.Reset(graph);
    CHECK_FALSE(history.Record(graph));

    AddNode(graph);
    history.Record(graph);
    history.Undo(graph);
    CHECK(history.CanRedo());
    AddNode(graph);
    history.Record(graph);
    CHECK_FALSE(history.CanRedo());
}

TEST_CASE("GraphHistory coalesces keyed edits until sealed", "[graph][history]")
{
    AnimationGraphData graph;
    const int id = AddNode(graph);
    GraphHistory history;
    history.Reset(graph);

    for (int i = 1; i <= 5; ++i)
    {
        graph.FindNode(id)->Position = glm::vec2(static_cast<float>(i), 0.0f);
        history.Record(graph, "move");
    }
    CHECK(history.Size() == 2);

    history.Seal();
    graph.FindNode(id)->Position = glm::vec2(50.0f, 0.0f);
    history.Record(graph, "move");
    CHECK(history.Size() == 3);

    history.Undo(graph);
    CHECK(graph.FindNode(id)->Position.x == 5.0f);
    history.Undo(graph);
    CHECK(graph.FindNode(id)->Position.x == 0.0f);
}

TEST_CASE("GraphHistory caps the number of snapshots", "[graph][history]")
{
    AnimationGraphData graph;
    GraphHistory history(4);
    history.Reset(graph);
    for (int i = 0; i < 10; ++i)
    {
        AddNode(graph);
        history.Record(graph);
    }
    CHECK(history.Size() == 4);
    while (history.Undo(graph)) {}
    CHECK(graph.Nodes.size() == 7);
}
