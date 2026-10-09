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
        void Flow(const int from, const int to, const std::size_t fromPin = 0) { Link(Out(from, fromPin), In(to, 0)); }
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
        int Print(const int flowFrom, const int valueNode, const std::size_t valuePin = 0, const std::size_t flowPin = 0)
        {
            const int print = Add(CreatePrintNode(Id()));
            Flow(flowFrom, print, flowPin);
            Data(valueNode, valuePin, print, 1);
            return print;
        }
        int Math(const NodeSubType op, const std::vector<std::pair<int, std::size_t>>& inputs)
        {
            const int node = Add(CreateMathNode(Id(), op));
            for (std::size_t i = 0; i < inputs.size(); ++i) Data(inputs[i].first, inputs[i].second, node, i);
            return node;
        }
    };

    using Snapshot = std::map<std::string, Value>;

    Snapshot Capture(const Scene& scene)
    {
        Snapshot snapshot;
        const auto& registry = scene.Registry();
        for (auto [entity, metadata] : registry.view<const GPP::MetadataComponent>().each())
        {
            GPP::ComponentRegistry::Instance().ForEach([&](const GPP::ComponentTypeInfo& info)
            {
                if (!info.Has || !info.Has(registry, entity)) return;
                for (const auto& field : info.Fields)
                {
                    snapshot[std::format("{}/{}/{}", metadata.Guid, info.Name, field.Name)] = field.Get(registry, entity);
                }
            });
        }
        return snapshot;
    }

    bool Near(const float a, const float b) { return std::abs(a - b) <= 1e-4f * std::max(1.0f, std::max(std::abs(a), std::abs(b))); }

    bool Same(const Value& a, const Value& b)
    {
        if (a.index() != b.index()) return false;
        if (const auto* x = std::get_if<float>(&a)) return Near(*x, std::get<float>(b));
        if (const auto* x = std::get_if<glm::vec2>(&a)) return Near(x->x, std::get<glm::vec2>(b).x) && Near(x->y, std::get<glm::vec2>(b).y);
        if (const auto* x = std::get_if<glm::vec3>(&a))
        {
            const auto& y = std::get<glm::vec3>(b);
            return Near(x->x, y.x) && Near(x->y, y.y) && Near(x->z, y.z);
        }
        if (const auto* x = std::get_if<glm::vec4>(&a))
        {
            const auto& y = std::get<glm::vec4>(b);
            return Near(x->x, y.x) && Near(x->y, y.y) && Near(x->z, y.z) && Near(x->w, y.w);
        }
        return a == b;
    }

    struct Outcome
    {
        std::vector<std::string> Prints;
        Snapshot State;
        std::set<std::string> Diagnostics;
        std::string Error;
    };

    class DiagnosticSink final : public ITraceSink
    {
    public:
        std::set<std::string>* Out{nullptr};
        void OnDiagnostic(std::string_view graph, int nodeId, TraceSeverity severity, std::string_view message) override
        {
            Out->insert(std::format("{}/{}/{}/{}", graph, nodeId, static_cast<int>(severity), message));
        }
    };

    using Setup = std::function<void(Scene&)>;

    Outcome Execute(const SceneGraphs& graphs, const Setup& setup, const int ticks, const std::string& mode)
    {
        Outcome outcome;
        Scene scene("Parity");
        setup(scene);
        std::uint64_t next = 5000;
        const auto onPrint = [&](std::string text) { outcome.Prints.push_back(std::move(text)); };
        DiagnosticSink sink;
        sink.Out = &outcome.Diagnostics;

        ScriptCache cache;
        std::unique_ptr<IGraphRuntime> runtime;
        if (mode == "interpreter")
        {
            runtime = std::make_unique<GraphSetExecutor>(graphs, onPrint);
        }
        else
        {
            auto script = std::make_unique<ScriptRuntime>(graphs, cache, onPrint, TranspileOptions{.InlinePure = mode == "luau-inline"});
            outcome.Error = script->Error();
            if (!script->Ok()) return outcome;
            runtime = std::move(script);
        }
        runtime->SetGuidSource([&next] { return next++; });
        if (mode != "luau-inline") runtime->SetTraceSink(&sink);

        const auto apply = [&](PendingWrites writes) { for (auto& write : writes) write(scene); };
        apply(runtime->ExecuteStartEvent(scene));
        for (int i = 0; i < ticks; ++i) apply(runtime->ExecuteTickEvent(scene, 0.25f));
        outcome.State = Capture(scene);
        return outcome;
    }

    void ExpectParity(const SceneGraphs& graphs, const Setup& setup, const int ticks = 3, const bool compareDiagnostics = false)
    {
        RegisterComponents();
        const Outcome reference = Execute(graphs, setup, ticks, "interpreter");
        for (const std::string mode : {"luau", "luau-inline"})
        {
            INFO("mode " << mode);
            const Outcome actual = Execute(graphs, setup, ticks, mode);
            INFO(actual.Error);
            REQUIRE(actual.Error.empty());
            CHECK(actual.Prints == reference.Prints);
            REQUIRE(actual.State.size() == reference.State.size());
            for (const auto& [key, value] : reference.State)
            {
                INFO(key);
                const auto it = actual.State.find(key);
                REQUIRE(it != actual.State.end());
                CHECK(Same(it->second, value));
            }
            if (compareDiagnostics && mode == "luau") CHECK(actual.Diagnostics == reference.Diagnostics);
        }
    }

    constexpr std::uint64_t kBlackHole = 101;
    constexpr std::uint64_t kSphere = 102;

    void StandardScene(Scene& scene)
    {
        RegisterComponents();
        const auto bh = scene.CreateEntityWithGuid(kBlackHole, "BH");
        scene.Registry().emplace<GPP::TransformComponent>(bh);
        scene.Registry().emplace<BlackHoleComponent>(bh, BlackHoleComponent{.Mass = 10.0f});
        const auto sphere = scene.CreateEntityWithGuid(kSphere, "Ball");
        scene.Registry().emplace<GPP::TransformComponent>(sphere);
        scene.Registry().emplace<SphereComponent>(sphere);
    }

    SceneGraphs Single(AnimationGraphData graph, const std::uint64_t entity = 0)
    {
        SceneGraphs graphs;
        graphs.Items.push_back(NamedGraph{.Name = "Main", .EntityGuid = entity, .Graph = std::move(graph)});
        return graphs;
    }

    int Getter(Gb& b, const std::uint64_t guid)
    {
        Node node = CreateGetterNode(b.Id(), "BlackHole");
        node.TargetGuid = guid;
        return b.Add(std::move(node));
    }

    std::size_t MakeAddOne(SceneGraphs& graphs, const bool pure)
    {
        const auto index = AddFunctionGraph(graphs, "AddOne");
        auto& fn = graphs.Items[index];
        fn.Signature.Pure = pure;
        AddParam(fn.Signature, false, "x", PinType::Float);
        AddParam(fn.Signature, true, "y", PinType::Float);
        SyncFunction(graphs, fn.Name);
        Gb b{fn.Graph};
        const int one = b.Const(1.0f, PinType::Float);
        const int add = b.Add(CreateMathNode(b.Id(), NodeSubType::Add));
        const int entry = static_cast<int>(std::ranges::find(b.G.Nodes, NodeSubType::FunctionEntry, &Node::SubType)->Id);
        const int ret = static_cast<int>(std::ranges::find(b.G.Nodes, NodeSubType::FunctionReturn, &Node::SubType)->Id);
        b.Link(b.G.FindNode(entry)->Outputs.back().Id, b.In(add, 0));
        b.Link(b.Out(one), b.In(add, 1));
        b.Link(b.Out(add), b.G.FindNode(ret)->Inputs.back().Id);
        fn.Graph = std::move(b.G);
        return index;
    }
}

TEST_CASE("Parity: tick adds delta time to a black hole mass", "[script][parity]")
{
    Gb b;
    const int tick = b.Add(CreateTickEventNode(b.Id()));
    const int getter = Getter(b, kBlackHole);
    const int decompose = b.Add(CreateDecomposerNode(b.Id(), NodeSubType::BlackHole));
    const int add = b.Math(NodeSubType::Add, {{decompose, 0}, {tick, 1}});
    const int setter = b.Add(CreateSetterNode(b.Id(), NodeSubType::BlackHole));
    b.Data(getter, 0, decompose, 0);
    b.Data(getter, 0, setter, 1);
    b.Data(add, 0, setter, 2);
    b.Flow(tick, setter);
    ExpectParity(Single(b.G), StandardScene, 4, true);
}

TEST_CASE("Parity: variables, memoized reads and printing", "[script][parity]")
{
    Gb b;
    b.G.Variables.push_back(Variable{"counter", PinType::Float, 0.0f});
    b.G.Variables.push_back(Variable{"label", PinType::String, std::string("x")});
    const int start = b.Add(CreateStartEventNode(b.Id()));
    const int tick = b.Add(CreateTickEventNode(b.Id()));
    const int read = b.Add(CreateVariableGetNode(b.Id(), "counter", PinType::Float));
    const int one = b.Const(1.0f, PinType::Float);
    const int add = b.Math(NodeSubType::Add, {{read, 0}, {one, 0}});
    const int set = b.Add(CreateVariableSetNode(b.Id(), "counter", PinType::Float));
    b.Data(add, 0, set, 1);
    const int before = b.Print(tick, read);
    b.Flow(before, set);
    b.Print(set, read);
    const int label = b.Add(CreateVariableGetNode(b.Id(), "label", PinType::String));
    b.Print(start, label);
    ExpectParity(Single(b.G), StandardScene, 4, true);
}

TEST_CASE("Parity: branch, boolean logic and unconnected inputs", "[script][parity]")
{
    Gb b;
    const int tick = b.Add(CreateTickEventNode(b.Id()));
    const int yes = b.Const(true, PinType::Bool);
    const int no = b.Const(false, PinType::Bool);
    const int both = b.Math(NodeSubType::And, {{yes, 0}, {no, 0}});
    const int either = b.Math(NodeSubType::Or, {{yes, 0}, {no, 0}});
    const int branch = b.Add(CreateBranchNode(b.Id()));
    b.Flow(tick, branch);
    b.Data(both, 0, branch, 1);
    const int a = b.Const(std::string("then-and"), PinType::String);
    const int c = b.Const(std::string("else-and"), PinType::String);
    b.Print(branch, a, 0, 0);
    b.Print(branch, c, 0, 1);
    const int branch2 = b.Add(CreateBranchNode(b.Id()));
    b.Flow(tick, branch2);
    b.Data(either, 0, branch2, 1);
    b.Print(branch2, a, 0, 0);
    b.Print(branch2, c, 0, 1);
    const int dangling = b.Math(NodeSubType::Add, {});
    b.Print(tick, dangling);
    ExpectParity(Single(b.G), StandardScene, 2, true);
}

TEST_CASE("Parity: for loops accumulate through variables and clear memoized values", "[script][parity]")
{
    Gb b;
    b.G.Variables.push_back(Variable{"sum", PinType::Float, 0.0f});
    const int start = b.Add(CreateStartEventNode(b.Id()));
    const int loop = b.Add(CreateForNode(b.Id()));
    const int from = b.Const(1, PinType::Int);
    const int to = b.Const(5, PinType::Int);
    b.Flow(start, loop);
    b.Data(from, 0, loop, 1);
    b.Data(to, 0, loop, 2);
    const int read = b.Add(CreateVariableGetNode(b.Id(), "sum", PinType::Float));
    const int add = b.Math(NodeSubType::Add, {{read, 0}, {loop, 1}});
    const int set = b.Add(CreateVariableSetNode(b.Id(), "sum", PinType::Float));
    b.Data(add, 0, set, 1);
    b.Flow(loop, set, 0);
    const int total = b.Add(CreateVariableGetNode(b.Id(), "sum", PinType::Float));
    b.Print(loop, total, 0, 2);
    b.Print(set, loop, 1);
    ExpectParity(Single(b.G), StandardScene, 1, true);
}

TEST_CASE("Parity: math nodes over floats and vectors", "[script][parity]")
{
    Gb b;
    const int tick = b.Add(CreateTickEventNode(b.Id()));
    const int f1 = b.Const(2.5f, PinType::Float);
    const int f2 = b.Const(-0.75f, PinType::Float);
    const int zero = b.Const(0.0f, PinType::Float);
    const int v1 = b.Const(glm::vec3(1.0f, 2.0f, 3.0f), PinType::Vec3);
    const int v2 = b.Const(glm::vec3(-4.0f, 0.5f, 9.0f), PinType::Vec3);
    const int w1 = b.Const(glm::vec2(1.0f, 2.0f), PinType::Vec2);
    const int w2 = b.Const(glm::vec2(3.0f, -1.0f), PinType::Vec2);
    const int q1 = b.Const(glm::vec4(1.0f, 2.0f, 3.0f, 4.0f), PinType::Vec4);
    const int q2 = b.Const(glm::vec4(0.5f, 0.25f, 8.0f, -2.0f), PinType::Vec4);
    int last = tick;
    const auto show = [&](const int node) { last = b.Print(last, node); };
    for (const auto op : {NodeSubType::Add, NodeSubType::Sub, NodeSubType::Mul, NodeSubType::Div, NodeSubType::Min, NodeSubType::Max})
    {
        show(b.Math(op, {{f1, 0}, {f2, 0}}));
        show(b.Math(op, {{v1, 0}, {v2, 0}}));
        show(b.Math(op, {{w1, 0}, {w2, 0}}));
        show(b.Math(op, {{q1, 0}, {q2, 0}}));
    }
    show(b.Math(NodeSubType::Div, {{f1, 0}, {zero, 0}}));
    show(b.Math(NodeSubType::Add, {{f1, 0}, {v1, 0}}));
    for (const auto op : {NodeSubType::Sin, NodeSubType::Cos, NodeSubType::Tan, NodeSubType::Sqrt, NodeSubType::Negate, NodeSubType::Length})
    {
        show(b.Math(op, {{f1, 0}}));
        show(b.Math(op, {{v2, 0}}));
        show(b.Math(op, {{q2, 0}}));
    }
    show(b.Math(NodeSubType::Distance, {{v1, 0}, {v2, 0}}));
    show(b.Math(NodeSubType::Distance, {{w1, 0}, {w2, 0}}));
    show(b.Math(NodeSubType::Lerp, {{v1, 0}, {v2, 0}, {f2, 0}}));
    show(b.Math(NodeSubType::Lerp, {{f1, 0}, {f2, 0}, {f2, 0}}));
    show(b.Math(NodeSubType::Clamp, {{v2, 0}, {v1, 0}, {v1, 0}}));
    show(b.Math(NodeSubType::Clamp, {{f1, 0}, {zero, 0}, {f2, 0}}));
    ExpectParity(Single(b.G), StandardScene, 1);
}

TEST_CASE("Parity: flow and pure functions, recursion and missing functions", "[script][parity][function]")
{
    for (const bool pure : {false, true})
    {
        SceneGraphs graphs;
        const auto fn = MakeAddOne(graphs, pure);
        Gb b;
        const int start = b.Add(CreateStartEventNode(b.Id()));
        const int two = b.Const(2.0f, PinType::Float);
        if (pure)
        {
            const int call = b.Add(CreateFunctionCallNode(b.Id(), "AddOne", graphs.Items[fn].Signature));
            b.Data(two, 0, call, 0);
            b.Print(start, call);
        }
        else
        {
            const int call = b.Add(CreateFunctionCallNode(b.Id(), "AddOne", graphs.Items[fn].Signature));
            b.Flow(start, call);
            b.Data(two, 0, call, 1);
            b.Print(call, call, 1);
        }
        const int missing = b.Add(CreateFunctionCallNode(b.Id(), "Missing", FunctionSignature{}));
        b.Flow(start, missing);
        graphs.Items.push_back(NamedGraph{.Name = "Main", .Graph = std::move(b.G)});
        ExpectParity(graphs, StandardScene, 1, true);
    }

    SceneGraphs graphs;
    const auto index = AddFunctionGraph(graphs, "Loop");
    auto& fn = graphs.Items[index];
    fn.Graph.Links.clear();
    Gb inner{fn.Graph};
    const int self = inner.Add(CreateFunctionCallNode(inner.Id(), "Loop", fn.Signature));
    const int text = inner.Const(std::string("in loop"), PinType::String);
    inner.Link(inner.Out(inner.G.Nodes[0].Id), inner.In(self, 0));
    const int printed = inner.Print(self, text);
    (void)printed;
    fn.Graph = std::move(inner.G);
    Gb b;
    const int start = b.Add(CreateStartEventNode(b.Id()));
    const int call = b.Add(CreateFunctionCallNode(b.Id(), "Loop", fn.Signature));
    b.Flow(start, call);
    graphs.Items.push_back(NamedGraph{.Name = "Main", .Graph = std::move(b.G)});
    ExpectParity(graphs, StandardScene, 1, true);
}

TEST_CASE("Parity: spawn, clone, destroy and setters on spawned entities", "[script][parity][spawn]")
{
    Gb b;
    const int start = b.Add(CreateStartEventNode(b.Id()));
    const int preset = b.Const(std::string("BlackHole"), PinType::String);
    const int position = b.Const(glm::vec3(1.0f, 2.0f, 3.0f), PinType::Vec3);
    const int mass = b.Const(7.0f, PinType::Float);
    const int spawn = b.Add(CreateSpawnEntityNode(b.Id()));
    b.Flow(start, spawn);
    b.Data(preset, 0, spawn, 1);
    b.Data(position, 0, spawn, 2);
    const int setter = b.Add(CreateSetterNode(b.Id(), NodeSubType::BlackHole));
    b.Flow(spawn, setter);
    b.Data(spawn, 1, setter, 1);
    b.Data(mass, 0, setter, 2);
    const int clone = b.Add(CreateCloneEntityNode(b.Id()));
    b.Flow(setter, clone);
    b.Data(spawn, 1, clone, 1);
    b.Data(position, 0, clone, 2);
    const int named = b.Const(std::string("Ball"), PinType::String);
    const int spawnNamed = b.Add(CreateSpawnEntityNode(b.Id()));
    b.Flow(clone, spawnNamed);
    b.Data(named, 0, spawnNamed, 1);
    b.Data(position, 0, spawnNamed, 2);
    const int destroy = b.Add(CreateDestroyEntityNode(b.Id()));
    b.Flow(spawnNamed, destroy);
    const int target = Getter(b, kSphere);
    b.Data(target, 0, destroy, 1);
    const int unknown = b.Const(std::string("Nothing"), PinType::String);
    const int spawnBad = b.Add(CreateSpawnEntityNode(b.Id()));
    b.Flow(destroy, spawnBad);
    b.Data(unknown, 0, spawnBad, 1);
    ExpectParity(Single(b.G), StandardScene, 1, true);
}

TEST_CASE("Parity: look at drives a camera rotation", "[script][parity][camera]")
{
    Gb b;
    const int start = b.Add(CreateStartEventNode(b.Id()));
    const int getter = Getter(b, kSphere);
    const int from = b.Const(glm::vec3(0.0f), PinType::Vec3);
    const int target = b.Const(glm::vec3(10.0f, 3.0f, -4.0f), PinType::Vec3);
    const int look = b.Math(NodeSubType::LookAt, {{from, 0}, {target, 0}});
    const int setter = b.Add(CreateSetterNode(b.Id(), "Transform"));
    b.Flow(start, setter);
    b.Data(getter, 0, setter, 1);
    b.Data(look, 0, setter, 3);
    ExpectParity(Single(b.G), StandardScene, 1, true);
}

TEST_CASE("Parity: shipped combos behave the same under both runtimes", "[script][parity][combo]")
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
    REQUIRE_FALSE(library.Entries().empty());
    for (const auto& entry : library.Entries())
    {
        INFO(entry.Name);
        AnimationGraphData graph;
        entry.Spawn(graph);
        ExpectParity(Single(std::move(graph), kBlackHole), StandardScene, 3);
    }
}

TEST_CASE("Transpiled source uses the expected constructs and keeps a source map", "[script][transpiler]")
{
    Gb b;
    b.G.Variables.push_back(Variable{"sum", PinType::Float, 0.0f});
    const int tick = b.Add(CreateTickEventNode(b.Id()));
    const int loop = b.Add(CreateForNode(b.Id()));
    const int to = b.Const(3, PinType::Int);
    const int cond = b.Const(true, PinType::Bool);
    const int branch = b.Add(CreateBranchNode(b.Id()));
    const int read = b.Add(CreateVariableGetNode(b.Id(), "sum", PinType::Float));
    const int set = b.Add(CreateVariableSetNode(b.Id(), "sum", PinType::Float));
    b.Flow(tick, loop);
    b.Data(to, 0, loop, 2);
    b.Flow(loop, branch, 0);
    b.Data(cond, 0, branch, 1);
    b.Flow(branch, set, 0);
    b.Data(read, 0, set, 1);
    SceneGraphs graphs = Single(b.G);

    IrCache cache;
    const auto script = TranspileGraph(graphs, graphs.Items[0], cache);
    REQUIRE(script.Ok());
    const std::string& src = script.Source;
    CHECK(src.find("function M.tick(dt)") != std::string::npos);
    CHECK(src.find("function M.start()") != std::string::npos);
    CHECK(src.find("for i" + std::to_string(loop) + " = ") != std::string::npos);
    CHECK(src.find("if bool(") != std::string::npos);
    CHECK(src.find("vars[\"sum\"]") != std::string::npos);
    CHECK(src.find("lazy(c, " + std::to_string(read)) != std::string::npos);

    int line = 1;
    std::size_t at = src.find("vars[\"sum\"] =");
    REQUIRE(at != std::string::npos);
    line += static_cast<int>(std::count(src.begin(), src.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
    CHECK(script.Locate(line).NodeId == set);
    CHECK(script.Locate(line).Graph == "Main");

    const auto inlined = TranspileGraph(graphs, graphs.Items[0], cache, TranspileOptions{.InlinePure = true});
    CHECK(inlined.Source.find("lazy(c, " + std::to_string(cond)) == std::string::npos);
}

TEST_CASE("Compiled IR is cached until the graph changes", "[script][ir]")
{
    Gb b;
    const int tick = b.Add(CreateTickEventNode(b.Id()));
    const int text = b.Const(std::string("x"), PinType::String);
    b.Print(tick, text);
    SceneGraphs graphs = Single(b.G);

    ScriptCache cache;
    const auto first = cache.Get(graphs, graphs.Items[0]);
    const auto again = cache.Get(graphs, graphs.Items[0]);
    CHECK(first == again);
    CHECK(cache.Transpilations() == 1);
    CHECK(cache.Ir().Compilations() == 1);

    graphs.Items[0].Graph.Nodes.push_back(CreatePrintNode(graphs.Items[0].Graph.AllocateId()));
    const auto changed = cache.Get(graphs, graphs.Items[0]);
    CHECK(changed != first);
    CHECK(cache.Transpilations() == 2);
}

TEST_CASE("Runtime errors map back to graph nodes and runaway graphs are stopped", "[script][runtime]")
{
    Gb b;
    const int tick = b.Add(CreateTickEventNode(b.Id()));
    const int loop = b.Add(CreateForNode(b.Id()));
    const int from = b.Const(0, PinType::Int);
    const int to = b.Const(2000000000, PinType::Int);
    const int text = b.Const(std::string("x"), PinType::String);
    b.Flow(tick, loop);
    b.Data(from, 0, loop, 1);
    b.Data(to, 0, loop, 2);
    const int print = b.Print(loop, text, 0, 0);
    SceneGraphs graphs = Single(b.G);

    ScriptCache cache;
    ScriptRuntime runtime(graphs, cache, nullptr, {}, GPP::LuauLimits{.MaxSafepoints = 50000, .MaxSeconds = 5.0});
    REQUIRE(runtime.Ok());
    struct Sink final : ITraceSink
    {
        std::vector<std::pair<int, std::string>> Errors;
        void OnDiagnostic(std::string_view, int node, TraceSeverity severity, std::string_view message) override
        {
            if (severity == TraceSeverity::Error) Errors.emplace_back(node, std::string(message));
        }
    } sink;
    runtime.SetTraceSink(&sink);
    Scene scene("Runaway");
    (void)runtime.ExecuteTickEvent(scene, 0.1f);
    CHECK_FALSE(runtime.LastRuntimeError().empty());
    REQUIRE_FALSE(sink.Errors.empty());
    CHECK(sink.Errors[0].second.find("instruction limit") != std::string::npos);
    CHECK(sink.Errors[0].first != 0);
    (void)print;
}
