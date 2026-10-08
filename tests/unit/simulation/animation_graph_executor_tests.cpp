#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    // Small helper shared by every test case below: wires a Link from an output pin id to an input
    // pin id, allocating the Link's own id the same way the (future) UI pass would.
    void Connect(AnimationGraphData& graph, const int startPinId, const int endPinId)
    {
        graph.Links.push_back(Link{graph.AllocateId(), startPinId, endPinId});
    }

    void ApplyWrites(PendingWrites& writes, Scene& scene)
    {
        for (auto& write : writes) { write(scene); }
    }
}

// Tick -> Decomposer(BlackHole).Mass + Tick.DeltaTime -> Add -> Setter(BlackHole).Mass. Applies
// each tick's PendingWrites to the scene immediately before the next ExecuteTickEvent call,
// simulating how production will apply one EnqueueEdit batch per tick before the next tick runs.
TEST_CASE("Tick event adds deltaTime to a BlackHole's Mass each tick via Decomposer+Add+Setter",
          "[simulation][animation]")
{
    RegisterComponents();
    Scene scene("AnimGraphTickTest");
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = 10.0f});
    const auto guid = scene.GuidOf(blackHole);

    AnimationGraphData graph;

    Node tickNode = CreateTickEventNode(graph.AllocateId());
    const int tickFlowOut = tickNode.Outputs[0].Id;
    const int tickDeltaTimeOut = tickNode.Outputs[1].Id;
    graph.Nodes.push_back(tickNode);

    Node getterNode = CreateGetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    getterNode.TargetGuid = guid;
    const int getterEntityOut = getterNode.Outputs[0].Id;
    graph.Nodes.push_back(getterNode);

    Node decomposerNode = CreateDecomposerNode(graph.AllocateId(), NodeSubType::BlackHole);
    const int decomposerEntityIn = decomposerNode.Inputs[0].Id;
    const int decomposerMassOut = decomposerNode.Outputs[0].Id; // Mass is property index 0
    graph.Nodes.push_back(decomposerNode);

    Node addNode = CreateMathNode(graph.AllocateId(), NodeSubType::Add);
    const int addInA = addNode.Inputs[0].Id;
    const int addInB = addNode.Inputs[1].Id;
    const int addOut = addNode.Outputs[0].Id;
    graph.Nodes.push_back(addNode);

    Node setterNode = CreateSetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    const int setterFlowIn = setterNode.Inputs[0].Id;
    const int setterEntityIn = setterNode.Inputs[1].Id;
    const int setterMassIn = setterNode.Inputs[2].Id; // Mass is property index 0 -> Inputs[2]
    graph.Nodes.push_back(setterNode);

    Connect(graph, tickFlowOut, setterFlowIn);
    Connect(graph, getterEntityOut, decomposerEntityIn);
    Connect(graph, getterEntityOut, setterEntityIn);
    Connect(graph, decomposerMassOut, addInA);
    Connect(graph, tickDeltaTimeOut, addInB);
    Connect(graph, addOut, setterMassIn);

    GraphExecutor executor(graph);

    for (int i = 0; i < 3; ++i)
    {
        PendingWrites writes = executor.ExecuteTickEvent(scene, 0.5f);
        ApplyWrites(writes, scene);
    }

    CHECK(scene.Registry().get<BlackHoleComponent>(blackHole).Mass == Catch::Approx(11.5f));
}

// Start -> VariableSet("counter", 0); Tick -> VariableGet("counter") -> Add 1 -> VariableSet
// ("counter"). Reads the value back via a Print fed by a fresh VariableGet (a test-only accessor
// would work equally well; this exercises real graph wiring instead). Then builds a brand new
// GraphExecutor over the SAME AnimationGraphData to simulate a fresh Play session, and confirms it
// does not see the first executor's accumulated variable state.
TEST_CASE("Variables persist across ticks within one executor instance but never leak into a new one",
          "[simulation][animation]")
{
    Scene scene("AnimGraphVarTest"); // unused by this graph beyond satisfying the Execute*Event signature

    AnimationGraphData graph;

    Node startNode = CreateStartEventNode(graph.AllocateId());
    const int startFlowOut = startNode.Outputs[0].Id;
    graph.Nodes.push_back(startNode);

    Node tickNode = CreateTickEventNode(graph.AllocateId());
    const int tickFlowOut = tickNode.Outputs[0].Id;
    graph.Nodes.push_back(tickNode);

    Node zeroConst = CreateConstantNode(graph.AllocateId(), PinType::Float); // defaults to 0.0f
    const int zeroOut = zeroConst.Outputs[0].Id;
    graph.Nodes.push_back(zeroConst);

    Node oneConst = CreateConstantNode(graph.AllocateId(), PinType::Float);
    oneConst.ConstantValue = 1.0f;
    const int oneOut = oneConst.Outputs[0].Id;
    graph.Nodes.push_back(oneConst);

    Node initSetNode = CreateVariableSetNode(graph.AllocateId(), "counter", PinType::Float);
    const int initFlowIn = initSetNode.Inputs[0].Id;
    const int initValueIn = initSetNode.Inputs[1].Id;
    graph.Nodes.push_back(initSetNode);

    Node getForAddNode = CreateVariableGetNode(graph.AllocateId(), "counter", PinType::Float);
    const int getForAddOut = getForAddNode.Outputs[0].Id;
    graph.Nodes.push_back(getForAddNode);

    Node addNode = CreateMathNode(graph.AllocateId(), NodeSubType::Add);
    const int addInA = addNode.Inputs[0].Id;
    const int addInB = addNode.Inputs[1].Id;
    const int addOut = addNode.Outputs[0].Id;
    graph.Nodes.push_back(addNode);

    Node tickSetNode = CreateVariableSetNode(graph.AllocateId(), "counter", PinType::Float);
    const int tickSetFlowIn = tickSetNode.Inputs[0].Id;
    const int tickSetValueIn = tickSetNode.Inputs[1].Id;
    const int tickSetFlowOut = tickSetNode.Outputs[0].Id;
    graph.Nodes.push_back(tickSetNode);

    Node getForPrintNode = CreateVariableGetNode(graph.AllocateId(), "counter", PinType::Float);
    const int getForPrintOut = getForPrintNode.Outputs[0].Id;
    graph.Nodes.push_back(getForPrintNode);

    Node printNode = CreatePrintNode(graph.AllocateId());
    const int printFlowIn = printNode.Inputs[0].Id;
    const int printValueIn = printNode.Inputs[1].Id;
    graph.Nodes.push_back(printNode);

    Connect(graph, startFlowOut, initFlowIn);
    Connect(graph, zeroOut, initValueIn);
    Connect(graph, tickFlowOut, tickSetFlowIn);
    Connect(graph, getForAddOut, addInA);
    Connect(graph, oneOut, addInB);
    Connect(graph, addOut, tickSetValueIn);
    Connect(graph, tickSetFlowOut, printFlowIn);
    Connect(graph, getForPrintOut, printValueIn);

    std::vector<std::string> prints1;
    GraphExecutor executor1(graph, [&prints1](std::string s) { prints1.push_back(std::move(s)); });

    PendingWrites startWrites = executor1.ExecuteStartEvent(scene);
    ApplyWrites(startWrites, scene);
    for (int i = 0; i < 3; ++i)
    {
        PendingWrites tickWrites = executor1.ExecuteTickEvent(scene, 1.0f / 60.0f);
        ApplyWrites(tickWrites, scene);
    }

    REQUIRE_FALSE(prints1.empty());
    CHECK(std::stof(prints1.back()) == Catch::Approx(3.0f));

    // Fresh executor over the SAME graph simulates a new Play session -- never calls
    // ExecuteStartEvent, so "counter" was never initialized on THIS instance.
    std::vector<std::string> prints2;
    GraphExecutor executor2(graph, [&prints2](std::string s) { prints2.push_back(std::move(s)); });
    PendingWrites leakCheckWrites = executor2.ExecuteTickEvent(scene, 1.0f / 60.0f);
    ApplyWrites(leakCheckWrites, scene);

    REQUIRE_FALSE(prints2.empty());
    CHECK(prints2.back() == "<empty>"); // monostate -- proves no leaked state from executor1
}

// Branch: true routes flow to one Setter (Mass -> 100), false routes to a different Setter
// (Mass -> -100). Verifies only the expected branch's write occurs for both a true- and a
// false-condition run.
TEST_CASE("Branch node routes flow to exactly one of its two Setters based on its condition",
          "[simulation][animation]")
{
    RegisterComponents();
    Scene scene("AnimGraphBranchTest");
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<BlackHoleComponent>(blackHole, BlackHoleComponent{.Mass = 1.0f});
    const auto guid = scene.GuidOf(blackHole);

    AnimationGraphData graph;

    Node startNode = CreateStartEventNode(graph.AllocateId());
    const int startFlowOut = startNode.Outputs[0].Id;
    graph.Nodes.push_back(startNode);

    Node getterNode = CreateGetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    getterNode.TargetGuid = guid;
    const int getterOut = getterNode.Outputs[0].Id;
    graph.Nodes.push_back(getterNode);

    Node condConst = CreateConstantNode(graph.AllocateId(), PinType::Bool);
    const int condNodeId = condConst.Id;
    const int condOut = condConst.Outputs[0].Id;
    graph.Nodes.push_back(condConst);

    Node branchNode = CreateBranchNode(graph.AllocateId());
    const int branchFlowIn = branchNode.Inputs[0].Id;
    const int branchCondIn = branchNode.Inputs[1].Id;
    const int branchTrueOut = branchNode.Outputs[0].Id;
    const int branchFalseOut = branchNode.Outputs[1].Id;
    graph.Nodes.push_back(branchNode);

    Node hundredConst = CreateConstantNode(graph.AllocateId(), PinType::Float);
    hundredConst.ConstantValue = 100.0f;
    const int hundredOut = hundredConst.Outputs[0].Id;
    graph.Nodes.push_back(hundredConst);

    Node negHundredConst = CreateConstantNode(graph.AllocateId(), PinType::Float);
    negHundredConst.ConstantValue = -100.0f;
    const int negHundredOut = negHundredConst.Outputs[0].Id;
    graph.Nodes.push_back(negHundredConst);

    Node setterTrue = CreateSetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    const int setterTrueFlowIn = setterTrue.Inputs[0].Id;
    const int setterTrueEntityIn = setterTrue.Inputs[1].Id;
    const int setterTrueMassIn = setterTrue.Inputs[2].Id;
    graph.Nodes.push_back(setterTrue);

    Node setterFalse = CreateSetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    const int setterFalseFlowIn = setterFalse.Inputs[0].Id;
    const int setterFalseEntityIn = setterFalse.Inputs[1].Id;
    const int setterFalseMassIn = setterFalse.Inputs[2].Id;
    graph.Nodes.push_back(setterFalse);

    Connect(graph, startFlowOut, branchFlowIn);
    Connect(graph, condOut, branchCondIn);
    Connect(graph, branchTrueOut, setterTrueFlowIn);
    Connect(graph, branchFalseOut, setterFalseFlowIn);
    Connect(graph, getterOut, setterTrueEntityIn);
    Connect(graph, getterOut, setterFalseEntityIn);
    Connect(graph, hundredOut, setterTrueMassIn);
    Connect(graph, negHundredOut, setterFalseMassIn);

    SECTION("condition true -> only the true-branch Setter runs")
    {
        Node* cond = graph.FindNode(condNodeId);
        REQUIRE(cond != nullptr);
        cond->ConstantValue = true;

        GraphExecutor executor(graph);
        PendingWrites writes = executor.ExecuteStartEvent(scene);
        ApplyWrites(writes, scene);

        CHECK(scene.Registry().get<BlackHoleComponent>(blackHole).Mass == Catch::Approx(100.0f));
    }

    SECTION("condition false -> only the false-branch Setter runs")
    {
        Node* cond = graph.FindNode(condNodeId);
        REQUIRE(cond != nullptr);
        cond->ConstantValue = false;

        GraphExecutor executor(graph);
        PendingWrites writes = executor.ExecuteStartEvent(scene);
        ApplyWrites(writes, scene);

        CHECK(scene.Registry().get<BlackHoleComponent>(blackHole).Mass == Catch::Approx(-100.0f));
    }
}

// For(start=0, end=3): body accumulates into a Float variable ("sum = sum + loopIndex"). Verifies
// the body fires exactly 3 times (via a Print in the body) and the final sum is 0+1+2=3. This also
// exercises a real fix made during this port: legacy's pin-value cache is only cleared once per
// Execute*Event call, never per loop iteration, so a loop body reading a pure data pin (like this
// accumulator's VariableGet) would get evaluated once and silently reused -- stale -- for every
// subsequent iteration in legacy's own design. This executor clears the cache every iteration
// instead (see ExecuteControlFlow's For branch), so the accumulation below is actually correct.
TEST_CASE("For node fires its body once per iteration and correctly accumulates into a variable",
          "[simulation][animation]")
{
    Scene scene("AnimGraphForTest");

    AnimationGraphData graph;

    Node startNode = CreateStartEventNode(graph.AllocateId());
    const int startFlowOut = startNode.Outputs[0].Id;
    graph.Nodes.push_back(startNode);

    Node zeroConst = CreateConstantNode(graph.AllocateId(), PinType::Float);
    const int zeroOut = zeroConst.Outputs[0].Id;
    graph.Nodes.push_back(zeroConst);

    Node initSumSet = CreateVariableSetNode(graph.AllocateId(), "sum", PinType::Float);
    const int initSumFlowIn = initSumSet.Inputs[0].Id;
    const int initSumValueIn = initSumSet.Inputs[1].Id;
    const int initSumFlowOut = initSumSet.Outputs[0].Id;
    graph.Nodes.push_back(initSumSet);

    Node startConst = CreateConstantNode(graph.AllocateId(), PinType::Int); // defaults to 0
    const int startConstOut = startConst.Outputs[0].Id;
    graph.Nodes.push_back(startConst);

    Node endConst = CreateConstantNode(graph.AllocateId(), PinType::Int);
    endConst.ConstantValue = 3;
    const int endConstOut = endConst.Outputs[0].Id;
    graph.Nodes.push_back(endConst);

    Node forNode = CreateForNode(graph.AllocateId());
    const int forFlowIn = forNode.Inputs[0].Id;
    const int forStartIn = forNode.Inputs[1].Id;
    const int forEndIn = forNode.Inputs[2].Id;
    const int forBodyOut = forNode.Outputs[0].Id;
    const int forIndexOut = forNode.Outputs[1].Id;
    const int forCompletedOut = forNode.Outputs[2].Id;
    graph.Nodes.push_back(forNode);

    Node getSumNode = CreateVariableGetNode(graph.AllocateId(), "sum", PinType::Float);
    const int getSumOut = getSumNode.Outputs[0].Id;
    graph.Nodes.push_back(getSumNode);

    Node addNode = CreateMathNode(graph.AllocateId(), NodeSubType::Add);
    const int addInA = addNode.Inputs[0].Id;
    const int addInB = addNode.Inputs[1].Id;
    const int addOut = addNode.Outputs[0].Id;
    graph.Nodes.push_back(addNode);

    Node bodySumSet = CreateVariableSetNode(graph.AllocateId(), "sum", PinType::Float);
    const int bodySumFlowIn = bodySumSet.Inputs[0].Id;
    const int bodySumValueIn = bodySumSet.Inputs[1].Id;
    const int bodySumFlowOut = bodySumSet.Outputs[0].Id;
    graph.Nodes.push_back(bodySumSet);

    Node bodyPrintNode = CreatePrintNode(graph.AllocateId());
    const int bodyPrintFlowIn = bodyPrintNode.Inputs[0].Id;
    const int bodyPrintValueIn = bodyPrintNode.Inputs[1].Id;
    graph.Nodes.push_back(bodyPrintNode);

    Node finalGetSumNode = CreateVariableGetNode(graph.AllocateId(), "sum", PinType::Float);
    const int finalGetSumOut = finalGetSumNode.Outputs[0].Id;
    graph.Nodes.push_back(finalGetSumNode);

    Node finalPrintNode = CreatePrintNode(graph.AllocateId());
    const int finalPrintFlowIn = finalPrintNode.Inputs[0].Id;
    const int finalPrintValueIn = finalPrintNode.Inputs[1].Id;
    graph.Nodes.push_back(finalPrintNode);

    Connect(graph, startFlowOut, initSumFlowIn);
    Connect(graph, zeroOut, initSumValueIn);
    Connect(graph, initSumFlowOut, forFlowIn);
    Connect(graph, startConstOut, forStartIn);
    Connect(graph, endConstOut, forEndIn);
    Connect(graph, forBodyOut, bodySumFlowIn);
    Connect(graph, getSumOut, addInA);
    Connect(graph, forIndexOut, addInB);
    Connect(graph, addOut, bodySumValueIn);
    Connect(graph, bodySumFlowOut, bodyPrintFlowIn);
    Connect(graph, forIndexOut, bodyPrintValueIn);
    Connect(graph, forCompletedOut, finalPrintFlowIn);
    Connect(graph, finalGetSumOut, finalPrintValueIn);

    int bodyPrintCount = 0;
    std::string finalPrinted;
    GraphExecutor executor(graph, [&](std::string s)
    {
        ++bodyPrintCount;
        finalPrinted = std::move(s);
    });

    PendingWrites writes = executor.ExecuteStartEvent(scene);
    CHECK(writes.empty()); // pure variable accumulation -- no Scene writes expected

    // bodyPrintNode fires 3 times (once per iteration), finalPrintNode fires once after the loop
    // completes -- 4 total Print invocations, with the LAST one being finalPrintNode's.
    CHECK(bodyPrintCount == 4);
    CHECK(std::stof(finalPrinted) == Catch::Approx(3.0f));
}

// A Setter targeting the Sphere category's Radius property actually changes the right entity's
// SphereComponent::Radius (resolved by Guid) -- proves the property table isn't BlackHole-only.
TEST_CASE("Setter targeting the Sphere category changes the right entity's Radius",
          "[simulation][animation]")
{
    RegisterComponents();
    Scene scene("AnimGraphSphereTest");
    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<SphereComponent>(sphere, SphereComponent{.Radius = 1.0f});
    const auto guid = scene.GuidOf(sphere);

    AnimationGraphData graph;

    Node startNode = CreateStartEventNode(graph.AllocateId());
    const int startFlowOut = startNode.Outputs[0].Id;
    graph.Nodes.push_back(startNode);

    Node getterNode = CreateGetterNode(graph.AllocateId(), NodeSubType::Sphere);
    getterNode.TargetGuid = guid;
    const int getterOut = getterNode.Outputs[0].Id;
    graph.Nodes.push_back(getterNode);

    Node radiusConst = CreateConstantNode(graph.AllocateId(), PinType::Float);
    radiusConst.ConstantValue = 5.0f;
    const int radiusOut = radiusConst.Outputs[0].Id;
    graph.Nodes.push_back(radiusConst);

    Node setterNode = CreateSetterNode(graph.AllocateId(), NodeSubType::Sphere);
    const int setterFlowIn = setterNode.Inputs[0].Id;
    const int setterEntityIn = setterNode.Inputs[1].Id;
    const int setterRadiusIn = setterNode.Inputs[2].Id; // Radius is property index 0
    graph.Nodes.push_back(setterNode);

    Connect(graph, startFlowOut, setterFlowIn);
    Connect(graph, getterOut, setterEntityIn);
    Connect(graph, radiusOut, setterRadiusIn);

    GraphExecutor executor(graph);
    PendingWrites writes = executor.ExecuteStartEvent(scene);
    ApplyWrites(writes, scene);

    CHECK(scene.Registry().get<SphereComponent>(sphere).Radius == Catch::Approx(5.0f));
}

// The SAME generic Transform Setter node (built via one unchanged CreateSetterNode(id, Transform)
// call) correctly sets Position on a BlackHole entity in one run and on a Sphere entity in
// another -- proves the "one shared category for all entity kinds" design actually works, not just
// in theory.
TEST_CASE("The same generic Transform Setter works on a BlackHole entity and on a Sphere entity",
          "[simulation][animation]")
{
    RegisterComponents();
    Scene scene("AnimGraphTransformTest");

    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<TransformComponent>(blackHole);
    scene.Registry().emplace<BlackHoleComponent>(blackHole);
    const auto blackHoleGuid = scene.GuidOf(blackHole);

    const auto sphere = scene.CreateEntity("Sphere");
    scene.Registry().emplace<TransformComponent>(sphere);
    scene.Registry().emplace<SphereComponent>(sphere);
    const auto sphereGuid = scene.GuidOf(sphere);

    auto runTransformSetter = [&](const std::uint64_t targetGuid, const glm::vec3& newPosition)
    {
        AnimationGraphData graph;

        Node startNode = CreateStartEventNode(graph.AllocateId());
        const int startFlowOut = startNode.Outputs[0].Id;
        graph.Nodes.push_back(startNode);

        Node getterNode = CreateGetterNode(graph.AllocateId(), NodeSubType::Transform);
        getterNode.TargetGuid = targetGuid;
        const int getterOut = getterNode.Outputs[0].Id;
        graph.Nodes.push_back(getterNode);

        Node positionConst = CreateConstantNode(graph.AllocateId(), PinType::Vec3);
        positionConst.ConstantValue = newPosition;
        const int positionOut = positionConst.Outputs[0].Id;
        graph.Nodes.push_back(positionConst);

        Node setterNode = CreateSetterNode(graph.AllocateId(), NodeSubType::Transform);
        const int setterFlowIn = setterNode.Inputs[0].Id;
        const int setterEntityIn = setterNode.Inputs[1].Id;
        const int setterPositionIn = setterNode.Inputs[2].Id; // Position is property index 0
        graph.Nodes.push_back(setterNode);

        Connect(graph, startFlowOut, setterFlowIn);
        Connect(graph, getterOut, setterEntityIn);
        Connect(graph, positionOut, setterPositionIn);

        GraphExecutor executor(graph);
        PendingWrites writes = executor.ExecuteStartEvent(scene);
        ApplyWrites(writes, scene);
    };

    runTransformSetter(blackHoleGuid, glm::vec3(1.0f, 2.0f, 3.0f));
    {
        const auto& pos = scene.Registry().get<TransformComponent>(blackHole).Position;
        CHECK(pos.x == Catch::Approx(1.0f));
        CHECK(pos.y == Catch::Approx(2.0f));
        CHECK(pos.z == Catch::Approx(3.0f));
    }

    runTransformSetter(sphereGuid, glm::vec3(4.0f, 5.0f, 6.0f));
    {
        const auto& pos = scene.Registry().get<TransformComponent>(sphere).Position;
        CHECK(pos.x == Catch::Approx(4.0f));
        CHECK(pos.y == Catch::Approx(5.0f));
        CHECK(pos.z == Catch::Approx(6.0f));
    }

    // The second run shouldn't have disturbed the first entity -- proves these are genuinely
    // independent entities, not the same one being written twice.
    {
        const auto& pos = scene.Registry().get<TransformComponent>(blackHole).Position;
        CHECK(pos.x == Catch::Approx(1.0f));
        CHECK(pos.y == Catch::Approx(2.0f));
        CHECK(pos.z == Catch::Approx(3.0f));
    }
}

// A Setter with an unconnected input pin leaves that property unmodified rather than crashing or
// zeroing it -- mirrors legacy's "monostate means untouched" rule.
TEST_CASE("A Setter's unconnected input pins leave their properties untouched",
          "[simulation][animation]")
{
    RegisterComponents();
    Scene scene("AnimGraphUnconnectedTest");
    const auto blackHole = scene.CreateEntity("BlackHole");
    scene.Registry().emplace<BlackHoleComponent>(
        blackHole, BlackHoleComponent{.Mass = 1.0f, .Spin = 0.25f, .Charge = 0.5f, .SpinAxis = {0.0f, 1.0f, 0.0f}});
    const auto guid = scene.GuidOf(blackHole);

    AnimationGraphData graph;

    Node startNode = CreateStartEventNode(graph.AllocateId());
    const int startFlowOut = startNode.Outputs[0].Id;
    graph.Nodes.push_back(startNode);

    Node getterNode = CreateGetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    getterNode.TargetGuid = guid;
    const int getterOut = getterNode.Outputs[0].Id;
    graph.Nodes.push_back(getterNode);

    Node massConst = CreateConstantNode(graph.AllocateId(), PinType::Float);
    massConst.ConstantValue = 42.0f;
    const int massOut = massConst.Outputs[0].Id;
    graph.Nodes.push_back(massConst);

    Node setterNode = CreateSetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    const int setterFlowIn = setterNode.Inputs[0].Id;
    const int setterEntityIn = setterNode.Inputs[1].Id;
    const int setterMassIn = setterNode.Inputs[2].Id;
    // Spin/Charge/SpinAxis (Inputs[3..5]) deliberately left unconnected.
    graph.Nodes.push_back(setterNode);

    Connect(graph, startFlowOut, setterFlowIn);
    Connect(graph, getterOut, setterEntityIn);
    Connect(graph, massOut, setterMassIn);

    GraphExecutor executor(graph);
    PendingWrites writes;
    CHECK_NOTHROW(writes = executor.ExecuteStartEvent(scene));
    for (auto& write : writes) { CHECK_NOTHROW(write(scene)); }

    const auto& blackHoleData = scene.Registry().get<BlackHoleComponent>(blackHole);
    CHECK(blackHoleData.Mass == Catch::Approx(42.0f));
    CHECK(blackHoleData.Spin == Catch::Approx(0.25f));
    CHECK(blackHoleData.Charge == Catch::Approx(0.5f));
    CHECK(blackHoleData.SpinAxis.x == Catch::Approx(0.0f));
    CHECK(blackHoleData.SpinAxis.y == Catch::Approx(1.0f));
    CHECK(blackHoleData.SpinAxis.z == Catch::Approx(0.0f));
}

// SerializeToYaml/DeserializeFromYaml round-trips a graph containing one of every implemented node
// kind, plus links and a variable, producing an equivalent AnimationGraphData. Pure data-model test
// -- no GPP::Scene involved at all.
TEST_CASE("SerializeToYaml/DeserializeFromYaml round-trips a graph with every implemented node kind",
          "[simulation][animation]")
{
    AnimationGraphData graph;
    graph.Variables.push_back(Variable{.Name = "counter", .Type = PinType::Float});

    Node startNode = CreateStartEventNode(graph.AllocateId());
    const int startFlowOut = startNode.Outputs[0].Id;
    graph.Nodes.push_back(startNode);

    Node tickNode = CreateTickEventNode(graph.AllocateId());
    graph.Nodes.push_back(tickNode);

    Node constNode = CreateConstantNode(graph.AllocateId(), PinType::Float);
    constNode.ConstantValue = 2.5f;
    const int constNodeId = constNode.Id;
    const int constOut = constNode.Outputs[0].Id;
    graph.Nodes.push_back(constNode);

    Node addNode = CreateMathNode(graph.AllocateId(), NodeSubType::Add);
    const int addInA = addNode.Inputs[0].Id;
    graph.Nodes.push_back(addNode);

    Node getterNode = CreateGetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    getterNode.TargetGuid = 123456789ull;
    graph.Nodes.push_back(getterNode);

    Node decomposerNode = CreateDecomposerNode(graph.AllocateId(), NodeSubType::BlackHole);
    graph.Nodes.push_back(decomposerNode);

    Node setterNode = CreateSetterNode(graph.AllocateId(), NodeSubType::BlackHole);
    const int setterFlowIn = setterNode.Inputs[0].Id;
    graph.Nodes.push_back(setterNode);

    Node branchNode = CreateBranchNode(graph.AllocateId());
    graph.Nodes.push_back(branchNode);

    Node forNode = CreateForNode(graph.AllocateId());
    graph.Nodes.push_back(forNode);

    Node printNode = CreatePrintNode(graph.AllocateId());
    graph.Nodes.push_back(printNode);

    Node varGetNode = CreateVariableGetNode(graph.AllocateId(), "counter", PinType::Float);
    graph.Nodes.push_back(varGetNode);

    Node varSetNode = CreateVariableSetNode(graph.AllocateId(), "counter", PinType::Float);
    graph.Nodes.push_back(varSetNode);

    Connect(graph, startFlowOut, setterFlowIn);
    Connect(graph, constOut, addInA);

    const std::string yaml = SerializeToYaml(graph);
    const AnimationGraphData restored = DeserializeFromYaml(yaml);

    REQUIRE(restored.Nodes.size() == graph.Nodes.size());
    REQUIRE(restored.Links.size() == graph.Links.size());
    REQUIRE(restored.Variables.size() == graph.Variables.size());
    CHECK(restored.NextId == graph.NextId);

    CHECK(restored.Variables[0].Name == "counter");
    CHECK(restored.Variables[0].Type == PinType::Float);

    for (std::size_t i = 0; i < graph.Nodes.size(); ++i)
    {
        const auto& original = graph.Nodes[i];
        const auto& copy = restored.Nodes[i];
        CHECK(copy.Id == original.Id);
        CHECK(copy.Name == original.Name);
        CHECK(copy.Type == original.Type);
        CHECK(copy.SubType == original.SubType);
        CHECK(copy.VariableName == original.VariableName);
        CHECK(copy.TargetGuid == original.TargetGuid);
        REQUIRE(copy.Inputs.size() == original.Inputs.size());
        REQUIRE(copy.Outputs.size() == original.Outputs.size());
        for (std::size_t p = 0; p < original.Inputs.size(); ++p)
        {
            CHECK(copy.Inputs[p].Id == original.Inputs[p].Id);
            CHECK(copy.Inputs[p].Name == original.Inputs[p].Name);
            CHECK(copy.Inputs[p].Type == original.Inputs[p].Type);
            CHECK(copy.Inputs[p].IsInput == original.Inputs[p].IsInput);
        }
        for (std::size_t p = 0; p < original.Outputs.size(); ++p)
        {
            CHECK(copy.Outputs[p].Id == original.Outputs[p].Id);
            CHECK(copy.Outputs[p].Name == original.Outputs[p].Name);
            CHECK(copy.Outputs[p].Type == original.Outputs[p].Type);
            CHECK(copy.Outputs[p].IsInput == original.Outputs[p].IsInput);
        }
    }

    for (std::size_t i = 0; i < graph.Links.size(); ++i)
    {
        CHECK(restored.Links[i].Id == graph.Links[i].Id);
        CHECK(restored.Links[i].StartPinId == graph.Links[i].StartPinId);
        CHECK(restored.Links[i].EndPinId == graph.Links[i].EndPinId);
    }

    const Node* restoredConst = restored.FindNode(constNodeId);
    REQUIRE(restoredConst != nullptr);
    REQUIRE(std::holds_alternative<float>(restoredConst->ConstantValue));
    CHECK(std::get<float>(restoredConst->ConstantValue) == Catch::Approx(2.5f));
}
