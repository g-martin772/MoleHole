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

    bool Has(const std::vector<Diagnostic>& list, const DiagnosticCode code, const int nodeId = 0)
    {
        return std::ranges::any_of(list, [&](const Diagnostic& d) { return d.Code == code && (nodeId == 0 || d.NodeId == nodeId); });
    }

    // Tick -> Print(a) -> Print(b); both print a constant.
    SceneGraphs TwoPrints(int& tickId, int& firstId, int& secondId)
    {
        SceneGraphs graphs;
        NamedGraph main{.Name = "Main"};
        auto& g = main.Graph;
        g.Nodes.push_back(CreateTickEventNode(g.AllocateId()));
        Node text = CreateConstantNode(g.AllocateId(), PinType::String);
        text.ConstantValue = std::string("hi");
        g.Nodes.push_back(text);
        g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
        g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
        tickId = g.Nodes[0].Id;
        firstId = g.Nodes[2].Id;
        secondId = g.Nodes[3].Id;
        Connect(g, g.Nodes[0].Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
        Connect(g, text.Outputs[0].Id, g.Nodes[2].Inputs[1].Id);
        Connect(g, g.Nodes[2].Outputs[0].Id, g.Nodes[3].Inputs[0].Id);
        Connect(g, text.Outputs[0].Id, g.Nodes[3].Inputs[1].Id);
        graphs.Items.push_back(std::move(main));
        return graphs;
    }
}

TEST_CASE("The trace sink sees executed nodes, traversed links and evaluated pins", "[graph][debug]")
{
    int tick = 0, first = 0, second = 0;
    const auto graphs = TwoPrints(tick, first, second);
    TraceRecorder trace;
    GraphSetExecutor executor(graphs, [](std::string) {});
    executor.SetTraceSink(&trace);
    Scene scene("Trace");
    (void)executor.ExecuteTickEvent(scene, 0.1f);

    CHECK(trace.Tick() == 1);
    CHECK(trace.NodeTick("Main", tick) == 1);
    CHECK(trace.NodeTick("Main", first) == 1);
    CHECK(trace.NodeTick("Main", second) == 1);
    CHECK(trace.NodeTick("Main", 9999) == 0);
    for (const auto& link : graphs.Items[0].Graph.Links)
    {
        const bool flow = FindPin(graphs.Items[0].Graph, link.StartPinId)->Type == PinType::Flow;
        if (flow) CHECK(trace.LinkTick("Main", link.Id) == 1);
    }
    const auto* dt = trace.PinValue("Main", graphs.Items[0].Graph.Nodes[0].Outputs[1].Id);
    REQUIRE(dt != nullptr);
    CHECK(std::get<float>(*dt) == 0.1f);
    const auto* constant = trace.PinValue("Main", graphs.Items[0].Graph.Nodes[1].Outputs[0].Id);
    REQUIRE(constant != nullptr);
    CHECK(std::get<std::string>(*constant) == "hi");
    CHECK(trace.Entries().size() >= 3);

    (void)executor.ExecuteTickEvent(scene, 0.1f);
    CHECK(trace.Tick() == 2);
    CHECK(trace.NodeTick("Main", first) == 2);
}

TEST_CASE("A breakpoint aborts the tick and pauses until continued or stepped", "[graph][debug]")
{
    int tick = 0, first = 0, second = 0;
    const auto graphs = TwoPrints(tick, first, second);
    TraceRecorder trace;
    int prints = 0;
    GraphSetExecutor executor(graphs, [&](std::string) { ++prints; });
    executor.SetTraceSink(&trace);
    Scene scene("Break");
    trace.ToggleBreakpoint("Main", second);
    CHECK(trace.HasBreakpoint("Main", second));

    REQUIRE(trace.ShouldRunTick());
    (void)executor.ExecuteTickEvent(scene, 0.1f);
    CHECK(prints == 1);
    CHECK(trace.Paused());
    REQUIRE(trace.Hit());
    CHECK(trace.Hit()->NodeId == second);
    CHECK_FALSE(trace.ShouldRunTick());

    trace.Step();
    REQUIRE(trace.ShouldRunTick());
    (void)executor.ExecuteTickEvent(scene, 0.1f);
    CHECK(prints == 3);
    CHECK(trace.Paused());
    CHECK_FALSE(trace.ShouldRunTick());

    trace.Continue();
    CHECK(trace.ShouldRunTick());
    (void)executor.ExecuteTickEvent(scene, 0.1f);
    CHECK(prints == 4);
    CHECK(trace.Paused());

    trace.ToggleBreakpoint("Main", second);
    CHECK_FALSE(trace.HasBreakpoint("Main", second));
    trace.Continue();
    (void)executor.ExecuteTickEvent(scene, 0.1f);
    CHECK(prints == 6);
    CHECK_FALSE(trace.Paused());
}

TEST_CASE("A breakpoint inside a function aborts the whole tick", "[graph][debug][function]")
{
    SceneGraphs graphs;
    const auto fnIndex = AddFunctionGraph(graphs, "Say");
    auto& fn = graphs.Items[fnIndex];
    fn.Graph.Links.clear();
    fn.Graph.Nodes.push_back(CreatePrintNode(fn.Graph.AllocateId()));
    Connect(fn.Graph, fn.Graph.Nodes[0].Outputs[0].Id, fn.Graph.Nodes[2].Inputs[0].Id);
    const int printId = fn.Graph.Nodes[2].Id;
    const auto signature = fn.Signature;

    NamedGraph main{.Name = "Main"};
    auto& g = main.Graph;
    g.Nodes.push_back(CreateStartEventNode(g.AllocateId()));
    g.Nodes.push_back(CreateFunctionCallNode(g.AllocateId(), "Say", signature));
    g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
    Connect(g, g.Nodes[0].Outputs[0].Id, g.Nodes[1].Inputs[0].Id);
    Connect(g, g.Nodes[1].Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
    graphs.Items.push_back(std::move(main));

    TraceRecorder trace;
    trace.ToggleBreakpoint("Say", printId);
    int prints = 0;
    GraphSetExecutor executor(graphs, [&](std::string) { ++prints; });
    executor.SetTraceSink(&trace);
    Scene scene("Break");
    (void)executor.ExecuteStartEvent(scene);
    CHECK(prints == 0);
    CHECK(trace.Paused());
    CHECK(trace.Hit()->Graph == "Say");
}

TEST_CASE("Runtime problems are reported per node", "[graph][debug]")
{
    RegisterComponents();
    SceneGraphs graphs;
    NamedGraph main{.Name = "Main"};
    auto& g = main.Graph;
    g.Nodes.push_back(CreateStartEventNode(g.AllocateId()));
    g.Nodes.push_back(CreateSetterNode(g.AllocateId(), "Transform"));
    g.Nodes.push_back(CreateFunctionCallNode(g.AllocateId(), "Nope", FunctionSignature{}));
    g.Nodes.push_back(CreateMathNode(g.AllocateId(), NodeSubType::Add));
    g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
    Connect(g, g.Nodes[0].Outputs[0].Id, g.Nodes[1].Inputs[0].Id);
    Connect(g, g.Nodes[1].Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
    Connect(g, g.Nodes[2].Outputs[0].Id, g.Nodes[4].Inputs[0].Id);
    Connect(g, g.Nodes[3].Outputs[0].Id, g.Nodes[4].Inputs[1].Id);
    const int setter = g.Nodes[1].Id, call = g.Nodes[2].Id, add = g.Nodes[3].Id;
    graphs.Items.push_back(std::move(main));

    TraceRecorder trace;
    GraphSetExecutor executor(graphs, [](std::string) {});
    executor.SetTraceSink(&trace);
    Scene scene("Problems");
    (void)executor.ExecuteStartEvent(scene);

    const auto setterIssues = trace.ActiveDiagnostics("Main", setter);
    REQUIRE(setterIssues.size() == 1);
    CHECK(setterIssues[0].Severity == TraceSeverity::Warning);
    const auto callIssues = trace.ActiveDiagnostics("Main", call);
    REQUIRE(callIssues.size() == 1);
    CHECK(callIssues[0].Severity == TraceSeverity::Error);
    CHECK(trace.ActiveDiagnostics("Main", add).size() == 1);
    const auto all = trace.AllActiveDiagnostics();
    CHECK(all.size() == 3);
    CHECK(std::ranges::all_of(all, [](const GraphNodeDiagnostic& d) { return d.Graph == "Main"; }));
    CHECK(std::ranges::count(trace.Entries(), TraceKind::Diagnostic, &TraceEntry::Kind) == 3);
}

TEST_CASE("Cyclic data and flow terminate at runtime", "[graph][debug]")
{
    SceneGraphs graphs;
    NamedGraph main{.Name = "Main"};
    auto& g = main.Graph;
    g.Nodes.push_back(CreateStartEventNode(g.AllocateId()));
    g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
    g.Nodes.push_back(CreateMathNode(g.AllocateId(), NodeSubType::Add));
    g.Nodes.push_back(CreateMathNode(g.AllocateId(), NodeSubType::Add));
    Connect(g, g.Nodes[0].Outputs[0].Id, g.Nodes[1].Inputs[0].Id);
    Connect(g, g.Nodes[1].Outputs[0].Id, g.Nodes[1].Inputs[0].Id);
    Connect(g, g.Nodes[2].Outputs[0].Id, g.Nodes[3].Inputs[0].Id);
    Connect(g, g.Nodes[3].Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
    Connect(g, g.Nodes[2].Outputs[0].Id, g.Nodes[1].Inputs[1].Id);
    graphs.Items.push_back(std::move(main));

    TraceRecorder trace;
    GraphSetExecutor executor(graphs, [](std::string) {});
    executor.SetTraceSink(&trace);
    Scene scene("Cycle");
    (void)executor.ExecuteStartEvent(scene);
    const auto issues = trace.ActiveDiagnostics("Main", graphs.Items[0].Graph.Nodes[2].Id);
    CHECK(std::ranges::any_of(issues, [](const NodeDiagnostic& d) { return d.Message.find("cycle") != std::string::npos; }));
    CHECK(trace.Entries().size() <= TraceRecorder::kMaxEntries);
}

TEST_CASE("Validation finds type mismatches, missing inputs and cycles", "[graph][validation]")
{
    AnimationGraphData g;
    g.Nodes.push_back(CreateConstantNode(g.AllocateId(), PinType::Bool));
    g.Nodes.push_back(CreateConstantNode(g.AllocateId(), PinType::String));
    g.Nodes.push_back(CreateMathNode(g.AllocateId(), NodeSubType::Add));
    g.Nodes.push_back(CreateMathNode(g.AllocateId(), NodeSubType::Add));
    g.Nodes.push_back(CreateStartEventNode(g.AllocateId()));
    g.Nodes.push_back(CreateSetterNode(g.AllocateId(), NodeSubType::Transform));
    const int boolOut = g.Nodes[0].Outputs[0].Id;
    const int mathA = g.Nodes[2].Inputs[0].Id;

    SECTION("an untouched graph is clean")
    {
        CHECK(ValidateGraph(g).empty());
    }
    SECTION("type mismatch")
    {
        Connect(g, boolOut, mathA);
        const auto issues = ValidateGraph(g);
        CHECK(Has(issues, DiagnosticCode::TypeMismatch, g.Nodes[2].Id));
    }
    SECTION("required inputs of connected nodes")
    {
        Connect(g, g.Nodes[4].Outputs[0].Id, g.Nodes[5].Inputs[0].Id);
        Connect(g, g.Nodes[3].Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
        const auto issues = ValidateGraph(g);
        CHECK(Has(issues, DiagnosticCode::UnconnectedInput, g.Nodes[5].Id));
        CHECK(Has(issues, DiagnosticCode::UnconnectedInput, g.Nodes[2].Id));
        CHECK_FALSE(Has(issues, DiagnosticCode::UnconnectedInput, g.Nodes[0].Id));
    }
    SECTION("data cycle")
    {
        Connect(g, g.Nodes[3].Outputs[0].Id, g.Nodes[2].Inputs[0].Id);
        Connect(g, g.Nodes[2].Outputs[0].Id, g.Nodes[3].Inputs[0].Id);
        const auto issues = ValidateGraph(g);
        CHECK(Has(issues, DiagnosticCode::DataCycle, g.Nodes[2].Id));
        CHECK(Has(issues, DiagnosticCode::DataCycle, g.Nodes[3].Id));
        CHECK_FALSE(Has(issues, DiagnosticCode::DataCycle, g.Nodes[0].Id));
    }
    SECTION("flow cycle")
    {
        g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
        g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
        const auto& a = g.Nodes[6];
        const auto& b = g.Nodes[7];
        Connect(g, a.Outputs[0].Id, b.Inputs[0].Id);
        Connect(g, b.Outputs[0].Id, a.Inputs[0].Id);
        CHECK(Has(ValidateGraph(g), DiagnosticCode::FlowCycle));
    }
    SECTION("nodes that never run are noted")
    {
        g.Nodes.push_back(CreatePrintNode(g.AllocateId()));
        Connect(g, g.Nodes[1].Outputs[0].Id, g.Nodes[6].Inputs[1].Id);
        const auto issues = ValidateGraph(g);
        CHECK(Has(issues, DiagnosticCode::NeverRuns, g.Nodes[6].Id));
        CHECK(CountSeverity(issues, TraceSeverity::Error) == 0);
    }
    SECTION("variables")
    {
        g.Nodes.push_back(CreateVariableGetNode(g.AllocateId(), "", PinType::Float));
        g.Nodes.push_back(CreateVariableGetNode(g.AllocateId(), "Ghost", PinType::Float));
        const auto issues = ValidateGraph(g);
        CHECK(Has(issues, DiagnosticCode::UnassignedVariable, g.Nodes[6].Id));
        CHECK(Has(issues, DiagnosticCode::UnknownVariable, g.Nodes[7].Id));
    }
}

TEST_CASE("Validation flags dangling, stale and recursive calls and function shape", "[graph][validation][function]")
{
    SceneGraphs graphs;
    const auto fnIndex = AddFunctionGraph(graphs, "F");
    AddParam(graphs.Items[fnIndex].Signature, false, "x", PinType::Float);
    SyncFunction(graphs, "F");
    const auto signature = graphs.Items[fnIndex].Signature;

    graphs.Items[fnIndex].Graph.Nodes.push_back(CreateFunctionCallNode(graphs.Items[fnIndex].Graph.AllocateId(), "F", signature));
    const int selfCall = graphs.Items[fnIndex].Graph.Nodes.back().Id;

    NamedGraph main{.Name = "Main"};
    main.Graph.Nodes.push_back(CreateFunctionCallNode(main.Graph.AllocateId(), "Gone", signature));
    main.Graph.Nodes.push_back(CreateFunctionCallNode(main.Graph.AllocateId(), "F", FunctionSignature{}));
    main.Graph.Nodes.push_back(CreateFunctionCallNode(main.Graph.AllocateId(), "F", signature));
    main.Graph.Nodes.push_back(CreateFunctionEntryNode(main.Graph.AllocateId(), FunctionSignature{}));
    const int gone = main.Graph.Nodes[0].Id, stale = main.Graph.Nodes[1].Id, good = main.Graph.Nodes[2].Id;
    graphs.Items.push_back(std::move(main));

    const auto all = ValidateScene(graphs);
    const auto& mainIssues = all.at("Main");
    CHECK(Has(mainIssues, DiagnosticCode::DanglingCall, gone));
    CHECK(Has(mainIssues, DiagnosticCode::StaleCall, stale));
    CHECK_FALSE(Has(mainIssues, DiagnosticCode::DanglingCall, good));
    CHECK_FALSE(Has(mainIssues, DiagnosticCode::StaleCall, good));
    CHECK_FALSE(Has(mainIssues, DiagnosticCode::RecursiveCall, good));
    CHECK(Has(mainIssues, DiagnosticCode::MisplacedFunctionNode));
    CHECK(Has(all.at("F"), DiagnosticCode::RecursiveCall, selfCall));

    auto& fn = graphs.Items[fnIndex].Graph;
    std::erase_if(fn.Nodes, [](const Node& n) { return n.SubType == NodeSubType::FunctionReturn; });
    CHECK(Has(ValidateGraph(fn, &graphs, &graphs.Items[fnIndex]), DiagnosticCode::MissingReturn));
}

TEST_CASE("Shipped combos validate without errors", "[graph][validation][combo]")
{
    RegisterComponents();
    ComboLibrary library;
    std::vector<ComboSource> sources;
    for (const auto& file : std::filesystem::directory_iterator(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "combos"))
    {
        if (file.path().extension() != ".yaml") continue;
        std::ifstream in(file.path());
        std::stringstream text;
        text << in.rdbuf();
        sources.push_back({file.path().stem().string(), text.str()});
    }
    library.Load(sources);
    REQUIRE(library.Entries().size() == 5);
    for (const auto& entry : library.Entries())
    {
        AnimationGraphData graph;
        entry.Spawn(graph);
        const auto issues = ValidateGraph(graph);
        for (const auto& d : issues) INFO(entry.Name << ": " << d.Message);
        CHECK(CountSeverity(issues, TraceSeverity::Error) == 0);
        CHECK(CountSeverity(issues, TraceSeverity::Warning) == 0);
    }
}
