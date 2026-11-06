#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;
namespace fs = std::filesystem;

namespace
{
    constexpr const char* kProbe = R"(
return {
    OnStart = function(self) events.emit("log", "start") end,
    OnTick = function(self, dt)
        local x, y, z = scene.get_xyz(self.entity, "Transform", "Position")
        scene.set_xyz(self.entity, "Transform", "Position", x + input.axis("move_x") * 0.5, y, z)
        if input.pressed("jump") then events.emit("log", "jump") end
    end,
    OnLateTick = function(self, dt) events.emit("log", "late") end,
    OnContact = function(self, other) events.emit("log", "contact") end,
    OnStop = function(self) events.emit("log", "stop") end,
}
)";

    struct Harness
    {
        fs::path Root = fs::temp_directory_path() / "molehole_script_input_wiring";
        std::shared_ptr<EventDispatcher> Dispatcher = std::make_shared<EventDispatcher>();
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();
        std::unique_ptr<AssetDirectories> Assets;
        std::unique_ptr<SceneManager> Scenes;
        AppSimulationModules Modules;
        std::shared_ptr<SimulationRunner> Runner;
        std::mutex LogMutex;
        std::vector<std::string> Events;
        std::uint64_t Probe{0};

        explicit Harness(const bool bodies = false)
        {
            RegisterComponents();
            fs::remove_all(Root);
            fs::create_directories(Root);
            std::ofstream(Root / "probe.luau") << kProbe;
            AssetOptions options;
            options.Kinds[kScriptAssetKind] = AssetKindOptions{{Root}, {".luau"}};
            Assets = std::make_unique<AssetDirectories>(options);
            Scenes = std::make_unique<SceneManager>(Log, std::make_shared<FileSystem>());

            Scene scene("Wiring");
            scene.SetSeed(9);
            const auto probe = scene.CreateEntity("Probe");
            scene.Registry().emplace<TransformComponent>(probe, TransformComponent{.Position = {0.0, 3.0, 0.0}});
            scene.Registry().emplace<ScriptsComponent>(probe).Entries.push_back(ScriptEntry{.Name = "probe"});
            Probe = scene.GuidOf(probe);
            if (bodies)
            {
                scene.Registry().emplace<VelocityComponent>(probe, VelocityComponent{.Linear = {0.0, -4.0, 0.0}});
                scene.Registry().emplace<RigidBodyComponent>(probe, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0, .EnableGravity = false});
                scene.Registry().emplace<ColliderComponent>(probe, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5});
                const auto other = scene.CreateEntity("Other");
                scene.Registry().emplace<TransformComponent>(other, TransformComponent{.Position = {0.0, 0.0, 0.0}});
                scene.Registry().emplace<VelocityComponent>(other, VelocityComponent{.Linear = {0.0, -1.0, 0.0}});
                scene.Registry().emplace<RigidBodyComponent>(other, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 1.0, .EnableGravity = false});
                scene.Registry().emplace<ColliderComponent>(other, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5});
            }

            Modules = MakeAppSimulationModules(Dispatcher, Log, *Assets);
            (void)Modules.Scripts->Host().Events().Subscribe("log", [this](const ScriptEvent& event)
            {
                std::scoped_lock lock(LogMutex);
                Events.push_back(std::get<std::string>(event.Args.at(0)));
            });
            Runner = Scenes->CreateSimulation(std::move(scene), Modules.All());
            Modules.Attach(*Runner);
            Runner->Start();
            Runner->SetPaused(true);
            Runner->SetManualStepping(true);
        }

        ~Harness()
        {
            Runner.reset();
            fs::remove_all(Root);
        }

        [[nodiscard]] std::size_t Count(const std::string& name)
        {
            std::scoped_lock lock(LogMutex);
            return static_cast<std::size_t>(std::ranges::count(Events, name));
        }

        [[nodiscard]] glm::dvec3 Position() const
        {
            const auto snapshot = Runner->AcquireSnapshot();
            return snapshot->Registry().get<TransformComponent>(snapshot->FindByGuid(Probe)).Position;
        }

        template <typename Pred>
        bool WaitFor(Pred&& pred)
        {
            for (int i = 0; i < 300; ++i)
            {
                if (pred()) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            return pred();
        }
    };
}

TEST_CASE("The GUI module stack delivers late ticks, contacts and OnStop at Play->Stop", "[simulation][scripts][input]")
{
    Harness h(true);
    auto session = std::make_shared<PlaySession>();
    CHECK(session->Resume(h.Runner));
    REQUIRE(h.WaitFor([&] { return !h.Runner->IsPaused(); }));
    h.Runner->StepAndWait(90);

    CHECK(h.Count("start") == 1);
    CHECK(h.Count("late") >= 90);
    CHECK(h.Count("contact") == 1);
    CHECK(h.Position().y < 2.9);
    CHECK(h.Count("stop") == 0);

    session->Stop(h.Runner, std::nullopt);
    REQUIRE(h.WaitFor([&] { return h.Count("stop") == 1; }));
    REQUIRE(h.WaitFor([&] { return h.Position().y == Approx(3.0); }));
    CHECK(h.Modules.Scripts->Statuses().empty());

    CHECK(session->Resume(h.Runner));
    REQUIRE(h.WaitFor([&] { return !h.Runner->IsPaused(); }));
    h.Runner->StepAndWait(2);
    CHECK(h.Count("start") == 2);
}

TEST_CASE("A paused scene never runs its scripts", "[simulation][scripts]")
{
    Harness h;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(h.Count("start") == 0);
    CHECK(h.Modules.Scripts->Host().InstanceCount() == 0);
}

TEST_CASE("Input injected into the runner moves a script and replays to the same hash", "[simulation][scripts][input][replay]")
{
    Harness h;
    auto session = std::make_shared<PlaySession>();
    session->Resume(h.Runner);
    REQUIRE(h.WaitFor([&] { return !h.Runner->IsPaused(); }));
    h.Runner->StepAndWait(2);
    CHECK(h.Position().x == Approx(0.0));

    h.Runner->BeginRecording(5);
    h.Runner->Input()->Set("move_x", 1.0f);
    h.Runner->StepAndWait(4);
    h.Runner->Input()->Set("jump", 1.0f);
    h.Runner->StepAndWait(3);
    h.Runner->Input()->Set("jump", 0.0f);
    h.Runner->Input()->Set("move_x", -1.0f);
    h.Runner->StepAndWait(3);
    const auto recording = h.Runner->EndRecording();
    CHECK(h.Position().x == Approx(0.5 * (7 - 3)));
    CHECK(h.Count("jump") == 1);
    REQUIRE(recording.FinalHash.has_value());
    CHECK_FALSE(recording.Inputs.empty());

    const auto verify = [&](const SimulationRecording& which)
    {
        auto modules = MakeAppSimulationModules(std::make_shared<EventDispatcher>(), std::make_shared<Logger>(), *h.Assets);
        ReplayOptions options;
        options.OnRunnerCreated = [&](SimulationRunner& runner) { modules.Attach(runner); };
        return VerifyReplay(which, modules.All(), options);
    };
    const auto ok = verify(recording);
    INFO(ok.Message);
    CHECK(ok.Ok);

    auto tampered = recording;
    tampered.Inputs.clear();
    CHECK_FALSE(verify(tampered).Ok);
}

TEST_CASE("Scene input defaults sit between the built-ins and the user's rebinds", "[input]")
{
    Scene scene("Bindings");
    scene.DeserializeFromYaml(R"(Scene: Bindings
Input:
  Actions:
    fire: [key:f]
    thrust: [key:w, -key:s]
Entities: []
)");
    CHECK(GPP::CountDiagnostics(GPP::ValidateSceneYaml(scene.SerializeToYaml()), GPP::DiagnosticSeverity::Error) == 0);

    std::string problem;
    auto map = EffectiveActionMap(&scene, {}, &problem);
    CHECK(problem.empty());
    CHECK(map.Find("fire")->Bindings == std::vector<InputBinding>{{"key:f", 1.0f}});
    CHECK(map.Find("thrust")->Bindings.size() == 2);
    CHECK(map.Find("move_x") != nullptr);

    ActionMap user;
    user.Set("fire", {{"mouse:right", 1.0f}});
    map = EffectiveActionMap(&scene, user);
    CHECK(map.Find("fire")->Bindings.front().Source == "mouse:right");
    CHECK(EffectiveActionMap(nullptr, user).Find("thrust") == nullptr);

    Scene broken("Broken");
    broken.DeserializeFromYaml("Scene: Broken\nInput:\n  Actions:\n    fire: [nope]\nEntities: []\n");
    CHECK(EffectiveActionMap(&broken, {}, &problem).Find("fire")->Bindings.front().Source == "mouse:left");
    CHECK_FALSE(problem.empty());
}

TEST_CASE("Script input is captured only while playing, focused and not released with Esc", "[input]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    auto state = std::make_shared<InputState>(dispatcher);
    ScriptInputBridge bridge(dispatcher, state);
    auto target = std::make_shared<SimulationInput>();
    bridge.SetTarget(target);
    bridge.SetMap(ActionMap::Defaults());

    const auto key = [&](const KeyCode code, const bool down) { dispatcher->Publish(KeyEvent{1, code, ScanCode::Unknown, down, false}); };
    const auto sample = [&] { return target->Sample(); };

    ScriptInputFrame frame{.Playing = true, .ScriptsUseInput = true, .ViewportFocused = true};
    key(KeyCode::W, true);
    CHECK(sample().Values.empty());

    CHECK(bridge.Update(frame));
    key(KeyCode::W, true);
    dispatcher->Publish(MouseMotionEvent{1, 0, 0, 3.0f, -1.0f});
    dispatcher->Publish(MouseButtonEvent{1, MouseButton::Left, true});
    auto first = sample();
    CHECK(first.Values.at("move_z") == 1.0f);
    CHECK(first.Values.at("fire") == 1.0f);
    CHECK(first.Mouse == glm::vec2(3.0f, -1.0f));
    CHECK(first.Scroll == 0.0f);

    bridge.AddScroll(1.0f);
    bridge.AddScroll(0.5f);
    CHECK(sample().Scroll == 1.5f);

    key(KeyCode::Escape, true);
    CHECK_FALSE(bridge.Captured());
    CHECK_FALSE(bridge.Update(frame));
    CHECK(bridge.WaitingForClick());
    auto released = sample();
    CHECK(released.Values.empty());
    CHECK(released.Released.contains("move_z"));
    key(KeyCode::D, true);
    CHECK(sample().Values.empty());
    bridge.AddScroll(3.0f);
    CHECK(sample().Scroll == 0.0f);

    frame.ViewportClicked = true;
    CHECK(bridge.Update(frame));
    frame.ViewportClicked = false;
    CHECK_FALSE(bridge.WaitingForClick());

    frame.TextInput = true;
    CHECK_FALSE(bridge.Update(frame));
    frame.TextInput = false;
    frame.ViewportFocused = false;
    CHECK_FALSE(bridge.Update(frame));
    frame.ViewportFocused = true;
    frame.Playing = false;
    CHECK_FALSE(bridge.Update(frame));
    frame.Playing = true;
    frame.ScriptsUseInput = false;
    CHECK_FALSE(bridge.Update(frame));
    CHECK_FALSE(bridge.WaitingForClick());
}

TEST_CASE("Rebinding captures the next key and Esc cancels it", "[input]")
{
    auto dispatcher = std::make_shared<EventDispatcher>();
    ScriptInputBridge bridge(dispatcher, std::make_shared<InputState>(dispatcher));
    auto& settings = ScriptInputSettings::Instance();
    settings.CancelCapture();

    settings.ArmCapture();
    dispatcher->Publish(KeyEvent{1, KeyCode::LeftShift, ScanCode::Unknown, true, false});
    CHECK(settings.TakeCaptured() == "key:shift");
    CHECK_FALSE(settings.Capturing());

    settings.ArmCapture();
    dispatcher->Publish(KeyEvent{1, KeyCode::Escape, ScanCode::Unknown, true, false});
    CHECK_FALSE(settings.Capturing());
    CHECK_FALSE(settings.TakeCaptured().has_value());

    const auto before = settings.Revision();
    ActionMap user;
    user.Set("fire", {{"key:f", 1.0f}});
    settings.SetUser(user);
    CHECK(settings.Revision() == before + 1);
    settings.SetUser(user);
    CHECK(settings.Revision() == before + 1);
    settings.SetUser({});
}

TEST_CASE("Animation graphs run on the sim thread and stop with the play session", "[simulation][scripts][graphs]")
{
    RegisterComponents();
    std::ifstream stream(fs::path(MOLEHOLE_SOURCE_DIR) / "tests" / "fixtures" / "scene-with-main-graph.yaml");
    const std::string text{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    Scene scene("Graphs");
    scene.DeserializeFromYaml(text);

    const fs::path root = fs::temp_directory_path() / "molehole_graph_module_tests";
    fs::create_directories(root);
    AssetOptions options;
    options.Kinds[kScriptAssetKind] = AssetKindOptions{{root}, {".luau"}};
    AssetDirectories assets(options);

    std::mutex mutex;
    std::vector<std::string> prints;
    auto modules = MakeAppSimulationModules(std::make_shared<EventDispatcher>(), std::make_shared<Logger>(), assets, {},
                                            [&](std::string message)
                                            {
                                                std::scoped_lock lock(mutex);
                                                prints.push_back(std::move(message));
                                            });
    SceneManager scenes(std::make_shared<Logger>(), std::make_shared<FileSystem>());
    auto runner = scenes.CreateSimulation(std::move(scene), modules.All());
    modules.Attach(*runner);
    runner->Start();
    runner->SetPaused(true);
    runner->SetManualStepping(true);

    CHECK_FALSE(modules.Graphs->Running());
    auto session = std::make_shared<PlaySession>();
    session->Resume(runner);
    for (int i = 0; i < 300 && runner->IsPaused(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    runner->StepAndWait(3);
    CHECK(modules.Graphs->Running());
    {
        std::scoped_lock lock(mutex);
        CHECK_FALSE(prints.empty());
    }
    CHECK(modules.Graphs->Trace().Tick() >= 3);

    session->Stop(runner, std::nullopt);
    for (int i = 0; i < 300 && modules.Graphs->Running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    CHECK_FALSE(modules.Graphs->Running());
    runner.reset();
    fs::remove_all(root);
}
