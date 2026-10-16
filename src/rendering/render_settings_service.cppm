module;
#include <yaml-cpp/yaml.h>
export module MoleHole:Rendering.SettingsService;

import std;
import GPP;
import :Rendering.Settings;

export namespace MoleHole
{
    // The profiles, which one each context uses, and the settings that result for the context being drawn.
    // UI thread only, except PostOverride which any thread may call.
    class RenderSettingsService final : public GPP::IService
    {
    public:
        RenderSettingsService() { Rebuild(); }

        void SetCapabilities(GPP::GpuCapabilities capabilities)
        {
            m_Capabilities = std::move(capabilities);
            Rebuild();
        }
        [[nodiscard]] const GPP::GpuCapabilities& Capabilities() const noexcept { return m_Capabilities; }

        void SetActiveContext(const RenderContext context)
        {
            if (context == m_Active) return;
            m_Active = context;
            Rebuild();
        }
        [[nodiscard]] RenderContext ActiveContext() const noexcept { return m_Active; }

        // Settings of the active context after overrides and device fallbacks
        [[nodiscard]] const RenderSettings& Effective() const noexcept { return m_Effective; }
        // Any thread: the effective settings as of the last rebuild
        [[nodiscard]] std::shared_ptr<const RenderSettings> Snapshot() const { return m_Snapshot.load(); }
        // Any thread: one effective setting as text, nullopt for an unknown key
        [[nodiscard]] std::optional<std::string> ReadSetting(const std::string_view key) const
        {
            const auto* setting = RenderSettingRegistry().Find(key);
            const auto snapshot = Snapshot();
            if (!setting || !snapshot) return std::nullopt;
            return RenderSettingRegistry().ToText(*snapshot, *setting);
        }
        // Any thread: whether `key=value` would be accepted; empty when it would, the reason otherwise
        [[nodiscard]] static std::string ValidateSetting(const std::string_view key, const std::string_view value)
        {
            RenderSettings scratch;
            const auto result = RenderSettingRegistry().SetFromText(scratch, key, value);
            return result ? std::string() : result.error();
        }
        // What the device fallback changed, for display
        [[nodiscard]] const std::vector<std::string>& EffectiveNotes() const noexcept { return m_Notes; }

        [[nodiscard]] const RenderProfileSet& Profiles() const noexcept { return m_Set; }
        // The profile a context uses, honouring --profile
        [[nodiscard]] const RenderProfile& ProfileFor(const RenderContext context) const
        {
            if (!m_ProfileOverride.empty())
            {
                if (const auto* profile = m_Set.Find(m_ProfileOverride)) return *profile;
            }
            return m_Set.ProfileFor(context);
        }
        [[nodiscard]] bool ProfileOverridden() const noexcept { return !m_ProfileOverride.empty() && m_Set.Find(m_ProfileOverride); }

        // Edits go to the context's profile; a built-in is forked first
        void Edit(const RenderContext context, const RenderSettings& settings)
        {
            if (SettingsEqual(m_Set.SettingsFor(context), settings)) return;
            m_Set.Replace(context, settings);
            Touch();
        }
        void Assign(const RenderContext context, const std::string_view name)
        {
            if (m_Set.ProfileName(context) == name || !m_Set.Assign(context, name)) return;
            Touch();
        }
        void Duplicate(const RenderContext context)
        {
            m_Set.Duplicate(context);
            Touch();
        }
        void Remove(const std::string_view name)
        {
            if (m_Set.Remove(name)) Touch();
        }
        void Rename(const std::string_view name, const std::string_view newName)
        {
            if (m_Set.Rename(name, newName)) Touch();
        }

        // ---- persistence ----
        // Scene extension as YAML text; empty (no extension) falls back to the app defaults
        void LoadScene(const std::string& yaml)
        {
            std::vector<std::string> warnings;
            m_Set = m_Defaults;
            if (!yaml.empty())
            {
                try { m_Set = RenderProfileSet::FromYaml(YAML::Load(yaml), &warnings); }
                catch (const std::exception& error) { warnings.push_back(std::string("render settings unreadable: ") + error.what()); }
            }
            m_HasScene = true;
            m_Adopted = !yaml.empty();
            m_Warnings = std::move(warnings);
            m_Revision = 0;
            m_WrittenRevision = 0;
            m_RuntimeOverrides.clear();
            Rebuild();
        }
        // True when an edit has not been mirrored into the scene yet
        [[nodiscard]] bool SceneWritePending() const noexcept { return m_Revision != m_WrittenRevision; }
        // True once the scene carries (or must carry) its own `Render:` extension: it had one, or was edited
        [[nodiscard]] bool PersistsToScene() const noexcept { return m_Adopted; }
        [[nodiscard]] std::optional<std::string> TakeSceneWrite()
        {
            if (m_Revision == m_WrittenRevision) return std::nullopt;
            m_WrittenRevision = m_Revision;
            return SceneYaml();
        }
        [[nodiscard]] std::string SceneYaml() const { return YAML::Dump(m_Set.ToYaml()); }
        [[nodiscard]] const std::vector<std::string>& Warnings() const noexcept { return m_Warnings; }

        void SetDefaults(RenderProfileSet defaults)
        {
            m_Defaults = std::move(defaults);
            if (m_HasScene) return;
            m_Set = m_Defaults;
            Rebuild();
        }
        [[nodiscard]] const RenderProfileSet& Defaults() const noexcept { return m_Defaults; }
        void UseCurrentAsDefaults() { m_Defaults = m_Set; }

        // ---- per-run overrides ----
        // --profile: wins over every context's assignment
        void SetProfileOverride(std::string name)
        {
            m_ProfileOverride = std::move(name);
            Rebuild();
        }
        // key=value pairs applied on top of the active profile while the Export context is active
        void SetExportOverrides(std::vector<std::pair<std::string, std::string>> overrides)
        {
            m_ExportOverrides = std::move(overrides);
            Rebuild();
        }
        [[nodiscard]] const std::vector<std::pair<std::string, std::string>>& ExportOverrides() const noexcept
        {
            return m_ExportOverrides;
        }

        // Any thread: scripts adjusting a setting for the running simulation. Cleared on scene load and Stop.
        void PostOverride(std::string key, std::string value)
        {
            std::scoped_lock lock(m_PostMutex);
            m_Posted.emplace_back(std::move(key), std::move(value));
        }
        // Applies posted overrides; returns false for none. Errors are reported through `errors`.
        bool DrainPosted(std::vector<std::string>* errors = nullptr)
        {
            std::vector<std::pair<std::string, std::string>> posted;
            {
                std::scoped_lock lock(m_PostMutex);
                posted.swap(m_Posted);
            }
            if (posted.empty()) return false;
            for (auto& [key, value] : posted)
            {
                const auto check = RenderSettingRegistry().SetFromText(m_Scratch, key, value);
                if (!check)
                {
                    if (errors) errors->push_back(check.error());
                    continue;
                }
                std::erase_if(m_RuntimeOverrides, [&](const auto& entry) { return entry.first == key; });
                m_RuntimeOverrides.emplace_back(std::move(key), std::move(value));
            }
            Rebuild();
            return true;
        }
        void ClearRuntimeOverrides()
        {
            if (m_RuntimeOverrides.empty()) return;
            m_RuntimeOverrides.clear();
            Rebuild();
        }

    private:
        static bool SettingsEqual(const RenderSettings& a, const RenderSettings& b)
        {
            return RenderSettingRegistry().Diff(a, b).empty();
        }

        void Touch()
        {
            ++m_Revision;
            m_Adopted = true;
            Rebuild();
        }

        void Rebuild()
        {
            auto settings = ProfileFor(m_Active).Settings;
            const auto apply = [&](const std::vector<std::pair<std::string, std::string>>& overrides)
            {
                for (const auto& [key, value] : overrides) (void)RenderSettingRegistry().SetFromText(settings, key, value);
            };
            apply(m_RuntimeOverrides);
            if (m_Active == RenderContext::Export) apply(m_ExportOverrides);
            m_Notes = SanitizeForDevice(settings, m_Capabilities);
            m_Effective = settings;
            m_Snapshot.store(std::make_shared<const RenderSettings>(settings));
        }

        GPP::GpuCapabilities m_Capabilities;
        RenderProfileSet m_Set;
        RenderProfileSet m_Defaults;
        RenderContext m_Active = RenderContext::Edit;
        std::string m_ProfileOverride;
        std::vector<std::pair<std::string, std::string>> m_ExportOverrides;
        std::vector<std::pair<std::string, std::string>> m_RuntimeOverrides;
        RenderSettings m_Effective;
        std::atomic<std::shared_ptr<const RenderSettings>> m_Snapshot;
        RenderSettings m_Scratch;
        std::vector<std::string> m_Notes;
        std::vector<std::string> m_Warnings;
        bool m_HasScene = false;
        bool m_Adopted = false;
        std::uint64_t m_Revision = 0;
        std::uint64_t m_WrittenRevision = 0;

        std::mutex m_PostMutex;
        std::vector<std::pair<std::string, std::string>> m_Posted;
    };
}
