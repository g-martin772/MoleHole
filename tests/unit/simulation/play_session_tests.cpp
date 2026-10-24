#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
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

    double PositionX(const SimulationRunner& runner, const std::uint64_t guid)
    {
        const auto snapshot = runner.AcquireSnapshot();
        const auto entity = snapshot->FindByGuid(guid);
        return snapshot->Registry().get<TransformComponent>(entity).Position.x;
    }
}

TEST_CASE("Stop restores the pre-play scene but keeps render settings edited while playing", "[simulation][playstop][rendering]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());

    Scene scene("PlaySession");
    const auto entity = scene.CreateEntity("Mover");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {1.0, 0.0, 0.0}});
    const auto guid = scene.GuidOf(entity);
    auto runner = scenes.CreateSimulation(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);

    auto session = std::make_shared<PlaySession>();
    CHECK_FALSE(session->Active());
    CHECK(session->Resume(runner));
    CHECK(session->Active());
    REQUIRE(WaitFor([&] { return !runner->IsPaused(); }));

    runner->EnqueueEdit([guid](Scene& s)
    {
        s.Registry().get<TransformComponent>(s.FindByGuid(guid)).Position = {42.0, 0.0, 0.0};
        StoreRenderExtension(s, "Version: 1");
    });
    REQUIRE(WaitFor([&] { return PositionX(*runner, guid) == 42.0; }));

    // Resuming a pause does not take a second snapshot
    runner->SetPaused(true);
    CHECK_FALSE(session->Resume(runner));
    REQUIRE(WaitFor([&] { return !runner->IsPaused(); }));

    session->Stop(runner, std::string("Version: 1\nContexts: {Edit: Lensed}"));
    CHECK_FALSE(session->Active());
    REQUIRE(WaitFor([&] { return runner->IsPaused() && PositionX(*runner, guid) == 1.0; }));
    REQUIRE(WaitFor([&] { return RenderExtensionText(*runner->AcquireSnapshot()).find("Lensed") != std::string::npos; }));
    runner->Stop();
}

TEST_CASE("Stop without a render extension leaves the scene's own", "[simulation][playstop][rendering]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());
    Scene scene("PlaySessionPlain");
    StoreRenderExtension(scene, "Version: 1\nContexts: {Edit: Traced}");
    auto runner = scenes.CreateSimulation(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);

    auto session = std::make_shared<PlaySession>();
    session->Resume(runner);
    REQUIRE(WaitFor([&] { return !runner->IsPaused(); }));
    session->Stop(runner, std::nullopt);
    REQUIRE(WaitFor([&] { return runner->IsPaused(); }));
    CHECK(RenderExtensionText(*runner->AcquireSnapshot()).find("Traced") != std::string::npos);
    runner->Stop();
}

TEST_CASE("Stop resets the simulation clock to the play-start time, pause keeps it", "[simulation][playstop][simclock]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());
    Scene scene("PlayClock");
    scene.SetTimeScale(2.0);
    auto runner = scenes.CreateSimulation(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);
    runner->SetSimTime(50.0);

    auto session = std::make_shared<PlaySession>();
    session->Resume(runner);
    REQUIRE(WaitFor([&] { return runner->AcquireSnapshot()->Clock().Time > 50.5; }));

    runner->SetPaused(true);
    REQUIRE(WaitFor([&] { return runner->GetStats().Paused; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto paused = runner->AcquireSnapshot()->Clock();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(runner->AcquireSnapshot()->Clock() == paused);
    CHECK(paused.Time > 50.5);

    session->Resume(runner);
    REQUIRE(WaitFor([&] { return runner->AcquireSnapshot()->Clock().Time > paused.Time + 0.5; }));

    session->Stop(runner, std::nullopt);
    REQUIRE(WaitFor([&] { return runner->IsPaused() && runner->AcquireSnapshot()->Clock().Time == 50.0; }));
    CHECK(runner->AcquireSnapshot()->Clock().Elapsed == 0.0);
    runner->Stop();
}

TEST_CASE("A paused session steps exactly n ticks and keeps the scene after Stop restores it", "[simulation][playstop][transport]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());
    Scene scene("PlayStep");
    scene.SetTimeScale(2.0);
    auto runner = scenes.CreateSimulation(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);
    runner->SetTickRate(100.0);

    auto session = std::make_shared<PlaySession>();
    CHECK(session->Resume(runner, false));
    CHECK(session->Active());
    runner->StepAndWait(3);
    CHECK(runner->IsPaused());
    CHECK(runner->AcquireSnapshot()->Clock().Time == Catch::Approx(0.06));
    runner->StepAndWait(10);
    CHECK(runner->AcquireSnapshot()->Clock().Time == Catch::Approx(0.26));

    session->Stop(runner, std::nullopt);
    REQUIRE(WaitFor([&] { return runner->AcquireSnapshot()->Clock().Time == 0.0; }));
    runner->Stop();
}

TEST_CASE("Snapshots restore the clock, entities and time scale, and are dropped on Stop", "[simulation][playstop][transport]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());
    Scene scene("PlayBookmarks");
    const auto entity = scene.CreateEntity("Mover");
    scene.Registry().emplace<TransformComponent>(entity, TransformComponent{.Position = {1.0, 0.0, 0.0}});
    const auto guid = scene.GuidOf(entity);
    auto runner = scenes.CreateSimulation(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);

    auto session = std::make_shared<PlaySession>();
    session->Capture(runner);
    CHECK(session->Bookmarks().empty());

    session->Resume(runner, false);
    runner->SetSimTime(5.0);
    session->Capture(runner);
    REQUIRE(WaitFor([&] { return session->Bookmarks().size() == 1; }));
    CHECK(session->Bookmarks()[0].SimTime == 5.0);

    runner->EnqueueSystemEdit([guid](Scene& s)
    {
        s.Registry().get<TransformComponent>(s.FindByGuid(guid)).Position = {9.0, 0.0, 0.0};
        s.SetTimeScale(50.0);
    }, false);
    runner->SetSimTime(77.0);
    REQUIRE(WaitFor([&] { return PositionX(*runner, guid) == 9.0; }));

    session->Restore(runner, 0);
    REQUIRE(WaitFor([&] { return PositionX(*runner, guid) == 1.0 && runner->AcquireSnapshot()->Clock().Time == 5.0; }));
    CHECK(runner->Metadata().TimeScale == 1.0);
    CHECK(runner->IsPaused());
    CHECK(session->Active());

    session->Restore(runner, 7);
    for (std::size_t i = 0; i < kMaxBookmarks + 3; ++i) session->Capture(runner);
    REQUIRE(WaitFor([&] { return session->Bookmarks().size() == kMaxBookmarks; }));

    session->Stop(runner, std::nullopt);
    REQUIRE(WaitFor([&] { return session->Bookmarks().empty(); }));
    runner->Stop();
}

TEST_CASE("Stop returns the time scale changed while playing", "[simulation][playstop][transport]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());
    auto runner = scenes.CreateSimulation(Scene("PlayScale"), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);

    auto session = std::make_shared<PlaySession>();
    session->Resume(runner);
    runner->EnqueueSystemEdit([](Scene& s) { s.SetTimeScale(100.0); }, false);
    REQUIRE(WaitFor([&] { return runner->Metadata().TimeScale == 100.0; }));
    session->Stop(runner, std::nullopt);
    REQUIRE(WaitFor([&] { return runner->Metadata().TimeScale == 1.0; }));
    runner->Stop();
}
