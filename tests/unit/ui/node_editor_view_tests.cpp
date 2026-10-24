#include <catch2/catch_test_macros.hpp>

import MoleHole;
import std;

using namespace MoleHole;

namespace
{
    constexpr std::string_view kView =
        R"({"nodes":{"node:1":{"location":{"x":-140,"y":438}}},"selection":null,"view":{"scroll":{"x":-10.5,"y":20},)"
        R"("visible_rect":{"max":{"x":900,"y":400},"min":{"x":-100,"y":-50}},"zoom":0.75}})";
}

TEST_CASE("Node editor view sanity rejects what the library cannot have produced", "[ui][nodeeditor]")
{
    CHECK(NodeEditorViewSane(kView));
    CHECK_FALSE(NodeEditorViewSane(R"({"view":{"scroll":{"x":0,"y":0}}})"));
    CHECK_FALSE(NodeEditorViewSane(R"({"view":{"zoom":0.0627}})"));
    CHECK_FALSE(NodeEditorViewSane(R"({"view":{"zoom":1,"scroll":{"x":5e9,"y":0}}})"));
    CHECK_FALSE(NodeEditorViewSane(R"({"view":{"zoom":-nan}})"));
    CHECK(NodeEditorViewSane(R"({"view":{"zoom":0.1}})"));
}

TEST_CASE("Stripping the visible rect keeps the rest of the view", "[ui][nodeeditor]")
{
    const auto stripped = StripVisibleRect(std::string(kView));
    CHECK(stripped.find("visible_rect") == std::string::npos);
    CHECK(stripped.find("\"zoom\":0.75") != std::string::npos);
    CHECK(stripped.find("\"scroll\":{\"x\":-10.5,\"y\":20}") != std::string::npos);
    CHECK(NodeEditorViewSane(stripped));

    CHECK(StripVisibleRect(R"({"view":{"visible_rect":{"a":{"b":1}},"zoom":1}})") == R"({"view":{"zoom":1}})");
    CHECK(StripVisibleRect(R"({"view":{"zoom":1,"visible_rect":{"a":1}}})") == R"({"view":{"zoom":1}})");
    CHECK(StripVisibleRect("{}") == "{}");
}
