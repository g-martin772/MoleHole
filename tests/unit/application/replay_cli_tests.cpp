#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;

TEST_CASE("A recorded dev scene verifies headlessly and fails when perturbed", "[replay][cli]")
{
    std::ifstream stream(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "templates" / "dev.yaml");
    const std::string yaml{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    Scene scene("Dev");
    scene.DeserializeFromYaml(yaml);
    std::uint64_t target = 0;
    for (const auto& [guid, entity] : scene.Guids().GuidToEntity)
    {
        if (scene.Registry().all_of<TransformComponent>(entity)) target = guid;
    }
    REQUIRE(target != 0);

    SimulationRecording recording;
    {
        SimulationRunner runner(std::move(scene), MakeHeadlessModules());
        runner.BeginRecording(30);
        runner.StepAndWait(40);
        runner.EnqueueCommand(SetFieldCommand{target, "Transform", "Scale", glm::vec3(1.5f)});
        runner.StepAndWait(50);
        recording = runner.EndRecording();
    }
    REQUIRE(recording.Commands.size() == 1);

    const auto dir = std::filesystem::temp_directory_path();
    const auto good = (dir / "molehole_replay_good.yaml").string();
    const auto bad = (dir / "molehole_replay_bad.yaml").string();
    REQUIRE(SaveRecording(recording, good));
    std::get<SetFieldCommand>(recording.Commands[0].Applied).Value = glm::vec3(2.5f);
    REQUIRE(SaveRecording(recording, bad));

    CHECK(RunReplay({good, true}) == 0);
    CHECK(RunReplay({good, true}) == 0);
    CHECK(RunReplay({bad, true}) == 1);
    CHECK(RunReplay({(dir / "molehole_replay_missing.yaml").string(), true}) == 2);
    std::filesystem::remove(good);
    std::filesystem::remove(bad);
}
