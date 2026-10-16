#include <catch2/catch_test_macros.hpp>

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
