export module MoleHole:Simulation.PlaySession;

import std;
import GPP;
import :Rendering.Settings;

export namespace MoleHole
{
    class PlaySession : public std::enable_shared_from_this<PlaySession>
    {
    public:
        bool Resume(const std::shared_ptr<GPP::SimulationRunner>& runner)
        {
            const bool fresh = !m_Active.exchange(true);
            runner->EnqueueTrackedEdit([self = shared_from_this(), runner = runner.get(), fresh](GPP::Scene& scene)
            {
                if (fresh)
                {
                    std::scoped_lock lock(self->m_Mutex);
                    self->m_Snapshot = scene.Clone();
                    self->m_Hidden = runner->CaptureState();
                }
                runner->ClearHistory();
                runner->SetPaused(false);
            });
            return fresh;
        }

        void Stop(const std::shared_ptr<GPP::SimulationRunner>& runner, std::optional<std::string> renderExtension)
        {
            m_Active = false;
            runner->EnqueueEdit([self = shared_from_this(), runner = runner.get(), renderExtension = std::move(renderExtension)](GPP::Scene& scene)
            {
                std::optional<GPP::Scene> snapshot;
                std::optional<GPP::RunnerState> hidden;
                {
                    std::scoped_lock lock(self->m_Mutex);
                    snapshot = std::exchange(self->m_Snapshot, std::nullopt);
                    hidden = std::exchange(self->m_Hidden, std::nullopt);
                }
                if (snapshot) GPP::Scene::SyncInto(*snapshot, scene);
                if (renderExtension) StoreRenderExtension(scene, *renderExtension);
                if (hidden) runner->RestoreState(*hidden);
                runner->ClearHistory();
                runner->SetPaused(true);
            });
        }

        [[nodiscard]] bool Active() const noexcept { return m_Active.load(); }

    private:
        std::mutex m_Mutex;
        std::optional<GPP::Scene> m_Snapshot;
        std::optional<GPP::RunnerState> m_Hidden;
        std::atomic<bool> m_Active{false};
    };
}
