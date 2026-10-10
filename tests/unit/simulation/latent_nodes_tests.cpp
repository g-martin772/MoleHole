#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;
using GPP::Scene;

namespace
{
    struct Gb
    {
        AnimationGraphData G;

        int Add(Node node)
        {
            G.Nodes.push_back(std::move(node));
            return G.Nodes.back().Id;
        }
        int Id() { return G.AllocateId(); }
        int Out(const int node, const std::size_t pin = 0) { return G.FindNode(node)->Outputs[pin].Id; }
        int In(const int node, const std::size_t pin) { return G.FindNode(node)->Inputs[pin].Id; }
        void Link(const int outPin, const int inPin) { G.Links.push_back(MoleHole::Link{G.AllocateId(), outPin, inPin}); }
        void Flow(const int from, const int to, const std::size_t fromPin = 0, const std::size_t toPin = 0)
        {
            Link(Out(from, fromPin), In(to, toPin));
        }
        void Data(const int from, const std::size_t fromPin, const int to, const std::size_t toPin)
        {
            Link(Out(from, fromPin), In(to, toPin));
        }
        int Const(const Value& value, const PinType type)
        {
            Node node = CreateConstantNode(Id(), type);
            node.ConstantValue = value;
            return Add(std::move(node));
        }
        int Text(const std::string& text) { return Const(text, PinType::String); }
        int Num(const float value) { return Const(value, PinType::Float); }
        // Prints a constant string after the given flow output and returns the print node.
        int Say(const int flowFrom, const std::string& text, const std::size_t flowPin = 0)
        {
            const int print = Add(CreatePrintNode(Id()));
            Flow(flowFrom, print, flowPin);
            Data(Text(text), 0, print, 1);
            return print;
        }
        int Delay(const int flowFrom, const float seconds, const std::size_t flowPin = 0)
        {
            const int delay = Add(CreateDelayNode(Id()));
            Flow(flowFrom, delay, flowPin);
            Data(Num(seconds), 0, delay, 1);
            return delay;
        }
    };

    struct Rig
    {
        SceneGraphs Graphs;
        ScriptCache Cache;
        Scene World{"Latent"};
        std::vector<std::string> Prints;
        std::set<std::string> Diagnostics;
        std::unique_ptr<ScriptRuntime> Runtime;
        std::vector<std::size_t> PrintsAfterTick;

        class Sink final : public ITraceSink
        {
        public:
            std::set<std::string>* Out{nullptr};
            void OnDiagnostic(std::string_view graph, int nodeId, TraceSeverity severity, std::string_view message) override
            {
                Out->insert(std::format("{}/{}/{}/{}", graph, nodeId, static_cast<int>(severity), message));
            }
        } Trace;

        explicit Rig(AnimationGraphData graph)
        {
            Graphs.Items.push_back(NamedGraph{.Name = "Main", .Graph = std::move(graph)});
        }

        void Build()
        {
            Runtime = std::make_unique<ScriptRuntime>(Graphs, Cache, [this](std::string text) { Prints.push_back(std::move(text)); });
            REQUIRE(Runtime->Ok());
            Trace.Out = &Diagnostics;
            Runtime->SetTraceSink(&Trace);
        }

        void Apply(PendingWrites writes) { for (auto& write : writes) write(World); }

        void Start()
        {
            if (!Runtime) Build();
            Apply(Runtime->ExecuteStartEvent(World));
            PrintsAfterTick.push_back(Prints.size());
        }

        void Tick(const int count, const float dt = 0.25f)
        {
            for (int i = 0; i < count; ++i)
            {
                Apply(Runtime->ExecuteTickEvent(World, dt));
                PrintsAfterTick.push_back(Prints.size());
            }
        }

        // Index of the tick (0 = start) after which the text first appeared, or -1.
        int FirstSeen(const std::string& text) const
        {
            const auto it = std::ranges::find(Prints, text);
            if (it == Prints.end()) return -1;
            const auto position = static_cast<std::size_t>(it - Prints.begin());
            for (std::size_t tick = 0; tick < PrintsAfterTick.size(); ++tick)
            {
                if (PrintsAfterTick[tick] > position) return static_cast<int>(tick);
            }
            return -1;
        }

        bool Diagnosed(const std::string& fragment) const
        {
            return std::ranges::any_of(Diagnostics, [&](const std::string& d) { return d.find(fragment) != std::string::npos; });
        }
    };

    int Start(Gb& g) { return g.Add(CreateStartEventNode(g.Id())); }
    int Tick(Gb& g) { return g.Add(CreateTickEventNode(g.Id())); }
}

TEST_CASE("Delay resumes its flow once the simulated clock passes the duration", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    const int before = g.Say(start, "before");
    const int delay = g.Delay(before, 1.0f);
    g.Say(delay, "after");
    Rig rig(std::move(g.G));
    rig.Start();
    CHECK(rig.Prints == std::vector<std::string>{"before"});
    rig.Tick(3);
    CHECK(rig.Prints.size() == 1);
    rig.Tick(1);
    CHECK(rig.Prints == std::vector<std::string>{"before", "after"});
    CHECK(rig.FirstSeen("after") == 4);
    CHECK(rig.Runtime->ActiveTasks() == 0);
}

TEST_CASE("A Delay on Tick spawns an independent wait every tick", "[latent][runtime]")
{
    Gb g;
    const int tick = Tick(g);
    const int delay = g.Delay(tick, 0.5f);
    g.Say(delay, "fired");
    Rig rig(std::move(g.G));
    rig.Start();
    rig.Tick(6);
    CHECK(std::ranges::count(rig.Prints, "fired") == 4);
    CHECK(rig.Runtime->ActiveTasks() == 2);
}

TEST_CASE("Sequence runs its branches in order and a latent branch does not hold back the next", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    const int seq = g.Add(CreateSequenceNode(g.Id(), 3));
    g.Flow(start, seq);
    const int slow = g.Delay(seq, 0.5f, 0);
    g.Say(slow, "slow");
    g.Say(seq, "second", 1);
    g.Say(seq, "third", 2);
    Rig rig(std::move(g.G));
    rig.Start();
    CHECK(rig.Prints == std::vector<std::string>{"second", "third"});
    rig.Tick(2);
    CHECK(rig.Prints == std::vector<std::string>{"second", "third", "slow"});
}

TEST_CASE("Wait Until polls its condition and Wait For Event wakes on a custom event", "[latent][runtime]")
{
    Gb g;
    g.G.Variables.push_back(Variable{"ready", PinType::Bool, false});
    const int start = Start(g);
    const int seq = g.Add(CreateSequenceNode(g.Id(), 3));
    g.Flow(start, seq);

    const int wait = g.Add(CreateWaitUntilNode(g.Id()));
    g.Flow(seq, wait, 0);
    const int get = g.Add(CreateVariableGetNode(g.Id(), "ready", PinType::Bool));
    g.Data(get, 0, wait, 1);
    g.Say(wait, "ready!");

    const int eventWait = g.Add(CreateWaitForEventNode(g.Id(), "go"));
    g.Flow(seq, eventWait, 1);
    g.Say(eventWait, "heard go");

    const int delay = g.Delay(seq, 0.5f, 2);
    const int set = g.Add(CreateVariableSetNode(g.Id(), "ready", PinType::Bool));
    g.Flow(delay, set);
    g.Data(g.Const(true, PinType::Bool), 0, set, 1);
    const int call = g.Add(CreateCallEventNode(g.Id(), "go"));
    g.Flow(set, call);

    const int define = g.Add(CreateCustomEventNode(g.Id(), "go"));
    g.Say(define, "handler");

    Rig rig(std::move(g.G));
    rig.Start();
    CHECK(rig.Prints.empty());
    rig.Tick(1);
    CHECK(rig.Prints.empty());
    rig.Tick(1);
    CHECK(rig.FirstSeen("handler") == 2);
    CHECK(rig.FirstSeen("heard go") == 2);
    CHECK(rig.FirstSeen("ready!") == -1);
    rig.Tick(1);
    CHECK(rig.FirstSeen("ready!") == 3);
    CHECK(rig.Runtime->ActiveTasks() == 0);
}

TEST_CASE("Interpolate drives a value over time with easing and then completes", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    const int tween = g.Add(CreateInterpolateNode(g.Id(), PinType::Float, "linear"));
    g.Flow(start, tween);
    g.Data(g.Num(0.0f), 0, tween, 1);
    g.Data(g.Num(10.0f), 0, tween, 2);
    g.Data(g.Num(1.0f), 0, tween, 3);
    const int print = g.Add(CreatePrintNode(g.Id()));
    g.Flow(tween, print, 0);
    g.Data(tween, 1, print, 1);
    g.Say(tween, "done", 2);
    Rig rig(std::move(g.G));
    rig.Start();
    rig.Tick(5);
    REQUIRE(rig.Prints.size() == 6);
    const std::array expected{0.0f, 2.5f, 5.0f, 7.5f, 10.0f};
    for (std::size_t i = 0; i < expected.size(); ++i) CHECK(std::stof(rig.Prints[i]) == expected[i]);
    CHECK(rig.Prints[5] == "done");
    CHECK(rig.FirstSeen("done") == 4);
}

TEST_CASE("Interpolate eases and handles vectors and instant durations", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    const int tween = g.Add(CreateInterpolateNode(g.Id(), PinType::Vec3, "easeIn"));
    g.Flow(start, tween);
    g.Data(g.Const(glm::vec3(0.0f), PinType::Vec3), 0, tween, 1);
    g.Data(g.Const(glm::vec3(8.0f, 0.0f, 0.0f), PinType::Vec3), 0, tween, 2);
    g.Data(g.Num(1.0f), 0, tween, 3);
    const int print = g.Add(CreatePrintNode(g.Id()));
    g.Flow(tween, print, 0);
    g.Data(tween, 1, print, 1);

    const int instant = g.Add(CreateInterpolateNode(g.Id(), PinType::Float, "linear"));
    g.Flow(start, instant);
    g.Data(g.Num(1.0f), 0, instant, 1);
    g.Data(g.Num(3.0f), 0, instant, 2);
    g.Data(g.Num(0.0f), 0, instant, 3);
    const int instantPrint = g.Add(CreatePrintNode(g.Id()));
    g.Flow(instant, instantPrint, 0);
    g.Data(instant, 1, instantPrint, 1);

    Rig rig(std::move(g.G));
    rig.Start();
    rig.Tick(4);
    REQUIRE(rig.Prints.size() == 6);
    CHECK(rig.Prints[0] == "(0.000000, 0.000000, 0.000000)");
    CHECK(rig.Prints[1] == "3.000000");
    CHECK(rig.Prints[2] == "(0.500000, 0.000000, 0.000000)");
    CHECK(rig.Prints[3] == "(2.000000, 0.000000, 0.000000)");
    CHECK(rig.Prints[4] == "(4.500000, 0.000000, 0.000000)");
    CHECK(rig.Prints[5] == "(8.000000, 0.000000, 0.000000)");
}

TEST_CASE("Do Once, Gate and Switch route the flow", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    const int tick = Tick(g);

    const int once = g.Add(CreateDoOnceNode(g.Id()));
    g.Flow(tick, once);
    g.Say(once, "once");

    const int gate = g.Add(CreateGateNode(g.Id()));
    g.Flow(start, gate, 0, 2);
    g.Flow(tick, gate, 0, 0);
    g.Say(gate, "gate");
    const int reopen = g.Delay(start, 0.5f);
    g.Flow(reopen, gate, 0, 1);

    const int pick = g.Add(CreateSwitchNode(g.Id(), 3));
    g.Flow(start, pick);
    g.Data(g.Const(1, PinType::Int), 0, pick, 1);
    g.Say(pick, "case0", 0);
    g.Say(pick, "case1", 1);
    g.Say(pick, "default", 3);

    Rig rig(std::move(g.G));
    rig.Start();
    CHECK(rig.Prints == std::vector<std::string>{"case1"});
    rig.Tick(4);
    CHECK(std::ranges::count(rig.Prints, "once") == 1);
    CHECK(std::ranges::count(rig.Prints, "gate") == 3);
    CHECK(rig.FirstSeen("gate") == 2);
}

TEST_CASE("Switch falls back to the default output", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    const int pick = g.Add(CreateSwitchNode(g.Id(), 2));
    g.Flow(start, pick);
    g.Data(g.Const(9, PinType::Int), 0, pick, 1);
    g.Say(pick, "case0", 0);
    g.Say(pick, "default", 2);
    Rig rig(std::move(g.G));
    rig.Start();
    CHECK(rig.Prints == std::vector<std::string>{"default"});
}

TEST_CASE("For Each Entity visits every entity with the component", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    const int each = g.Add(CreateForEachEntityNode(g.Id(), "Sphere"));
    g.Flow(start, each);
    const int print = g.Add(CreatePrintNode(g.Id()));
    g.Flow(each, print, 0);
    g.Data(each, 2, print, 1);
    g.Say(each, "end", 3);
    Rig rig(std::move(g.G));
    GPP::RegisterBaseComponents();
    RegisterComponents();
    for (int i = 0; i < 3; ++i)
    {
        const auto entity = rig.World.CreateEntity("S" + std::to_string(i));
        rig.World.Registry().emplace<SphereComponent>(entity);
    }
    rig.World.Registry().emplace<GPP::TransformComponent>(rig.World.CreateEntity("Plain"));
    rig.Start();
    REQUIRE(rig.Prints.size() == 4);
    CHECK(rig.Prints[0] == "0");
    CHECK(rig.Prints[2] == "2");
    CHECK(rig.Prints[3] == "end");
}

TEST_CASE("While repeats while its condition holds and is capped", "[latent][runtime]")
{
    Gb g;
    g.G.Variables.push_back(Variable{"n", PinType::Float, 0.0f});
    const int start = Start(g);
    const int loop = g.Add(CreateWhileNode(g.Id()));
    g.Flow(start, loop);
    const int get = g.Add(CreateVariableGetNode(g.Id(), "n", PinType::Float));
    const int less = g.Add(CreateMathNode(g.Id(), NodeSubType::Sub));
    g.Data(g.Num(3.0f), 0, less, 0);
    g.Data(get, 0, less, 1);
    const int check = g.Add(CreateBranchNode(g.Id()));
    (void)check;
    // Condition is a bool; compare through a constant true that the body turns off.
    g.G.Variables.push_back(Variable{"go", PinType::Bool, true});
    const int go = g.Add(CreateVariableGetNode(g.Id(), "go", PinType::Bool));
    g.Data(go, 0, loop, 1);
    const int bump = g.Add(CreateVariableSetNode(g.Id(), "n", PinType::Float));
    g.Flow(loop, bump, 0);
    const int plus = g.Add(CreateMathNode(g.Id(), NodeSubType::Add));
    g.Data(get, 0, plus, 0);
    g.Data(g.Num(1.0f), 0, plus, 1);
    g.Data(plus, 0, bump, 1);
    const int stop = g.Add(CreateVariableSetNode(g.Id(), "go", PinType::Bool));
    g.Flow(bump, stop);
    g.Data(g.Const(false, PinType::Bool), 0, stop, 1);
    g.Say(loop, "after", 1);
    Rig rig(std::move(g.G));
    rig.Start();
    CHECK(rig.Prints == std::vector<std::string>{"after"});

    Gb endless;
    const int s2 = Start(endless);
    const int forever = endless.Add(CreateWhileNode(endless.Id()));
    endless.Flow(s2, forever);
    endless.Data(endless.Const(true, PinType::Bool), 0, forever, 1);
    endless.Say(forever, "never", 1);
    Rig capped(std::move(endless.G));
    capped.Start();
    CHECK(std::ranges::count(capped.Prints, "never") == 1);
    CHECK(capped.Diagnosed("iteration cap"));
}

TEST_CASE("Custom events carry parameters to their handler", "[latent][runtime]")
{
    Gb g;
    const int define = g.Add(CreateCustomEventNode(g.Id(), "shout", {EventParam{0, "Amount", PinType::Float}}));
    const int print = g.Add(CreatePrintNode(g.Id()));
    g.Flow(define, print);
    g.Data(define, 1, print, 1);

    const int start = Start(g);
    const int call = g.Add(CreateCallEventNode(g.Id(), "shout", {EventParam{0, "Amount", PinType::Float}}));
    g.Flow(start, call);
    g.Data(g.Num(3.5f), 0, call, 1);
    g.Say(call, "after call");
    Rig rig(std::move(g.G));
    rig.Start();
    CHECK(rig.Prints == std::vector<std::string>{"3.500000", "after call"});
}

TEST_CASE("Host events reach On Trigger and On Key handlers and wake waiters", "[latent][runtime]")
{
    Gb g;
    const int trigger = g.Add(CreateOnTriggerNode(g.Id()));
    const int entered = g.Add(CreatePrintNode(g.Id()));
    g.Flow(trigger, entered);
    g.Data(trigger, 3, entered, 1);
    const int key = g.Add(CreateOnKeyNode(g.Id(), "Space"));
    g.Say(key, "jump");
    const int start = Start(g);
    const int wait = g.Add(CreateWaitForEventNode(g.Id(), "@key:Space"));
    g.Flow(start, wait);
    g.Say(wait, "waiter");
    Rig rig(std::move(g.G));
    rig.Start();
    rig.Runtime->PostEvent("@key:Space", {});
    rig.Runtime->PostEvent("@key:Enter", {});
    rig.Runtime->PostEvent(kTriggerEventName, {std::uint64_t{1}, std::uint64_t{2}, true});
    rig.Tick(1);
    CHECK(rig.Prints == std::vector<std::string>{"jump", "waiter", "true"});
}

TEST_CASE("Latent state is frozen while ticks stop and dropped with the runtime", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    g.Say(g.Delay(start, 1.0f), "late");
    Rig rig(std::move(g.G));
    rig.Start();
    rig.Tick(2);
    CHECK(rig.Runtime->ActiveTasks() == 1);
    // The debugger paused: no ticks arrive, nothing advances.
    CHECK(rig.Prints.empty());
    rig.Tick(2);
    CHECK(rig.Prints == std::vector<std::string>{"late"});

    Rig restarted(rig.Graphs.Items[0].Graph);
    restarted.Start();
    restarted.Tick(2);
    CHECK(restarted.Runtime->ActiveTasks() == 1);
    restarted.Runtime.reset();
    restarted.Build();
    CHECK(restarted.Runtime->ActiveTasks() == 0);
    restarted.Tick(8);
    CHECK(restarted.Prints.empty());
}

TEST_CASE("The interpreter reports latent nodes as Luau-only", "[latent][interpreter]")
{
    Gb g;
    const int start = Start(g);
    g.Say(g.Delay(start, 0.1f), "never");
    SceneGraphs graphs;
    graphs.Items.push_back(NamedGraph{.Name = "Main", .Graph = std::move(g.G)});
    std::vector<std::string> prints;
    GraphSetExecutor executor(graphs, [&](std::string text) { prints.push_back(std::move(text)); });
    std::vector<std::string> errors;
    class Sink final : public ITraceSink
    {
    public:
        std::vector<std::string>* Out{nullptr};
        void OnDiagnostic(std::string_view, int, TraceSeverity severity, std::string_view message) override
        {
            if (severity == TraceSeverity::Error) Out->emplace_back(message);
        }
    } sink;
    sink.Out = &errors;
    executor.SetTraceSink(&sink);
    Scene scene("Interp");
    (void)executor.ExecuteStartEvent(scene);
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("Luau runtime") != std::string::npos);
    CHECK(prints.empty());
}

TEST_CASE("Resumed flows keep reporting node diagnostics", "[latent][runtime]")
{
    Gb g;
    const int start = Start(g);
    const int delay = g.Delay(start, 0.25f);
    const int setter = g.Add(CreateSetterNode(g.Id(), "Transform"));
    g.Flow(delay, setter);
    Rig rig(std::move(g.G));
    rig.Start();
    CHECK_FALSE(rig.Diagnosed("Entity input is empty"));
    rig.Tick(2);
    CHECK(rig.Diagnosed("Entity input is empty"));
    CHECK(rig.Runtime->ActiveTasks() == 0);
}

TEST_CASE("Nodes and event names survive serialization and renames", "[latent][persistence]")
{
    Gb g;
    const int define = g.Add(CreateCustomEventNode(g.Id(), "shout", {EventParam{0, "A", PinType::Float}}));
    const int tween = g.Add(CreateInterpolateNode(g.Id(), PinType::Vec3, "cubicInOut"));
    const int sequence = g.Add(CreateSequenceNode(g.Id(), 4));
    const int key = g.Add(CreateOnKeyNode(g.Id(), "F"));
    const auto loaded = DeserializeFromYaml(SerializeToYaml(g.G));
    REQUIRE(loaded.Nodes.size() == 4);
    CHECK(loaded.FindNode(define)->Label == "shout");
    CHECK(loaded.FindNode(define)->SubType == NodeSubType::CustomEvent);
    CHECK(loaded.FindNode(tween)->Label == "cubicInOut");
    CHECK(loaded.FindNode(tween)->Type == NodeType::Latent);
    CHECK(loaded.FindNode(sequence)->Outputs.size() == 4);
    CHECK(loaded.FindNode(key)->Label == "F");
    CHECK(EventNameOf(*loaded.FindNode(key)) == "@key:F");
}

TEST_CASE("Event parameter edits keep the call nodes in sync", "[latent][events]")
{
    AnimationGraphData graph;
    graph.Nodes.push_back(CreateCustomEventNode(graph.AllocateId(), "hit"));
    const int define = graph.Nodes.back().Id;
    graph.Nodes.push_back(CreateCallEventNode(graph.AllocateId(), "hit"));
    const int call = graph.Nodes.back().Id;
    REQUIRE(AddEventParam(graph, define, "Power", PinType::Float));
    REQUIRE(AddEventParam(graph, define, "Where", PinType::Vec3));
    CHECK(graph.FindNode(call)->Inputs.size() == 3);
    CHECK(CallMatchesEvent(*graph.FindNode(call), *graph.FindNode(define)));

    graph.Nodes.push_back(CreateConstantNode(graph.AllocateId(), PinType::Vec3));
    const int where = graph.Nodes.back().Id;
    REQUIRE(TryLink(graph, graph.FindNode(where)->Outputs[0].Id, graph.FindNode(call)->Inputs[2].Id));
    REQUIRE(RemoveEventParam(graph, define, 0));
    CHECK(graph.FindNode(call)->Inputs.size() == 2);
    CHECK(graph.FindNode(call)->Inputs[1].Name == "Where");
    CHECK(graph.Links.size() == 1);
    REQUIRE(RetypeEventParam(graph, define, 1, PinType::Float));
    CHECK(graph.Links.empty());
    REQUIRE(RenameEventParam(graph, define, 1, "Strength"));
    CHECK(graph.FindNode(call)->Inputs[1].Name == "Strength");
    REQUIRE(RenameCustomEvent(graph, "hit", "smash"));
    CHECK(graph.FindNode(call)->Label == "smash");
    CHECK(EventParams(*graph.FindNode(define)).size() == 1);
    CHECK_FALSE(AddEventParam(graph, define, "x", PinType::Flow));
    CHECK(CustomEventNodeEntries(graph).size() == 2);
}

TEST_CASE("Validation flags latent nodes in pure functions, bad events and Luau-only nodes", "[latent][validation]")
{
    SceneGraphs graphs;
    NamedGraph pure;
    pure.Name = "Pure";
    pure.IsFunction = true;
    pure.Signature.Pure = true;
    pure.Graph.Nodes.push_back(CreateDelayNode(pure.Graph.AllocateId()));
    graphs.Items.push_back(pure);

    NamedGraph impure;
    impure.Name = "Impure";
    impure.IsFunction = true;
    impure.Graph.Nodes.push_back(CreateDelayNode(impure.Graph.AllocateId()));
    graphs.Items.push_back(impure);

    NamedGraph main;
    main.Name = "Main";
    main.Graph.Nodes.push_back(CreateCallEventNode(main.Graph.AllocateId(), "missing"));
    main.Graph.Nodes.push_back(CreateCustomEventNode(main.Graph.AllocateId(), "dup"));
    main.Graph.Nodes.push_back(CreateCustomEventNode(main.Graph.AllocateId(), "dup"));
    main.Graph.Nodes.push_back(CreateCustomEventNode(main.Graph.AllocateId(), ""));
    graphs.Items.push_back(main);

    const auto has = [](const std::vector<Diagnostic>& list, const DiagnosticCode code, const TraceSeverity severity)
    {
        return std::ranges::any_of(list, [&](const Diagnostic& d) { return d.Code == code && d.Severity == severity; });
    };
    const auto result = ValidateScene(graphs);
    CHECK(has(result.at("Pure"), DiagnosticCode::LatentInPureFunction, TraceSeverity::Error));
    CHECK(has(result.at("Pure"), DiagnosticCode::LuauOnly, TraceSeverity::Info));
    CHECK_FALSE(has(result.at("Impure"), DiagnosticCode::LatentInPureFunction, TraceSeverity::Error));
    CHECK(has(result.at("Main"), DiagnosticCode::UndefinedEvent, TraceSeverity::Error));
    CHECK(std::ranges::count_if(result.at("Main"), [](const Diagnostic& d) { return d.Code == DiagnosticCode::EventName; }) == 3);
}

TEST_CASE("Palette entries exist for every latent and event node and are marked Luau-only", "[latent][palette]")
{
    const NodeRegistry registry = BuildNodeRegistry();
    for (const char* name : {"Delay", "Wait Until", "Wait For Event", "Interpolate", "Interpolate Vec3", "Sequence", "Do Once", "Gate",
                             "Switch", "For Each Entity", "While", "Custom Event", "On Trigger", "On Key"})
    {
        INFO(name);
        const NodeEntry* entry = registry.Find(name);
        REQUIRE(entry != nullptr);
        AnimationGraphData scratch;
        const Node* probe = ProbeNode(*entry, scratch);
        REQUIRE(probe != nullptr);
        CHECK(IsLuauOnly(*probe));
        CHECK(entry->Description.find("Luau runtime only") != std::string::npos);
    }
}
