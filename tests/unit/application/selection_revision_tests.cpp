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
}

TEST_CASE("Selecting an entity does not change the content revision, editing a field does", "[application][dirty]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());

    Scene scene("Selection");
    const auto entity = scene.CreateEntity("Body");
    scene.Registry().emplace<TransformComponent>(entity);
    const auto guid = scene.GuidOf(entity);
    auto runner = scenes.CreateSimulation(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);

    UiState ui;
    SceneDirty dirty;
    dirty.Reset(runner->AcquireSnapshot().ContentRevision());

    ui.SelectedEntityGuid = guid;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ui.SelectedEntityGuid = 0;
    dirty.Observe(runner->AcquireSnapshot().ContentRevision());
    CHECK_FALSE(dirty.IsDirty());

    CommandOptions options;
    options.Undoable = true;
    runner->EnqueueCommand(SetFieldCommand{guid, "Transform", "Position", glm::vec3(3.0f, 0.0f, 0.0f)}, options);
    REQUIRE(WaitFor([&]
    {
        dirty.Observe(runner->AcquireSnapshot().ContentRevision());
        return dirty.IsDirty();
    }));

    runner->Stop();
}

TEST_CASE("System edits do not dirty the scene", "[application][dirty]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());

    Scene scene("System");
    const auto entity = scene.CreateEntity("Body");
    scene.Registry().emplace<TransformComponent>(entity);
    const auto guid = scene.GuidOf(entity);
    auto runner = scenes.CreateSimulation(std::move(scene), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);

    const auto before = runner->AcquireSnapshot().ContentRevision();
    std::atomic<bool> ran{false};
    runner->EnqueueSystemEdit([&](Scene& s)
    {
        s.Registry().get<TransformComponent>(s.FindByGuid(guid)).Position = {5.0, 0.0, 0.0};
        ran = true;
    });
    REQUIRE(WaitFor([&] { return ran.load(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(runner->AcquireSnapshot().ContentRevision() == before);

    runner->Stop();
}
