module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Application.SceneLoader;

import std;
import glm;
import GPP;
import :Simulation.Components;
import :Rendering.Meshes;
import :Rendering.Settings;

export namespace MoleHole
{
    class SceneLoader
    {
    public:
        struct EditorView
        {
            glm::dvec3 PositionM{0.0};
            float Yaw = -90.0f;
            float Pitch = 0.0f;
            float Fov = 60.0f;
            std::optional<double> SpeedMs;
        };

        struct Result
        {
            enum class Kind { Startup, File, Template };
            Kind LoadKind = Kind::File;
            bool Ok = false;
            std::string Path;
            std::string SceneName;
            std::string Error;
            std::string RenderExtension;
            std::optional<EditorView> Editor;
            std::size_t MeshCount = 0;
            std::vector<std::string> FailedMeshes;
            std::size_t WarningCount = 0;
            double Seconds = 0.0;
        };

        SceneLoader(std::shared_ptr<GPP::SceneManager> scenes, std::shared_ptr<MeshCache> meshes,
                    std::shared_ptr<GPP::Logger> logger)
            : m_State(std::make_shared<State>()), m_Scenes(std::move(scenes)), m_Meshes(std::move(meshes)),
              m_Logger(std::move(logger))
        {
        }

        bool Begin(std::string path, const Result::Kind kind)
        {
            bool expected = false;
            if (!m_State->InFlight.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
            auto task = m_Logger->Notifications()->BeginTask("Loading " + std::filesystem::path(path).filename().string());
            task->SetDetail("Reading scene file");
            try
            {
                GPP::ThreadPool::Instance().Submit(
                    [state = m_State, scenes = m_Scenes, meshes = m_Meshes, logger = m_Logger, task,
                     path = std::move(path), kind]() mutable
                    {
                        Run(state, scenes, meshes, logger, task, std::move(path), kind);
                    });
            }
            catch (const std::exception&)
            {
                task->Finish(false);
                m_State->InFlight.store(false, std::memory_order_release);
                m_State->InFlight.notify_all();
                return false;
            }
            return true;
        }

        [[nodiscard]] std::optional<Result> Poll()
        {
            std::scoped_lock lock(m_State->Mutex);
            return std::exchange(m_State->Outcome, std::nullopt);
        }

        void WaitIdle() const { m_State->InFlight.wait(true, std::memory_order_acquire); }

    private:
        struct State
        {
            std::mutex Mutex;
            std::optional<Result> Outcome;
            std::atomic<bool> InFlight{false};
        };

        // Optional `EditorCamera: {PositionM, Yaw, Pitch, Fov, SpeedMs}` scene key: where the editor camera starts
        static std::optional<EditorView> ReadEditorView(const GPP::Scene& scene)
        {
            const auto* node = scene.FindExtension("EditorCamera");
            if (!node || !node->IsMap() || !(*node)["PositionM"]) return std::nullopt;
            EditorView view;
            view.PositionM = (*node)["PositionM"].as<glm::dvec3>();
            view.Yaw = (*node)["Yaw"].as<float>(view.Yaw);
            view.Pitch = (*node)["Pitch"].as<float>(view.Pitch);
            view.Fov = (*node)["Fov"].as<float>(view.Fov);
            if ((*node)["SpeedMs"]) view.SpeedMs = (*node)["SpeedMs"].as<double>();
            return view;
        }

        static std::vector<std::string> MeshPaths(GPP::Scene& scene)
        {
            std::vector<std::string> paths;
            for (auto [entity, mesh] : scene.Registry().view<const GPP::MeshComponent>().each())
            {
                if (!mesh.AssetPath.empty() && std::ranges::find(paths, mesh.AssetPath) == paths.end())
                    paths.push_back(mesh.AssetPath);
            }
            return paths;
        }

        static constexpr std::size_t kMaxParallelLoads = 8;

        static void LoadMeshesParallel(const std::vector<std::string>& paths, MeshCache& meshes, GPP::ProgressTask& task)
        {
            GPP::ParallelFor(paths.size(), kMaxParallelLoads, [&](const std::size_t index)
            {
                const std::filesystem::path asset(paths[index]);
                const auto folder = asset.parent_path().filename().string();
                task.SetDetail(std::format("Mesh {}/{}: {}", index + 1, paths.size(),
                                           folder.empty() ? asset.stem().string() : folder));
                meshes.GetOrLoad(paths[index]);
                task.Advance();
            });
        }

        static std::vector<std::string> LoadSceneMeshes(GPP::Scene& scene, MeshCache& meshes, GPP::ProgressTask& task)
        {
            const auto paths = MeshPaths(scene);
            task.SetTotal(paths.size());
            if (!paths.empty()) LoadMeshesParallel(paths, meshes, task);
            std::vector<std::string> failed;
            for (const auto& path : paths)
            {
                if (!meshes.GetOrLoad(path)) failed.push_back(path);
            }
            for (auto [entity, mesh, collider] :
                 scene.Registry().view<const GPP::MeshComponent, GPP::ColliderComponent>().each())
            {
                if (collider.Shape != GPP::ColliderShape::ConvexMesh || !collider.ConvexHullPoints.empty()) continue;
                if (mesh.AssetPath.empty()) continue;
                if (const auto gltfScene = meshes.GetOrLoad(mesh.AssetPath); gltfScene && !gltfScene->Geometry.Vertices.empty())
                {
                    collider.ConvexHullPoints = gltfScene->Geometry.Vertices;
                }
            }
            return failed;
        }

        static void Run(const std::shared_ptr<State>& state, const std::shared_ptr<GPP::SceneManager>& scenes,
                        const std::shared_ptr<MeshCache>& meshes, const std::shared_ptr<GPP::Logger>& logger,
                        const std::shared_ptr<GPP::ProgressTask>& task, std::string path, const Result::Kind kind)
        {
            Result result;
            result.LoadKind = kind;
            result.Path = path;
            try
            {
                std::vector<GPP::SceneDiagnostic> diagnostics;
                auto& scene = scenes->LoadSceneFromFile(path, &diagnostics);
                result.WarningCount = diagnostics.size();
                result.MeshCount = MeshPaths(scene).size();
                result.FailedMeshes = LoadSceneMeshes(scene, *meshes, *task);
                result.Ok = true;
                result.SceneName = scene.Metadata().Name;
                result.RenderExtension = RenderExtensionText(scene);
                result.Editor = ReadEditorView(scene);
            }
            catch (const std::exception& error)
            {
                result.Error = error.what();
                logger->Warn("SceneLoader: failed to load '{}': {}", path, error.what());
            }
            result.Seconds = task->Read().ElapsedSeconds;
            task->Finish(result.Ok);
            {
                std::scoped_lock lock(state->Mutex);
                state->Outcome = std::move(result);
            }
            state->InFlight.store(false, std::memory_order_release);
            state->InFlight.notify_all();
        }

        std::shared_ptr<State> m_State;
        std::shared_ptr<GPP::SceneManager> m_Scenes;
        std::shared_ptr<MeshCache> m_Meshes;
        std::shared_ptr<GPP::Logger> m_Logger;
    };
}
