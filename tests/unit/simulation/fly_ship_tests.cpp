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
    constexpr std::uint64_t kShip = 5101000000000000002ULL;
    constexpr std::uint64_t kChase = 5101000000000000003ULL;
    constexpr std::uint64_t kCockpit = 5101000000000000004ULL;
    constexpr std::uint64_t kFly = 5101000000000000005ULL;
    constexpr std::uint64_t kFrame = 5101000000000000008ULL;

    struct FlyShip
    {
        fs::path Source{MOLEHOLE_SOURCE_DIR};
        std::shared_ptr<EventDispatcher> Dispatcher = std::make_shared<EventDispatcher>();
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();
        std::unique_ptr<AssetDirectories> Assets;
        std::unique_ptr<SceneManager> Scenes;
        AppSimulationModules Modules;
        std::shared_ptr<SimulationRunner> Runner;
        std::shared_ptr<PlaySession> Session = std::make_shared<PlaySession>();

        FlyShip()
        {
            RegisterComponents();
            AssetOptions options;
            options.Kinds[kScriptAssetKind] = AssetKindOptions{{Source / "scripts"}, {".luau"}};
            Assets = std::make_unique<AssetDirectories>(options);
            Scenes = std::make_unique<SceneManager>(Log, std::make_shared<FileSystem>());

            std::ifstream stream(Source / "templates" / "fly-ship.yaml");
            const std::string yaml{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
            Scene scene("Fly");
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

        [[nodiscard]] glm::dvec3 ShipPosition() const
        {
            const auto snapshot = Runner->AcquireSnapshot();
            return snapshot->Registry().get<TransformComponent>(snapshot->FindByGuid(kShip)).Position;
        }

        [[nodiscard]] glm::dvec3 ShipVelocity() const
        {
            const auto snapshot = Runner->AcquireSnapshot();
            return snapshot->Registry().get<VelocityComponent>(snapshot->FindByGuid(kShip)).Linear;
        }

        [[nodiscard]] bool Primary(const std::uint64_t guid) const
        {
            const auto snapshot = Runner->AcquireSnapshot();
            return snapshot->Registry().get<CameraComponent>(snapshot->FindByGuid(guid)).Primary;
        }

        void Tap(const std::string& action)
        {
            Runner->Input()->Set(action, 1.0f);
            Runner->StepAndWait(1);
            Runner->Input()->Set(action, 0.0f);
        }
    };

    ReplayVerification Verify(const SimulationRecording& recording, FlyShip& source)
    {
        auto modules = MakeAppSimulationModules(std::make_shared<EventDispatcher>(), std::make_shared<Logger>(), *source.Assets);
        ReplayOptions options;
        options.OnRunnerCreated = [&](SimulationRunner& runner) { modules.Attach(runner); };
        return VerifyReplay(recording, modules.All(), options);
    }
}

TEST_CASE("The fly-ship template runs its seven scripts without errors and stays in orbit", "[simulation][scripts][fly-ship]")
{
    FlyShip f;
    f.Runner->StepAndWait(300);
    CHECK(f.Modules.Scripts->TakeErrors().empty());
    CHECK(f.Modules.Scripts->Host().InstanceCount() == 7);
    const auto radius = glm::length(f.ShipPosition());
    CHECK(radius > 100.0 * 1476.0);
    CHECK(radius < 600.0 * 1476.0);
    CHECK(f.Primary(kChase));
    CHECK_FALSE(f.Primary(kCockpit));
}

TEST_CASE("Pressing C on the fly-ship template cycles the camera", "[simulation][scripts][fly-ship]")
{
    FlyShip f;
    f.Runner->StepAndWait(5);
    f.Tap("camera");
    f.Runner->StepAndWait(3);
    CHECK_FALSE(f.Primary(kChase));
    CHECK(f.Primary(kCockpit));
    CHECK(f.Modules.Scripts->TakeErrors().empty());
}

TEST_CASE("C walks chase, cockpit, fly and frame lock and comes back", "[simulation][scripts][fly-ship]")
{
    FlyShip f;
    f.Runner->StepAndWait(5);
    for (const auto guid : {kCockpit, kFly, kFrame, kChase})
    {
        f.Tap("camera");
        f.Runner->StepAndWait(3);
        CHECK(f.Primary(guid));
    }
    CHECK(f.Modules.Scripts->TakeErrors().empty());
}

TEST_CASE("The telemetry HUD publishes commands the viewport can draw", "[simulation][scripts][fly-ship][hud]")
{
    FlyShip f;
    f.Runner->StepAndWait(5);
    const auto frame = f.Modules.Scripts->HudSnapshot();
    CHECK(f.Modules.Scripts->TakeErrors().empty());
    REQUIRE(frame->Commands.size() > 40);
    const auto has = [&](const std::string_view needle)
    {
        return std::ranges::any_of(frame->Commands, [&](const HudCommand& command)
        {
            return command.Kind == HudKind::Text && command.Text.find(needle) != std::string::npos;
        });
    };
    CHECK(has("SPEED"));
    CHECK(has("Mm/s"));
    CHECK(has("THR"));
    CHECK(has("HDG"));
    f.Tap("hud");
    f.Tap("hud");
    f.Runner->StepAndWait(2);
    CHECK(f.Modules.Scripts->HudSnapshot()->Commands.empty());
}

TEST_CASE("Throttle, speed hold and kill velocity act on the template ship", "[simulation][scripts][fly-ship]")
{
    FlyShip coast;
    coast.Runner->StepAndWait(120);
    FlyShip throttled;
    throttled.Runner->Input()->Set("throttle", 1.0f);
    throttled.Runner->StepAndWait(60);
    throttled.Runner->Input()->Set("throttle", 0.0f);
    throttled.Runner->StepAndWait(60);
    CHECK(throttled.Modules.Scripts->TakeErrors().empty());
    CHECK(glm::length(throttled.ShipVelocity()) > glm::length(coast.ShipVelocity()) * 1.01);

    FlyShip braked;
    braked.Runner->Input()->Set("kill_velocity", 1.0f);
    braked.Runner->StepAndWait(120);
    CHECK(braked.Modules.Scripts->TakeErrors().empty());
    CHECK(glm::length(braked.ShipVelocity()) < glm::length(coast.ShipVelocity()) * 0.98);
}

TEST_CASE("Thrust input changes the orbit of the fly-ship template", "[simulation][scripts][fly-ship]")
{
    FlyShip coast;
    coast.Runner->StepAndWait(120);
    FlyShip burn;
    burn.Runner->Input()->Set("move_z", 1.0f);
    burn.Runner->StepAndWait(120);
    CHECK(burn.Modules.Scripts->TakeErrors().empty());
    CHECK(glm::length(burn.ShipVelocity()) > glm::length(coast.ShipVelocity()) * 1.02);
}

TEST_CASE("Recorded flight input replays to the same hash twice", "[simulation][scripts][fly-ship][replay]")
{
    FlyShip f;
    f.Runner->BeginRecording(60);
    f.Runner->Input()->Set("move_z", 1.0f);
    f.Runner->StepAndWait(40);
    f.Runner->Input()->Set("boost", 1.0f);
    f.Runner->Input()->Set("yaw", 1.0f);
    f.Runner->StepAndWait(30);
    f.Runner->Input()->Set("boost", 0.0f);
    f.Runner->Input()->Set("yaw", 0.0f);
    f.Runner->Input()->Set("pitch", -1.0f);
    f.Runner->Input()->Set("move_z", 0.0f);
    f.Runner->Input()->Set("move_x", -1.0f);
    f.Runner->StepAndWait(30);
    f.Tap("assist");
    f.Tap("camera");
    f.Tap("control_mode");
    f.Tap("speed_hold");
    f.Runner->Input()->Set("throttle", 1.0f);
    f.Runner->Input()->Set("cam_yaw", 1.0f);
    f.Runner->StepAndWait(10);
    f.Runner->Input()->Set("throttle", 0.0f);
    f.Runner->Input()->Set("cam_yaw", 0.0f);
    f.Runner->Input()->Set("kill_velocity", 1.0f);
    f.Runner->StepAndWait(10);
    f.Runner->Input()->Set("kill_velocity", 0.0f);
    f.Tap("hud");
    f.Tap("camera");
    f.Runner->Input()->Set("pitch", 0.0f);
    f.Runner->Input()->Set("move_x", 0.0f);
    f.Runner->StepAndWait(40);
    const auto recording = f.Runner->EndRecording();
    CHECK(f.Modules.Scripts->TakeErrors().empty());
    REQUIRE(recording.FinalHash.has_value());
    CHECK_FALSE(recording.Inputs.empty());

    const auto first = Verify(recording, f);
    const auto second = Verify(recording, f);
    INFO(first.Message);
    INFO(second.Message);
    CHECK(first.Ok);
    CHECK(second.Ok);
    CHECK(first.FinalHash == second.FinalHash);
    CHECK(first.FinalHash == *recording.FinalHash);

    auto tampered = recording;
    tampered.Inputs.clear();
    CHECK_FALSE(Verify(tampered, f).Ok);
}

TEST_CASE("The autopilot circularises the template orbit and replays to the same hash", "[simulation][scripts][fly-ship][replay]")
{
    constexpr double kHoleMass = 1.9884098706980507e30;
    const double mu = Units::kGravitationalConstant * kHoleMass;
    const auto eccentricity = [&](FlyShip& flight)
    {
        const auto r = flight.ShipPosition();
        const auto v = flight.ShipVelocity();
        const auto h = glm::cross(r, v);
        const double energy = 0.5 * glm::dot(v, v) - mu / glm::length(r);
        return std::sqrt(std::max(0.0, 1.0 + 2.0 * energy * glm::dot(h, h) / (mu * mu)));
    };

    FlyShip f;
    f.Runner->BeginRecording(60);
    f.Runner->StepAndWait(300);
    CHECK(eccentricity(f) > 0.3);
    f.Tap("autopilot");
    f.Runner->StepAndWait(3600);
    CHECK(f.Modules.Scripts->TakeErrors().empty());
    CHECK(eccentricity(f) < 0.05);
    const auto frame = f.Modules.Scripts->HudSnapshot();
    CHECK(std::ranges::any_of(frame->Commands, [](const HudCommand& command)
    {
        return command.Kind == HudKind::Text && command.Text.find("AUTOPILOT ORBIT") != std::string::npos;
    }));
    const auto recording = f.Runner->EndRecording();
    REQUIRE(recording.FinalHash.has_value());
    const auto first = Verify(recording, f);
    INFO(first.Message);
    CHECK(first.Ok);
    CHECK(first.FinalHash == *recording.FinalHash);
}
