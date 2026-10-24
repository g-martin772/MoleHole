export module MoleHole:Simulation.PlaySession;

import std;
import GPP;
import :Rendering.Settings;
import :UI.TransportLogic;

export namespace MoleHole
{
    class PlaySession : public std::enable_shared_from_this<PlaySession>
    {
    public:
        bool Resume(const std::shared_ptr<GPP::SimulationRunner>& runner, const bool run = true)
        {
            const bool fresh = !m_Active.exchange(true);
            runner->EnqueueSystemEdit([self = shared_from_this(), runner = runner.get(), fresh, run](GPP::Scene& scene)
            {
                if (fresh)
                {
                    std::scoped_lock lock(self->m_Mutex);
                    self->m_Snapshot = scene.Clone();
                    self->m_Hidden = runner->CaptureState();
                    self->m_Revision = runner->ContentRevision();
                    self->m_TimeScale = scene.Metadata().TimeScale;
                    self->m_Bookmarks.Clear();
                }
                runner->ClearHistory();
                if (run) runner->SetPaused(false);
            });
            return fresh;
        }

        void Capture(const std::shared_ptr<GPP::SimulationRunner>& runner)
        {
            if (!m_Active) return;
            runner->EnqueueSystemEdit([self = shared_from_this(), runner = runner.get()](GPP::Scene& scene)
            {
                const double time = scene.Clock().Time;
                Bookmark bookmark{{BookmarkLabel(time), time, runner->Tick()}, scene.Clone(), runner->CaptureState(),
                                  scene.Metadata().TimeScale};
                std::scoped_lock lock(self->m_Mutex);
                self->m_Bookmarks.Push(std::move(bookmark));
            });
        }

        void Restore(const std::shared_ptr<GPP::SimulationRunner>& runner, const std::size_t index)
        {
            if (!m_Active) return;
            runner->EnqueueSystemEdit([self = shared_from_this(), runner = runner.get(), index](GPP::Scene& scene)
            {
                std::scoped_lock lock(self->m_Mutex);
                const auto* bookmark = self->m_Bookmarks.At(index);
                if (!bookmark) return;
                GPP::Scene::SyncInto(bookmark->State, scene);
                scene.SetTimeScale(bookmark->TimeScale);
                runner->RestoreState(bookmark->Hidden);
                runner->SetPaused(true);
            }, false);
        }

        [[nodiscard]] std::vector<BookmarkInfo> Bookmarks() const
        {
            std::scoped_lock lock(m_Mutex);
            std::vector<BookmarkInfo> infos;
            for (const auto& bookmark : m_Bookmarks.Items()) infos.push_back(bookmark.Info);
            return infos;
        }

        void Stop(const std::shared_ptr<GPP::SimulationRunner>& runner, std::optional<std::string> renderExtension)
        {
            m_Active = false;
            runner->EnqueueSystemEdit([self = shared_from_this(), runner = runner.get(), renderExtension = std::move(renderExtension)](GPP::Scene& scene)
            {
                std::optional<GPP::Scene> snapshot;
                std::optional<GPP::RunnerState> hidden;
                std::uint64_t revision = 0;
                double timeScale = 1.0;
                {
                    std::scoped_lock lock(self->m_Mutex);
                    snapshot = std::exchange(self->m_Snapshot, std::nullopt);
                    hidden = std::exchange(self->m_Hidden, std::nullopt);
                    revision = self->m_Revision;
                    timeScale = self->m_TimeScale;
                    self->m_Bookmarks.Clear();
                }
                if (snapshot)
                {
                    GPP::Scene::SyncInto(*snapshot, scene);
                    scene.SetTimeScale(timeScale);
                    runner->RestoreContentRevision(revision);
                }
                if (renderExtension && *renderExtension != RenderExtensionText(scene))
                {
                    StoreRenderExtension(scene, *renderExtension);
                    runner->MarkContentChanged();
                }
                if (hidden) runner->RestoreState(*hidden);
                runner->ClearHistory();
                runner->SetPaused(true);
            }, false);
        }

        [[nodiscard]] bool Active() const noexcept { return m_Active.load(); }

    private:
        struct Bookmark
        {
            BookmarkInfo Info;
            GPP::Scene State;
            GPP::RunnerState Hidden;
            double TimeScale = 1.0;
        };

        mutable std::mutex m_Mutex;
        BookmarkRing<Bookmark> m_Bookmarks;
        double m_TimeScale = 1.0;
        std::optional<GPP::Scene> m_Snapshot;
        std::optional<GPP::RunnerState> m_Hidden;
        std::uint64_t m_Revision = 0;
        std::atomic<bool> m_Active{false};
    };
}
