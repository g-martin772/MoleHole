export module MoleHole:Application.SceneDirty;

import std;
import GPP;

export namespace MoleHole
{
    // The open scene differs from the file when the content revision moved since the last save.
    class SceneDirty
    {
    public:
        static constexpr std::uint64_t kNeverSaved = std::numeric_limits<std::uint64_t>::max();

        void Reset(const std::uint64_t revision) noexcept
        {
            m_Saved.store(revision, std::memory_order_release);
            m_Live.store(revision, std::memory_order_release);
        }

        void Observe(const std::uint64_t revision) noexcept { m_Live.store(revision, std::memory_order_release); }

        // `exact` is false when the saved text is not the content the revision names (simulation running).
        void MarkSaved(const std::uint64_t revision, const bool exact = true) noexcept
        {
            m_Saved.store(exact ? revision : kNeverSaved, std::memory_order_release);
        }

        [[nodiscard]] std::uint64_t LiveRevision() const noexcept { return m_Live.load(std::memory_order_acquire); }

        [[nodiscard]] bool IsDirty() const noexcept
        {
            return m_Live.load(std::memory_order_acquire) != m_Saved.load(std::memory_order_acquire);
        }

    private:
        std::atomic<std::uint64_t> m_Saved{0};
        std::atomic<std::uint64_t> m_Live{0};
    };

    struct SceneAction
    {
        enum class Kind { None, NewScene, OpenFile, OpenTemplate, CloseToLauncher, Quit };
        Kind What = Kind::None;
        std::string Path;
    };

    enum class UnsavedChoice { Save, Discard, Cancel };

    // Holds an action back until the user decided what happens to unsaved changes.
    class UnsavedPrompt
    {
    public:
        // True when the action may run now; false when the prompt is open for it (or another action is waiting).
        [[nodiscard]] bool Request(SceneAction action, const bool dirty)
        {
            if (!dirty) return true;
            if (m_Pending.What != SceneAction::Kind::None && action.What != SceneAction::Kind::Quit) return false;
            m_Pending = std::move(action);
            return false;
        }

        [[nodiscard]] bool Open() const noexcept { return m_Pending.What != SceneAction::Kind::None; }
        [[nodiscard]] const SceneAction& Pending() const noexcept { return m_Pending; }

        // Save keeps the prompt open unless `saved`. Returns the action to run.
        [[nodiscard]] std::optional<SceneAction> Resolve(const UnsavedChoice choice, const bool saved = false)
        {
            switch (choice)
            {
            case UnsavedChoice::Cancel:
                m_Pending = {};
                return std::nullopt;
            case UnsavedChoice::Save:
                if (!saved) return std::nullopt;
                [[fallthrough]];
            case UnsavedChoice::Discard:
                return std::exchange(m_Pending, {});
            }
            return std::nullopt;
        }

    private:
        SceneAction m_Pending;
    };

    [[nodiscard]] inline std::expected<void, std::string> SaveSceneFile(const std::filesystem::path& path,
                                                                        const std::string_view yaml)
    {
        return GPP::WriteFileAtomic(path, yaml);
    }
}
