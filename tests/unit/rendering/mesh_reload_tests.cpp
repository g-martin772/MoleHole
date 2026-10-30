#include <catch2/catch_test_macros.hpp>

import MoleHole;
import GPP;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    struct MeshDir
    {
        std::filesystem::path Directory = std::filesystem::temp_directory_path() /
            ("molehole_mesh_reload_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::shared_ptr<FileSystem> Files = std::make_shared<FileSystem>();
        std::shared_ptr<Logger> Log = std::make_shared<Logger>();

        MeshDir() { std::filesystem::create_directories(Directory); }

        ~MeshDir()
        {
            std::error_code error;
            std::filesystem::remove_all(Directory, error);
        }

        [[nodiscard]] std::filesystem::path Touch(const std::string& name, const std::string& content) const
        {
            const auto path = Directory / name;
            std::ofstream(path, std::ios::binary) << content;
            std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(
                                                                                                  ++m_Bumps * 5));
            return path;
        }

        mutable int m_Bumps = 0;
    };

    struct FakeLoader
    {
        std::filesystem::path Source;
        std::atomic<int> Calls{0};
        std::atomic<bool> Fail{false};
        std::atomic<float> Extent{1.0f};

        MeshCache::Loader Make()
        {
            return [this](const std::string&)
            {
                ++Calls;
                if (Fail) throw std::runtime_error("corrupt file");
                auto scene = std::make_shared<GltfSceneData>();
                scene->Geometry.Vertices.push_back({Extent.load(), 0.0f, 0.0f});
                if (!Source.empty()) scene->Sources.push_back(Source);
                return scene;
            };
        }
    };

    bool PumpUntil(MeshCache& cache, const std::function<bool()>& done,
                   const std::chrono::milliseconds timeout = std::chrono::milliseconds(4000))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            cache.Update();
            if (done()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }
}

TEST_CASE("A changed mesh file swaps in the reloaded scene", "[mesh][reload]")
{
    MeshDir dir;
    const auto file = dir.Touch("m.glb", "one");
    FakeLoader loader;
    MeshCache cache(dir.Files, dir.Log, loader.Make());

    const auto first = cache.GetOrLoad(file.string());
    REQUIRE(first);
    CHECK(cache.BoundingRadius(file.string()) == 1.0f);
    CHECK(cache.IsCurrent(first.get()));

    loader.Extent = 3.0f;
    dir.Touch("m.glb", "two");
    REQUIRE(PumpUntil(cache, [&] { return cache.Generation() == 1; }));

    const auto second = cache.GetOrLoad(file.string());
    CHECK(second != first);
    CHECK(cache.BoundingRadius(file.string()) == 3.0f);
    CHECK(cache.IsCurrent(second.get()));
    CHECK_FALSE(cache.IsCurrent(first.get()));
}

TEST_CASE("A failed reload keeps the previous mesh", "[mesh][reload]")
{
    MeshDir dir;
    const auto file = dir.Touch("m.glb", "one");
    FakeLoader loader;
    MeshCache cache(dir.Files, dir.Log, loader.Make());
    const auto first = cache.GetOrLoad(file.string());

    loader.Fail = true;
    dir.Touch("m.glb", "broken");
    REQUIRE(PumpUntil(cache, [&] { return loader.Calls.load() >= 2 && cache.PendingReloads() == 0; }));
    cache.Update();

    CHECK(cache.Generation() == 0);
    CHECK(cache.GetOrLoad(file.string()) == first);
    CHECK(cache.IsCurrent(first.get()));

    loader.Fail = false;
    dir.Touch("m.glb", "fixed");
    REQUIRE(PumpUntil(cache, [&] { return cache.Generation() == 1; }));
    CHECK(cache.GetOrLoad(file.string()) != first);
}

TEST_CASE("A mesh that failed to load recovers when the file is fixed", "[mesh][reload]")
{
    MeshDir dir;
    const auto file = dir.Touch("m.glb", "broken");
    FakeLoader loader;
    loader.Fail = true;
    MeshCache cache(dir.Files, dir.Log, loader.Make());
    CHECK_FALSE(cache.GetOrLoad(file.string()));

    loader.Fail = false;
    dir.Touch("m.glb", "fixed");
    REQUIRE(PumpUntil(cache, [&] { return cache.Generation() == 1; }));
    CHECK(cache.GetOrLoad(file.string()));
}

TEST_CASE("A changed external resource reloads the mesh that uses it", "[mesh][reload]")
{
    MeshDir dir;
    const auto file = dir.Touch("m.gltf", "json");
    FakeLoader loader;
    loader.Source = dir.Touch("m.bin", "data");
    MeshCache cache(dir.Files, dir.Log, loader.Make());
    const auto first = cache.GetOrLoad(file.string());

    dir.Touch("m.bin", "data2");
    REQUIRE(PumpUntil(cache, [&] { return cache.Generation() == 1; }));
    CHECK(cache.GetOrLoad(file.string()) != first);
}

TEST_CASE("Unrelated file changes do not reload a mesh", "[mesh][reload]")
{
    MeshDir dir;
    const auto file = dir.Touch("m.glb", "one");
    const auto other = dir.Touch("other.glb", "one");
    FakeLoader loader;
    MeshCache cache(dir.Files, dir.Log, loader.Make());
    cache.GetOrLoad(file.string());

    dir.Touch("other.glb", "two");
    PumpUntil(cache, [] { return false; }, std::chrono::milliseconds(600));
    CHECK(loader.Calls.load() == 1);
    CHECK(cache.Generation() == 0);
}
