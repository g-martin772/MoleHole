#include <catch2/catch_test_macros.hpp>

import MoleHole;
import GPP;
import std;

using namespace MoleHole;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace
{
    using Scheduler = AutosaveScheduler;
    using Action = Scheduler::Action;
    const Scheduler::Clock::time_point kStart{std::chrono::steady_clock::time_point{} + 1000s};

    Scheduler::Input Dirty(const Scheduler::Clock::duration at, const std::uint64_t revision = 1, const bool path = true)
    {
        return {.Now = kStart + at, .Dirty = true, .Revision = revision, .HasPath = path};
    }

    Scheduler FileScheduler(const std::chrono::seconds delay = 5s, const std::chrono::seconds recovery = 1h)
    {
        Scheduler scheduler;
        auto config = scheduler.Settings();
        config.FileEnabled = true;
        config.Delay = delay;
        config.RecoveryInterval = recovery;
        scheduler.Configure(config);
        return scheduler;
    }

    struct TempDir
    {
        fs::path Root = fs::temp_directory_path() /
                        std::format("molehole_autosave_{}", std::chrono::steady_clock::now().time_since_epoch().count());
        TempDir() { fs::create_directories(Root); }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(Root, ec);
        }
    };

    std::string Slurp(const fs::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
}

TEST_CASE("Clean scene never schedules anything", "[autosave]")
{
    auto scheduler = FileScheduler();
    CHECK(scheduler.Update({.Now = kStart, .Dirty = false}) == Action::None);
    CHECK(scheduler.Update({.Now = kStart + 1h, .Dirty = false}) == Action::None);
}

TEST_CASE("File autosave fires after the delay and every change resets it", "[autosave]")
{
    auto scheduler = FileScheduler(5s);
    CHECK(scheduler.Update(Dirty(0s, 1)) == Action::None);
    CHECK(scheduler.Update(Dirty(4s, 1)) == Action::None);
    CHECK(scheduler.Update(Dirty(4s + 900ms, 2)) == Action::None);
    CHECK(scheduler.Update(Dirty(9s, 2)) == Action::None);
    CHECK(scheduler.Update(Dirty(9s + 900ms, 2)) == Action::SaveFile);
}

TEST_CASE("Autosave off keeps the recovery copy only", "[autosave]")
{
    Scheduler scheduler;
    CHECK(scheduler.Update(Dirty(0s)) == Action::None);
    CHECK(scheduler.Update(Dirty(29s)) == Action::None);
    CHECK(scheduler.Update(Dirty(30s)) == Action::WriteRecovery);
    scheduler.Succeeded(Action::WriteRecovery, kStart + 30s);
    CHECK(scheduler.Update(Dirty(31s)) == Action::None);
    CHECK(scheduler.Update(Dirty(70s)) == Action::None);
}

TEST_CASE("Recovery copy repeats for newer revisions every interval", "[autosave]")
{
    Scheduler scheduler;
    CHECK(scheduler.Update(Dirty(0s, 1)) == Action::None);
    CHECK(scheduler.Update(Dirty(30s, 1)) == Action::WriteRecovery);
    scheduler.Succeeded(Action::WriteRecovery, kStart + 30s);
    CHECK(scheduler.Update(Dirty(40s, 2)) == Action::None);
    CHECK(scheduler.Update(Dirty(59s, 2)) == Action::None);
    CHECK(scheduler.Update(Dirty(60s, 2)) == Action::WriteRecovery);
}

TEST_CASE("Scenes without a path get no file autosave", "[autosave]")
{
    auto scheduler = FileScheduler(1s, 60s);
    CHECK(scheduler.Update(Dirty(0s, 1, false)) == Action::None);
    CHECK(scheduler.Update(Dirty(10s, 1, false)) == Action::None);
    CHECK(scheduler.Update(Dirty(59s, 1, false)) == Action::None);
    CHECK(scheduler.Update(Dirty(60s, 1, false)) == Action::WriteRecovery);
}

TEST_CASE("Nothing is written while playing or while the modal is open", "[autosave]")
{
    auto scheduler = FileScheduler(1s);
    auto playing = Dirty(0s, 1);
    playing.Playing = true;
    CHECK(scheduler.Update(playing) == Action::None);
    playing.Now = kStart + 60s;
    playing.Revision = 2;
    CHECK(scheduler.Update(playing) == Action::None);

    auto paused = Dirty(60s + 500ms, 2);
    CHECK(scheduler.Update(paused) == Action::None);
    paused = Dirty(62s, 2);
    paused.ModalOpen = true;
    CHECK(scheduler.Update(paused) == Action::None);
    CHECK(scheduler.Update(Dirty(62s, 2)) == Action::SaveFile);
}

TEST_CASE("Failures retry with exponential backoff and success resets it", "[autosave]")
{
    auto scheduler = FileScheduler(1s);
    CHECK(scheduler.Update(Dirty(0s)) == Action::None);
    CHECK(scheduler.Update(Dirty(1s)) == Action::SaveFile);
    CHECK(scheduler.Failed(Action::SaveFile, kStart + 1s) == 1);
    CHECK(scheduler.Update(Dirty(2s)) == Action::None);
    CHECK(scheduler.Update(Dirty(3s)) == Action::SaveFile);
    CHECK(scheduler.Failed(Action::SaveFile, kStart + 3s) == 2);
    CHECK(scheduler.Update(Dirty(6s)) == Action::None);
    CHECK(scheduler.Update(Dirty(7s)) == Action::SaveFile);
    CHECK(scheduler.Failed(Action::SaveFile, kStart + 7s) == 3);
    CHECK(scheduler.Backoff(3) == 8s);
    CHECK(scheduler.Backoff(20) == 60s);

    scheduler.Succeeded(Action::SaveFile, kStart + 20s);
    CHECK(scheduler.Update(Dirty(21s, 2)) == Action::None);
    CHECK(scheduler.Update(Dirty(22s, 2)) == Action::SaveFile);
    CHECK(scheduler.Failed(Action::SaveFile, kStart + 22s) == 1);
}

TEST_CASE("A failed file save does not block the recovery copy", "[autosave]")
{
    auto scheduler = FileScheduler(1s, 2s);
    CHECK(scheduler.Update(Dirty(0s)) == Action::None);
    CHECK(scheduler.Update(Dirty(1s)) == Action::SaveFile);
    scheduler.Failed(Action::SaveFile, kStart + 1s);
    CHECK(scheduler.Update(Dirty(2s)) == Action::WriteRecovery);
}

TEST_CASE("Recovery header round trips", "[autosave]")
{
    const auto text = RecoveryHeaderText({4242, "/s/a b.yaml", "Scene A"}) + "Name: x\n";
    const auto header = ParseRecoveryHeader(text);
    REQUIRE(header);
    CHECK(header->ProcessId == 4242);
    CHECK(header->ScenePath == "/s/a b.yaml");
    CHECK(header->SceneName == "Scene A");
    CHECK(StripRecoveryHeader(text) == "Name: x\n");

    const auto pathless = ParseRecoveryHeader(RecoveryHeaderText({7, "", "Untitled-1"}));
    REQUIRE(pathless);
    CHECK(pathless->ScenePath.empty());
    CHECK(pathless->SceneName == "Untitled-1");

    CHECK_FALSE(ParseRecoveryHeader("Name: x\n"));
    CHECK_FALSE(ParseRecoveryHeader("# molehole-recovery 1\n# path: /a\n"));
}

TEST_CASE("Recovery key depends on path, or name when there is no path", "[autosave]")
{
    CHECK(RecoveryKey("/a.yaml", "X") == RecoveryKey("/a.yaml", "Y"));
    CHECK(RecoveryKey("/a.yaml", "X") != RecoveryKey("/b.yaml", "X"));
    CHECK(RecoveryKey("", "Untitled-1") != RecoveryKey("", "Untitled-2"));
    CHECK(RecoveryKey("/a.yaml", "X").size() == 16);
}

TEST_CASE("Recovery newer-than check", "[autosave]")
{
    const auto now = fs::file_time_type::clock::now();
    CHECK(IsRecoveryNewer(now, std::nullopt));
    CHECK(IsRecoveryNewer(now, now - 1min));
    CHECK_FALSE(IsRecoveryNewer(now - 1min, now));
    CHECK_FALSE(IsRecoveryNewer(now, now));
}

TEST_CASE("Recovery store lifecycle: write, replace, clear", "[autosave]")
{
    TempDir dir;
    RecoveryStore store(dir.Root / "autosave", 100, [](long) { return false; });

    REQUIRE(store.Write("", "Untitled-1", "Name: a\n"));
    const auto first = store.FileFor("", "Untitled-1");
    CHECK(fs::exists(first));
    CHECK(Slurp(first).ends_with("Name: a\n"));
    CHECK(store.HasWritten());

    REQUIRE(store.Write("", "Untitled-1", "Name: b\n"));
    CHECK(Slurp(first).ends_with("Name: b\n"));

    REQUIRE(store.Write((dir.Root / "s.yaml").string(), "S", "Name: s\n"));
    CHECK_FALSE(fs::exists(first));
    CHECK(fs::exists(store.FileFor((dir.Root / "s.yaml").string(), "S")));

    store.Clear();
    CHECK_FALSE(store.HasWritten());
    CHECK(fs::is_empty(dir.Root / "autosave"));
}

TEST_CASE("Recovery scan lists dead instances' newer copies and prunes obsolete ones", "[autosave]")
{
    TempDir dir;
    const auto autosave = dir.Root / "autosave";
    const auto scene = dir.Root / "scene.yaml";
    const auto live = [](const long pid) { return pid == 300; };

    RecoveryStore crashed(autosave, 100, live);
    REQUIRE(crashed.Write(scene.string(), "Scene", "Name: unsaved\n"));
    RecoveryStore pathless(autosave, 200, live);
    REQUIRE(pathless.Write("", "Untitled-1", "Name: new\n"));
    RecoveryStore running(autosave, 300, live);
    REQUIRE(running.Write("", "Untitled-2", "Name: other window\n"));

    const RecoveryStore reader(autosave, 400, live);
    auto found = reader.Scan();
    REQUIRE(found.size() == 2);
    CHECK(std::ranges::any_of(found, [](const RecoveryEntry& e) { return e.SceneName == "Scene" && e.ProcessId == 100; }));
    CHECK(std::ranges::any_of(found, [](const RecoveryEntry& e) { return e.SceneName == "Untitled-1" && e.ScenePath.empty(); }));

    std::ofstream(scene) << "Name: saved\n";
    fs::last_write_time(crashed.FileFor(scene.string(), "Scene"), fs::file_time_type::clock::now() - 1h);
    found = reader.Scan();
    REQUIRE(found.size() == 1);
    CHECK(found.front().SceneName == "Untitled-1");
    CHECK_FALSE(fs::exists(crashed.FileFor(scene.string(), "Scene")));
}

TEST_CASE("Adopting a crashed copy makes it this instance's own", "[autosave]")
{
    TempDir dir;
    const auto autosave = dir.Root / "autosave";
    RecoveryStore crashed(autosave, 100, [](long) { return false; });
    REQUIRE(crashed.Write("", "Untitled-1", "Name: keep\n"));
    const auto oldFile = crashed.FileFor("", "Untitled-1");

    RecoveryStore next(autosave, 500, [](long) { return false; });
    REQUIRE(next.Adopt(oldFile));
    CHECK_FALSE(fs::exists(oldFile));
    const auto own = next.FileFor("", "Untitled-1");
    REQUIRE(fs::exists(own));
    CHECK(Slurp(own).ends_with("Name: keep\n"));
    CHECK(ParseRecoveryHeader(Slurp(own))->ProcessId == 500);
    CHECK(next.HasWritten());
    next.Clear();
    CHECK_FALSE(fs::exists(own));
}

TEST_CASE("Recovery write fails cleanly when unconfigured or unwritable", "[autosave]")
{
    RecoveryStore none;
    CHECK_FALSE(none.Write("", "x", "y"));

    TempDir dir;
    std::ofstream(dir.Root / "blocker") << "file";
    RecoveryStore blocked(dir.Root / "blocker" / "sub", 1, [](long) { return false; });
    CHECK_FALSE(blocked.Write("", "x", "y"));
}

TEST_CASE("Startup parses --recover as an explicit editor start", "[autosave]")
{
    const std::vector<std::string> args{"--recover", "/state/autosave/x-1.yaml", "--skip-intro"};
    const auto parsed = ParseStartupArgs(args);
    REQUIRE(parsed.Recover);
    CHECK(*parsed.Recover == "/state/autosave/x-1.yaml");
    CHECK_FALSE(parsed.Scene);
    CHECK(SelectStartupMode(parsed, StartupPrefs{}) == StartupMode::Editor);
}
