#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
namespace fs = std::filesystem;

namespace
{
    constexpr std::uint64_t kCamera = 4200000000000000021ULL;
    constexpr std::uint64_t kProbe = 4200000000000000003ULL;
    constexpr std::uint64_t kGate1 = 4200000000000000010ULL;
    constexpr std::uint64_t kGate3 = 4200000000000000012ULL;
    constexpr std::uint64_t kMoon = 4200000000000000002ULL;
    constexpr std::uint64_t kFocus = 4200000000000000020ULL;

    struct WaypointDemo
    {
        fs::path Source{MOLEHOLE_SOURCE_DIR};
        std::shared_ptr<EventDispatcher> Dispatcher = std::make_shared<EventDispatcher>();
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();
        std::unique_ptr<AssetDirectories> Assets;
        std::unique_ptr<SceneManager> Scenes;
        AppSimulationModules Modules;
        std::shared_ptr<SimulationRunner> Runner;
        std::shared_ptr<PlaySession> Session = std::make_shared<PlaySession>();

        WaypointDemo()
        {
            RegisterComponents();
            AssetOptions options;
            options.Kinds[kScriptAssetKind] = AssetKindOptions{{Source / "scripts"}, {".luau"}};
            Assets = std::make_unique<AssetDirectories>(options);
            Scenes = std::make_unique<SceneManager>(Log, std::make_shared<FileSystem>());

            std::ifstream stream(Source / "templates" / "waypoint-demo.yaml");
            const std::string yaml{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            Scene scene("Waypoints");
            scene.DeserializeFromYaml(yaml);
            Modules = MakeAppSimulationModules(Dispatcher, Log, *Assets);
            Runner = Scenes->CreateSimulation(std::move(scene), Modules.All());
            Modules.Attach(*Runner);
            Runner->Start();
            Runner->SetPaused(true);
            Runner->SetManualStepping(true);
            Session->Resume(Runner);
            for (int i = 0; i < 300 && Runner->IsPaused(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        [[nodiscard]] glm::dvec3 PositionOf(const std::uint64_t guid) const
        {
            const auto snapshot = Runner->AcquireSnapshot();
            return snapshot->Registry().get<TransformComponent>(snapshot->FindByGuid(guid)).Position;
        }

        [[nodiscard]] float Fov() const
        {
            const auto snapshot = Runner->AcquireSnapshot();
            return snapshot->Registry().get<CameraComponent>(snapshot->FindByGuid(kCamera)).Fov;
        }

    };

    ReplayVerification Verify(const SimulationRecording& recording, WaypointDemo& source)
    {
        auto modules = MakeAppSimulationModules(std::make_shared<EventDispatcher>(), std::make_shared<Logger>(), *source.Assets);
        ReplayOptions options;
        options.OnRunnerCreated = [&](SimulationRunner& runner) { modules.Attach(runner); };
        return VerifyReplay(recording, modules.All(), options);
    }
}

TEST_CASE("The waypoint demo runs its three paths without script errors", "[simulation][scripts][waypoint]")
{
    WaypointDemo d;
    d.Runner->StepAndWait(300);
    CHECK(d.Modules.Scripts->TakeErrors().empty());
    CHECK(d.Modules.Scripts->Host().InstanceCount() == 3);
}

TEST_CASE("The demo camera starts on the first gate and meets the third at its time", "[simulation][scripts][waypoint]")
{
    WaypointDemo d;
    d.Runner->StepAndWait(1);
    CHECK(glm::distance(d.PositionOf(kCamera), d.PositionOf(kGate1)) < 1e-3);
    d.Runner->StepAndWait(600);
    CHECK(glm::distance(d.PositionOf(kCamera), d.PositionOf(kGate3)) < 1e-3);
    CHECK(d.Modules.Scripts->TakeErrors().empty());
}

TEST_CASE("The demo focus ends on the Moon and the camera field of view narrows", "[simulation][scripts][waypoint]")
{
    WaypointDemo d;
    d.Runner->StepAndWait(1);
    CHECK(d.Fov() == 50.0f);
    d.Runner->StepAndWait(1800);
    CHECK(glm::distance(d.PositionOf(kFocus), d.PositionOf(kMoon)) < 1e-3);
    CHECK(d.Fov() == 25.0f);
}

TEST_CASE("The demo probe moves the same distance every tick", "[simulation][scripts][waypoint]")
{
    WaypointDemo d;
    d.Runner->StepAndWait(2);
    auto previous = d.PositionOf(kProbe);
    for (int i = 0; i < 120; ++i)
    {
        d.Runner->StepAndWait(1);
        const auto now = d.PositionOf(kProbe);
        CHECK(glm::distance(now, previous) == Catch::Approx(2.0e6 / 60.0).epsilon(0.01));
        previous = now;
    }
}

TEST_CASE("Two waypoint demo runs agree on every path position", "[simulation][scripts][waypoint]")
{
    WaypointDemo a;
    WaypointDemo b;
    for (const int n : {1, 10, 100, 400})
    {
        a.Runner->StepAndWait(n);
        b.Runner->StepAndWait(n);
        for (const auto guid : {kCamera, kProbe, kFocus}) CHECK(a.PositionOf(guid) == b.PositionOf(guid));
        CHECK(a.Fov() == b.Fov());
    }
}

TEST_CASE("The waypoint demo replays to the recorded hash", "[simulation][scripts][waypoint][replay]")
{
    WaypointDemo d;
    d.Runner->BeginRecording(60);
    d.Runner->StepAndWait(400);
    const auto recording = d.Runner->EndRecording();
    REQUIRE(recording.FinalHash.has_value());
    const auto first = Verify(recording, d);
    const auto second = Verify(recording, d);
    INFO(first.Message);
    CHECK(first.Ok);
    CHECK(second.Ok);
    CHECK(first.FinalHash == *recording.FinalHash);
    CHECK(d.Modules.Scripts->TakeErrors().empty());
}

TEST_CASE("Waypoint script state saves and loads back unchanged", "[simulation][scripts][waypoint][state]")
{
    WaypointDemo a;
    a.Runner->StepAndWait(100);
    REQUIRE(a.Modules.Scripts->TakeErrors().empty());
    const auto saved = a.Modules.Scripts->SaveState();
    WaypointDemo b;
    b.Modules.Scripts->LoadState(saved);
    CHECK(b.Modules.Scripts->SaveState() == saved);
    CHECK(saved.find("evaluate") == std::string::npos);
}
