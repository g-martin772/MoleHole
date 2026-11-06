#include <catch2/catch_test_macros.hpp>

import MoleHole;
import std;

using namespace MoleHole;
namespace fs = std::filesystem;

namespace
{
    constexpr const char* kSample = R"(local flight = require("lib/flight")
local zoom = require('lib/zoom')
-- local ghost = require("lib/ghost")

return {
    properties = {
        { name = "Speed", type = "float", unit = "m/s", default = 1000, min = 0, max = 1e15 },
        { name = "Offset", type = "vec3", default = vector.create(0, 1, 2) },
        { name = "Target", type = "entity" },
        { name = "Label", type = "string", default = "a, b" },
    },

    OnStart = function(self)
        events.on("ping", function() end)
    end,

    OnEvent = function(self, name, a)
        if name == "camera_switch" then
            events.emit("pong", a)
        elseif name == "ship_status" then
            self.name = "ignored"
        end
    end,

    OnTick = function(self, dt)
        events.emit("tick", dt)
    end,
}
)";

    fs::path SourceRoot()
    {
        return fs::path(MOLEHOLE_SOURCE_DIR);
    }

    struct TempTree
    {
        fs::path Root = fs::temp_directory_path() / "molehole_script_library_test";

        TempTree()
        {
            fs::remove_all(Root);
            fs::create_directories(Root / "scripts" / "lib");
            fs::create_directories(Root / "templates");
        }

        ~TempTree() { fs::remove_all(Root); }

        void Write(const fs::path& relative, const std::string& text) const { std::ofstream(Root / relative, std::ios::binary) << text; }
    };
}

TEST_CASE("Script source yields properties, callbacks, events and requires", "[application][script-library]")
{
    const auto entry = ParseScriptSource("sample", "scripts/sample.luau", kSample);
    CHECK_FALSE(entry.Library);
    REQUIRE(entry.Properties.size() == 4);
    CHECK(entry.Properties[0].Name == "Speed");
    CHECK(entry.Properties[0].Type == "float");
    CHECK(entry.Properties[0].Unit == "m/s");
    CHECK(entry.Properties[0].Default == "1000");
    CHECK(entry.Properties[0].Min == "0");
    CHECK(entry.Properties[0].Max == "1e15");
    CHECK(entry.Properties[1].Default == "vector.create(0, 1, 2)");
    CHECK(entry.Properties[2].Type == "entity");
    CHECK(entry.Properties[2].Default.empty());
    CHECK(entry.Properties[3].Default == "\"a, b\"");
    CHECK(entry.Callbacks == std::vector<std::string>{"OnStart", "OnEvent", "OnTick"});
    CHECK(entry.Emits == std::vector<std::string>{"pong", "tick"});
    CHECK(entry.Handles == std::vector<std::string>{"ping", "camera_switch", "ship_status"});
    CHECK(entry.Requires == std::vector<std::string>{"lib/flight", "lib/zoom"});
    CHECK(entry.Lines == 28);
}

TEST_CASE("Comments and names outside OnEvent do not count", "[application][script-library]")
{
    const auto entry = ParseScriptSource("lib/x", "x.luau", "-- events.emit(\"no\")\nlocal name = \"a\"\nif name == \"b\" then end\nreturn {}\n");
    CHECK(entry.Library);
    CHECK(entry.Emits.empty());
    CHECK(entry.Handles.empty());
    CHECK(entry.Properties.empty());
    CHECK(entry.Callbacks.empty());
}

TEST_CASE("Template script usage reads only Scripts entry names", "[application][script-library]")
{
    constexpr const char* yaml = R"(Entities:
  - Guid: 1
    Components:
      Metadata: {Guid: 1, Name: Ship}
      Scripts:
        - Name: ship_controller
          Props:
            Name: not_a_script
        - Name: "chase_camera"
  - Guid: 2
    Components:
      Metadata:
        Name: other
      Scripts:
      - Name: ray_gun # trailing
      Transform: {}
)";
    CHECK(ScriptNamesInTemplate(yaml) == std::vector<std::string>{"ship_controller", "chase_camera", "ray_gun"});
    CHECK(ScriptNamesInTemplate("Entities: []\n").empty());
}

TEST_CASE("Library build links requires, templates and inherited events", "[application][script-library]")
{
    const TempTree tree;
    tree.Write("scripts/a.luau", "local c = require(\"lib/c\")\nreturn { properties = { { name = \"P\", type = \"int\", default = 3 } }, OnTick = function(self, dt) end }\n");
    tree.Write("scripts/b.luau", "return { OnStart = function(self) end }\n");
    tree.Write("scripts/lib/c.luau", "local d = require(\"lib/d\")\nlocal c = {}\nevents.emit(\"from_c\")\nreturn c\n");
    tree.Write("scripts/lib/d.luau", "events.emit(\"from_d\")\nreturn {}\n");
    tree.Write("scripts/notes.txt", "x");
    tree.Write("templates/one.yaml", "      Scripts:\n        - Name: a\n");
    tree.Write("templates/two.yaml", "      Scripts:\n        - Name: a\n        - Name: b\n");

    const std::vector<fs::path> dirs{tree.Root / "scripts"};
    const auto entries = BuildScriptLibrary(dirs, tree.Root / "templates");
    REQUIRE(entries.size() == 4);
    CHECK(entries[0].Name == "a");
    CHECK(entries[1].Name == "b");
    CHECK(entries[2].Name == "lib/c");
    CHECK(entries[3].Name == "lib/d");
    CHECK(entries[2].Library);

    CHECK(entries[0].Templates == std::vector<std::string>{"one.yaml", "two.yaml"});
    CHECK(entries[1].Templates == std::vector<std::string>{"two.yaml"});
    CHECK(entries[2].RequiredBy == std::vector<std::string>{"a"});
    CHECK(entries[3].RequiredBy == std::vector<std::string>{"lib/c"});
    CHECK(entries[0].Emits == std::vector<std::string>{"from_c", "from_d"});
    CHECK(entries[1].Emits.empty());
    CHECK(entries[0].Properties.size() == 1);
}

TEST_CASE("Library build tolerates missing directories", "[application][script-library]")
{
    const std::vector<fs::path> dirs{fs::path("/nonexistent/scripts")};
    CHECK(BuildScriptLibrary(dirs, "/nonexistent/templates").empty());
}

TEST_CASE("Filter ranks names first and searches details", "[application][script-library]")
{
    std::vector<ScriptLibraryEntry> entries;
    entries.push_back(ParseScriptSource("chase_camera", "a", "return { properties = { { name = \"Distance\", type = \"float\" } } }\n"));
    entries.push_back(ParseScriptSource("ray_gun", "b", "events.emit(\"ray_hit\")\nreturn {}\n"));
    entries.push_back(ParseScriptSource("lib/orbit", "c", "return {}\n"));

    CHECK(FilterScriptLibrary(entries, "").size() == 3);
    CHECK(FilterScriptLibrary(entries, "  ").size() == 3);
    CHECK(FilterScriptLibrary(entries, "chase") == std::vector<std::size_t>{0});
    CHECK(FilterScriptLibrary(entries, "ray_hit") == std::vector<std::size_t>{1});
    CHECK(FilterScriptLibrary(entries, "distance") == std::vector<std::size_t>{0});
    CHECK(FilterScriptLibrary(entries, "zzz").empty());
}

TEST_CASE("Shipped scripts all parse and templates link to them", "[application][script-library]")
{
    const std::vector<fs::path> dirs{SourceRoot() / "scripts"};
    const auto entries = BuildScriptLibrary(dirs, SourceRoot() / "templates");
    REQUIRE_FALSE(entries.empty());
    const auto find = [&](const std::string_view name) -> const ScriptLibraryEntry*
    {
        const auto found = std::ranges::find(entries, name, &ScriptLibraryEntry::Name);
        return found == entries.end() ? nullptr : &*found;
    };

    for (const auto& entry : entries)
    {
        CAPTURE(entry.Name);
        CHECK(entry.Library == entry.Name.starts_with("lib/"));
        CHECK_FALSE(entry.Source.empty());
        for (const auto& property : entry.Properties)
        {
            CHECK_FALSE(property.Name.empty());
            CHECK(std::ranges::contains(std::array<std::string_view, 7>{"float", "int", "bool", "vec3", "color", "string", "entity"}, property.Type));
        }
        for (const auto& required : entry.Requires) CHECK(find(required) != nullptr);
    }

    const auto* ship = find("ship_controller");
    REQUIRE(ship);
    CHECK_FALSE(ship->Library);
    CHECK(ship->Properties.size() > 10);
    CHECK(std::ranges::contains(ship->Requires, "lib/flight"));
    CHECK(std::ranges::contains(ship->Templates, "fly-ship.yaml"));
    CHECK(std::ranges::contains(ship->Emits, "ship_status"));
    CHECK(std::ranges::contains(ship->Handles, "camera_mode"));

    const auto* flight = find("lib/flight");
    REQUIRE(flight);
    CHECK(flight->Library);
    CHECK(std::ranges::contains(flight->RequiredBy, "ship_controller"));
}
