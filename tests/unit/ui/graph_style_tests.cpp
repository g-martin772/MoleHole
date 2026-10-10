#include <catch2/catch_test_macros.hpp>

import MoleHole;
import glm;
import std;

using namespace MoleHole;

TEST_CASE("Every pin type has a distinct opaque colour", "[ui][graph]")
{
    constexpr PinType all[] = {PinType::Flow, PinType::Bool, PinType::Float, PinType::Int, PinType::Vec2,
                               PinType::Vec3, PinType::Vec4, PinType::String, PinType::Object};
    for (std::size_t i = 0; i < std::size(all); ++i)
    {
        CHECK(GraphPinColor(all[i]).a == 1.0f);
        for (std::size_t j = i + 1; j < std::size(all); ++j) CHECK(GraphPinColor(all[i]) != GraphPinColor(all[j]));
    }
}

TEST_CASE("Flow wires are thicker than data wires", "[ui][graph]")
{
    CHECK(GraphLinkThickness(PinType::Flow) > GraphLinkThickness(PinType::Float));
}

TEST_CASE("Node categories map to different header colours", "[ui][graph]")
{
    CHECK(GraphNodeColor(NodeType::Event) != GraphNodeColor(NodeType::Function));
    CHECK(GraphNodeColor(NodeType::Reroute) != GraphNodeColor(NodeType::Control));
}
