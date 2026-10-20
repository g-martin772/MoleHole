#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    struct World
    {
        Scene Space{"RelativityNodes"};
        HoleFrame Frame{};
        std::uint64_t Body{0};
    };

    World MakeWorld(const double spin)
    {
        RegisterComponents();
        World world;
        const BlackHoleComponent hole{.Mass = 1e6 * Units::kSolarMass, .Spin = spin};
        const auto holeEntity = world.Space.CreateEntity("Hole");
        world.Space.Registry().emplace<TransformComponent>(holeEntity);
        world.Space.Registry().emplace<BlackHoleComponent>(holeEntity, hole);
        world.Frame = MakeHoleFrame(hole, {0.0, 0.0, 0.0});
        const auto body = world.Space.CreateEntity("Probe");
        world.Space.Registry().emplace<TransformComponent>(
            body, TransformComponent{.Position = glm::dvec3(30.0, 5.0, 0.0) * world.Frame.LengthM});
        world.Space.Registry().emplace<GeodesicBodyComponent>(body, GeodesicBodyComponent{.Launch = GeodesicLaunch::Raindrop});
        world.Body = world.Space.GuidOf(body);
        return world;
    }

    int AddConstant(AnimationGraphData& graph, const Value& value, const PinType type)
    {
        Node node = CreateConstantNode(graph.AllocateId(), type);
        node.ConstantValue = value;
        const int out = node.Outputs[0].Id;
        graph.Nodes.push_back(std::move(node));
        return out;
    }

    void Connect(AnimationGraphData& graph, const int from, const int to) { graph.Links.push_back(Link{graph.AllocateId(), from, to}); }

    // Collects diagnostics and evaluated pin values of a run
    class Recorder final : public ITraceSink
    {
    public:
        std::vector<std::string> Messages;
        std::map<int, Value> Pins;
        void OnDiagnostic(std::string_view, int, TraceSeverity, std::string_view message) override { Messages.emplace_back(message); }
        void OnPinEvaluated(std::string_view, const int pinId, const Value& value) override { Pins[pinId] = value; }
    };

    // Start -> Launch Circular Orbit(body, radius, azimuth, retrograde)
    AnimationGraphData LaunchGraph(const std::uint64_t body, const float radius, const float azimuth, const bool retrograde)
    {
        AnimationGraphData graph;
        Node start = CreateStartEventNode(graph.AllocateId());
        const int startOut = start.Outputs[0].Id;
        graph.Nodes.push_back(start);
        Node getter = CreateGetterNode(graph.AllocateId(), "GeodesicBody");
        getter.TargetGuid = body;
        const int entity = getter.Outputs[0].Id;
        graph.Nodes.push_back(getter);
        Node launch = CreateLaunchOrbitNode(graph.AllocateId());
        const auto in = launch.Inputs;
        graph.Nodes.push_back(launch);
        Connect(graph, startOut, in[0].Id);
        Connect(graph, entity, in[1].Id);
        Connect(graph, AddConstant(graph, radius, PinType::Float), in[2].Id);
        Connect(graph, AddConstant(graph, azimuth, PinType::Float), in[3].Id);
        Connect(graph, AddConstant(graph, retrograde, PinType::Bool), in[4].Id);
        return graph;
    }
}

TEST_CASE("Launch Circular Orbit puts a geodesic body on a circular orbit", "[simulation][animation][relativity]")
{
    for (const bool retrograde : {false, true})
    {
        INFO("retrograde " << retrograde);
        auto world = MakeWorld(0.7);
        GeodesicSimulationModule module;
        module.OnInit(world.Space);

        const auto graph = LaunchGraph(world.Body, 12.0f, 1.0f, retrograde);
        GraphExecutor executor(graph);
        Recorder recorder;
        executor.SetTraceSink(&recorder);
        auto writes = executor.ExecuteStartEvent(world.Space);
        for (const auto& message : recorder.Messages) UNSCOPED_INFO("diagnostic: " << message);
        CHECK(writes.size() == 3);
        for (auto& write : writes) write(world.Space);
        const auto entity = world.Space.FindByGuid(world.Body);
        CHECK(world.Space.Registry().get<GeodesicBodyComponent>(entity).Launch == GeodesicLaunch::Orbiting);
        CHECK(world.Space.Registry().get<GeodesicBodyComponent>(entity).Retrograde == retrograde);

        // The simulation sees the moved body and launches it from there
        module.OnTick(world.Space, 1.0f);
        const auto& body = world.Space.Registry().get<GeodesicBodyComponent>(entity);
        REQUIRE(body.Status == GeodesicBodyStatus::FreeFall);
        CHECK(body.State.R == Approx(12.0).epsilon(1e-6));
        CHECK(body.State.Theta == Approx(std::numbers::pi / 2.0).margin(1e-6));
        CHECK((body.State.AngularMomentum < 0.0) == retrograde);
        const auto direction = retrograde ? OrbitDirection::Retrograde : OrbitDirection::Prograde;
        CHECK(body.ClockRate == Approx(std::sqrt(CircularOrbitNorm(world.Frame.Metric, 12.0, direction))).epsilon(1e-6));
    }
}

TEST_CASE("Launch Circular Orbit reports what it cannot do", "[simulation][animation][relativity]")
{
    auto world = MakeWorld(0.7);
    const auto expectFailure = [&](const AnimationGraphData& graph, const std::string& fragment)
    {
        GraphExecutor executor(graph);
        Recorder recorder;
        executor.SetTraceSink(&recorder);
        const auto writes = executor.ExecuteStartEvent(world.Space);
        CHECK(writes.empty());
        INFO(fragment);
        CHECK(std::ranges::any_of(recorder.Messages, [&](const std::string& message) { return message.contains(fragment); }));
    };
    // Inside the photon orbit there is no circular orbit
    expectFailure(LaunchGraph(world.Body, 1.5f, 0.0f, false), "No circular orbit");
    // An entity without a Geodesic Body
    const auto plain = world.Space.CreateEntity("Plain");
    world.Space.Registry().emplace<TransformComponent>(plain);
    expectFailure(LaunchGraph(world.Space.GuidOf(plain), 10.0f, 0.0f, false), "no Geodesic Body");
}

TEST_CASE("Clock Rate gives dτ/dt of the chosen observer", "[simulation][animation][relativity]")
{
    auto world = MakeWorld(0.0);
    const auto rate = [&](const glm::vec3& positionM, const int observer)
    {
        // Start -> Print(Clock Rate(position, observer)); the value is read from the trace
        AnimationGraphData graph;
        Node start = CreateStartEventNode(graph.AllocateId());
        const int startOut = start.Outputs[0].Id;
        graph.Nodes.push_back(start);
        Node math = CreateMathNode(graph.AllocateId(), NodeSubType::ClockRate);
        const auto in = math.Inputs;
        const int out = math.Outputs[0].Id;
        graph.Nodes.push_back(math);
        Node print = CreatePrintNode(graph.AllocateId());
        const auto printIn = print.Inputs;
        graph.Nodes.push_back(print);
        Connect(graph, startOut, printIn[0].Id);
        Connect(graph, out, printIn[1].Id);
        Connect(graph, AddConstant(graph, positionM, PinType::Vec3), in[0].Id);
        Connect(graph, AddConstant(graph, observer, PinType::Int), in[1].Id);
        GraphExecutor executor(graph, [](std::string) {});
        Recorder recorder;
        executor.SetTraceSink(&recorder);
        (void)executor.ExecuteStartEvent(world.Space);
        return recorder.Pins.contains(out) ? recorder.Pins[out] : Value{};
    };
    const float lengthM = static_cast<float>(world.Frame.LengthM);
    // Schwarzschild, static at r = 10 M: sqrt(1 - 2M/r)
    const Value stillValue = rate(glm::vec3(10.0f * lengthM, 0.0f, 0.0f), 1);
    REQUIRE(std::holds_alternative<float>(stillValue));
    CHECK(std::get<float>(stillValue) == Approx(std::sqrt(0.8)).epsilon(1e-5));
    // Orbiting in the equatorial plane: sqrt(1 - 3M/r)
    CHECK(std::get<float>(rate(glm::vec3(10.0f * lengthM, 0.0f, 0.0f), 2)) == Approx(std::sqrt(0.7)).epsilon(1e-5));
    // Inside the photon orbit no orbiting observer exists
    CHECK(std::get<float>(rate(glm::vec3(2.5f * lengthM, 0.0f, 0.0f), 2)) == 0.0f);
    CHECK(ObserverClockRateAt(world.Space, glm::dvec3(10.0, 0.0, 0.0) * world.Frame.LengthM, 1) == Approx(std::sqrt(0.8)));
}
