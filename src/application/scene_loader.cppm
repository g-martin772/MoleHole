export module MoleHole:Application.SceneLoader;

import std;
import GPP;
import :Simulation.Components;
import :Rendering.Meshes;
import :Rendering.Settings;

export namespace MoleHole
{
    class SceneLoader
    {
    public:
        struct Result
        {
            enum class Kind { Startup, File, Template };
            Kind LoadKind = Kind::File;
            bool Ok = false;
            std::string Path;
            std::string SceneName;
            std::string Error;
            std::string RenderExtension;
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
            try
            {
                GPP::ThreadPool::Instance().Submit(
                    [state = m_State, scenes = m_Scenes, meshes = m_Meshes, logger = m_Logger,
                     path = std::move(path), kind]() mutable
                    {
                        Run(state, scenes, meshes, logger, std::move(path), kind);
                    });
            }
            catch (const std::exception&)
            {
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

        static void PopulateConvexHulls(GPP::Scene& scene, MeshCache& meshes)
        {
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
        }

        static void Run(const std::shared_ptr<State>& state, const std::shared_ptr<GPP::SceneManager>& scenes,
                        const std::shared_ptr<MeshCache>& meshes, const std::shared_ptr<GPP::Logger>& logger,
                        std::string path, const Result::Kind kind)
        {
            Result result;
            result.LoadKind = kind;
            result.Path = path;
            try
            {
                auto& scene = scenes->LoadSceneFromFile(path);
                PopulateConvexHulls(scene, *meshes);
                result.Ok = true;
                result.SceneName = scene.Metadata().Name;
                result.RenderExtension = RenderExtensionText(scene);
            }
            catch (const std::exception& error)
            {
                result.Error = error.what();
                logger->Warn("SceneLoader: failed to load '{}': {}", path, error.what());
            }
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
