#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
namespace fs = std::filesystem;

namespace
{
    constexpr const char* kSceneYaml = R"(Scene: Lib
SchemaVersion: 2
Entities:
  - Guid: 5
    Components:
      Metadata: {Guid: 5, Name: Probe, TypeTag: Entity}
      Transform: {Position: [0, 0, 0]}
      Scripts:
        - Name: lib_state_probe
)";

    constexpr const char* kProbe = R"(
local pid = require("lib/pid")
local input_map = require("lib/input_map")
local sequencer = require("lib/sequencer")

return {
    OnStart = function(self)
        self.pid = pid.new_vec({ kp = 3, ki = 1, kd = 0.5, out_min = -2, out_max = 2, d_tau = 0.05 })
        self.pos = vector.create(0, 0, 0)
        self.map = input_map.new({
            axes = { throttle = { action = "a", deadzone = 0.1, expo = 0.3, slew = 3 } },
            toggles = { lights = { action = "t" } },
            repeats = { fire = { action = "f", delay = 0.2, rate = 8 } },
            sticks = { move = { x = "a", y = "b", deadzone = 0.2 } },
        })
        self.seq = sequencer.new()
        sequencer.add(self.seq, sequencer.sequence({
            sequencer.call("bump", 1),
            sequencer.tween("level", 0, 10, 0.5, "smoothstep"),
            sequencer.every(0.1, "bump", 2),
        }))
        self.bumps, self.level = 0, 0
    end,
    OnTick = function(self, dt)
        self.n = (self.n or 0) + 1
        local n = self.n
        local source = {
            axis = function(name) return name == "a" and math.sin(n / 5) or math.cos(n / 9) end,
            held = function() return n % 30 < 20 end,
            pressed = function(name) return (name == "t" and n % 17 == 0) or (name == "f" and n % 30 == 0) end,
        }
        input_map.update(self.map, dt, source)
        local sx, sy = input_map.stick(self.map, "move")
        self.pos += pid.step_vec(self.pid, vector.create(sx, sy, 1), self.pos, dt) * dt
        sequencer.update(self.seq, {
            bump = function(by) self.bumps += by end,
            level = function(value) self.level = value end,
        }, dt)
        scene.set(self.entity, "Transform", "Position", self.pos)
    end,
}
)";

    struct Rig
    {
        fs::path Root = fs::temp_directory_path() / "molehole_script_lib_tests";
        std::unique_ptr<AssetDirectories> Assets;
        std::shared_ptr<ScriptSimulationModule> Module;
        std::unique_ptr<SimulationRunner> Runner;

        Rig()
        {
            fs::remove_all(Root);
            fs::create_directories(Root);
            std::ofstream(Root / "lib_state_probe.luau") << kProbe;
            AssetOptions options;
            options.Kinds[kScriptAssetKind] = AssetKindOptions{{Root, fs::path(MOLEHOLE_SOURCE_DIR) / "scripts"}, {".lua", ".luau"}};
            Assets = std::make_unique<AssetDirectories>(options);
            RegisterBaseComponents();
            RegisterScriptComponents();
            Scene scene;
            scene.DeserializeFromYaml(kSceneYaml);
            Module = std::make_shared<ScriptSimulationModule>(*Assets);
            Runner = std::make_unique<SimulationRunner>(std::move(scene), Module);
            Module->SetRandom(Runner->Random());
            Module->SetInput(Runner->Input());
        }

        ~Rig() { fs::remove_all(Root); }
    };
}

TEST_CASE("Script library state survives SaveState and LoadState", "[scripting][lib][state]")
{
    Rig first;
    first.Runner->StepAndWait(40);
    REQUIRE(first.Module->TakeErrors().empty());
    const auto saved = first.Module->SaveState();
    REQUIRE(saved.find("kp") != std::string::npos);
    REQUIRE(saved.find("level") != std::string::npos);

    Rig second;
    second.Module->LoadState(saved);
    CHECK(second.Module->SaveState() == saved);

    first.Runner->StepAndWait(120);
    second.Runner->StepAndWait(120);
    CHECK(first.Module->TakeErrors().empty());
    CHECK(second.Module->TakeErrors().empty());
    CHECK(second.Module->SaveState() == first.Module->SaveState());
    CHECK(second.Module->SaveState() != saved);
}

TEST_CASE("Script library runs are deterministic", "[scripting][lib][state]")
{
    Rig a;
    Rig b;
    a.Runner->StepAndWait(200);
    b.Runner->StepAndWait(200);
    CHECK(a.Module->TakeErrors().empty());
    CHECK(a.Module->SaveState() == b.Module->SaveState());
}

namespace
{
    std::vector<std::string> Forbidden(const std::string& text)
    {
        static const std::vector<std::pair<std::string, std::regex>> rules = {
            {"os", std::regex(R"((^|[^.\w])os\b)")},
            {"math.randomseed", std::regex(R"(\bmath\s*\.\s*randomseed\b)")},
            {"Random.new", std::regex(R"(\bRandom\s*\.\s*new\b)")},
            {"tick()", std::regex(R"((^|[^.\w:])tick\s*\()")},
            {"DateTime", std::regex(R"(\bDateTime\b)")},
        };
        std::vector<std::string> found;
        std::istringstream lines(text);
        std::string line;
        int number = 0;
        while (std::getline(lines, line))
        {
            ++number;
            const auto comment = line.find("--");
            if (comment != std::string::npos) line.resize(comment);
            for (const auto& [name, pattern] : rules)
            {
                if (std::regex_search(line, pattern)) found.push_back(std::format("{}: {}", number, name));
            }
        }
        return found;
    }
}

TEST_CASE("The lint flags wall clocks and unseeded randomness", "[scripting][lint]")
{
    CHECK(Forbidden("local t = os.clock()").size() == 1);
    CHECK(Forbidden("local t = os.time()").size() == 1);
    CHECK(Forbidden("math.randomseed(5)").size() == 1);
    CHECK(Forbidden("local r = Random.new()").size() == 1);
    CHECK(Forbidden("local t = tick()").size() == 1);
    CHECK(Forbidden("local t = os[\"clock\"]()").size() >= 1);
    CHECK(Forbidden("local x = math.random()").empty());
    CHECK(Forbidden("local t = time.sim_time()").empty());
    CHECK(Forbidden("self.os = 1; local y = sim.ticks()").empty());
    CHECK(Forbidden("return { OnTick = function(self, dt) end } -- os.clock").empty());
}

TEST_CASE("Shipped scripts never read a wall clock or reseed randomness", "[scripting][lint]")
{
    const fs::path scripts = fs::path(MOLEHOLE_SOURCE_DIR) / "scripts";
    std::size_t checked = 0;
    for (const auto& entry : fs::recursive_directory_iterator(scripts))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".luau") continue;
        std::ifstream stream(entry.path(), std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        INFO(entry.path().string());
        CHECK(Forbidden(text).empty());
        ++checked;
    }
    CHECK(checked > 0);
}
