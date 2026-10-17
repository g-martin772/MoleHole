#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr std::array kContexts{
        RenderContext::Edit, RenderContext::Play, RenderContext::Reference, RenderContext::Export};

    RenderSettings BaselineFor(const RenderProfileSet& set, const RenderProfile& profile)
    {
        if (profile.BuiltIn) return profile.Settings;
        if (const auto* base = set.Find(profile.Base)) return base->Settings;
        return RenderSettings{};
    }

    struct RenderWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, UiState, RenderSettingsService>;

        RenderWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<UiState> uiState,
                          std::shared_ptr<RenderSettingsService> settings)
            : HotReloadableLayer(logger), m_UiState(std::move(uiState)), m_Settings(std::move(settings))
        {
        }

        void OnAttach() override { EnsureIconFont(*m_UiState); }

        void OnUiRender() override
        {
            if (!m_UiState->ShowRenderWindow) return;
            if (!ImGui::Begin("Render", &m_UiState->ShowRenderWindow))
            {
                ImGui::End();
                return;
            }

            if (m_Follow) m_EditContext = m_Settings->ActiveContext();
            DrawContexts();
            DrawProfileBar();
            DrawActions();
            DrawSettings();
            ImGui::End();
        }

    private:
        void DrawContexts()
        {
            SectionHeader("CONTEXTS");
            const auto& set = m_Settings->Profiles();
            ImGui::Checkbox("Edit what the viewport shows", &m_Follow);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("On: the profile of the active context is the one you edit.\n"
                                  "Off: pick a context below, e.g. Export, to prepare it while looking at another.");
            }
            if (m_Settings->ProfileOverridden())
            {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "--profile is set: every context uses '%s'",
                                   m_Settings->ProfileFor(RenderContext::Edit).Name.c_str());
            }

            for (const auto context : kContexts)
            {
                ImGui::PushID(static_cast<int>(context));
                const bool selected = m_EditContext == context;
                if (ImGui::RadioButton("##edit", selected))
                {
                    m_EditContext = context;
                    m_Follow = false;
                }
                ImGui::SameLine();
                const bool active = m_Settings->ActiveContext() == context;
                ImGui::TextColored(active ? ImVec4(0.95f, 0.6f, 0.25f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                                   "%-20s", std::string(RenderContextLabel(context)).c_str());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                const auto& current = set.ProfileFor(context);
                if (ImGui::BeginCombo("##profile", current.Name.c_str()))
                {
                    for (const auto& profile : set.Profiles())
                    {
                        const std::string label = profile.BuiltIn ? profile.Name + "  (built-in)" : profile.Name;
                        if (ImGui::Selectable(label.c_str(), profile.Name == current.Name))
                            m_Settings->Assign(context, profile.Name);
                        if (ImGui::IsItemHovered() && !profile.Description.empty()) ImGui::SetTooltip("%s", profile.Description.c_str());
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopID();
            }
        }

        void DrawProfileBar()
        {
            const auto& set = m_Settings->Profiles();
            const auto& profile = set.ProfileFor(m_EditContext);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Editing %s", std::string(RenderContextLabel(m_EditContext)).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(profile.BuiltIn ? "[%s, built-in]" : "[%s]", profile.Name.c_str());
            if (!profile.Description.empty()) ImGui::TextWrapped("%s", profile.Description.c_str());
            if (profile.BuiltIn)
            {
                ImGui::TextDisabled("Changing a setting forks this profile into an editable copy.");
            }
            else
            {
                if (m_RenamedFor != profile.Name)
                {
                    m_RenamedFor = profile.Name;
                    m_NameBuffer = profile.Name;
                }
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
                TextValue("##name", m_NameBuffer);
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    m_Settings->Rename(profile.Name, m_NameBuffer);
                    m_RenamedFor.clear();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("name");
            }

            if (ImGui::Button("Duplicate")) m_Settings->Duplicate(m_EditContext);
            if (!profile.BuiltIn)
            {
                ImGui::SameLine();
                if (ImGui::Button("Delete")) m_Settings->Remove(profile.Name);
            }
            ImGui::SameLine();
            if (ImGui::Button("Use as app default")) m_Settings->UseCurrentAsDefaults();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Scenes without their own render settings start from the profiles and contexts shown here.");
            }
        }

        void DrawActions()
        {
            ImGui::Spacing();
            const bool reference = m_UiState->ReferenceView;
            if (reference) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(reference ? "Back to the normal viewport" : "Render reference"))
                m_UiState->ReferenceView = !reference;
            if (reference) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Draws the viewport with the Reference context's profile, to judge the final look\n"
                                  "without exporting. Expect a slow frame rate.");
            }
            ImGui::SameLine();
            ImGui::Checkbox("Stats overlay", &m_UiState->ShowRenderStats);
            for (const auto& note : m_Settings->EffectiveNotes())
            {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "%s", note.c_str());
            }
        }

        void DrawSettings()
        {
            const auto& registry = RenderSettingRegistry();
            const auto& set = m_Settings->Profiles();
            const auto& profile = set.ProfileFor(m_EditContext);
            const RenderSettings baseline = BaselineFor(set, profile);
            RenderSettings edited = profile.Settings;
            const auto& caps = m_Settings->Capabilities();
            ImFont* icons = m_UiState->IconFont;

            SectionHeader("SETTINGS");
            SearchBox(icons, "##settingsearch", m_Search, "Search settings...");
            ImGui::Spacing();

            bool changed = false;
            for (const auto& group : registry.Groups())
            {
                std::vector<const SettingDesc<RenderSettings>*> rows;
                for (const auto* setting : registry.InGroup(group))
                {
                    if (!registry.IsVisible(edited, *setting)) continue;
                    if (!m_Search.empty() && !FuzzyScore(m_Search, setting->Meta.Label + " " + setting->Key + " " + group)) continue;
                    rows.push_back(setting);
                }
                if (rows.empty()) continue;
                if (!BeginSection(icons, group.c_str(), group != "Debug" && group != "Overlays")) continue;
                for (const auto* setting : rows)
                {
                    ImGui::PushID(setting->Key.c_str());
                    changed |= DrawRenderSetting(icons, registry, *setting, edited, baseline, caps);
                    ImGui::PopID();
                    if (setting->Key == "debug.view") DrawDebugViewLegend(edited.Debug);
                }
                if (group == "Overlays") DrawOverlayLegends(edited);
                EndSection();
            }
            if (changed) m_Settings->Edit(m_EditContext, edited);
        }

        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<RenderSettingsService> m_Settings;
        RenderContext m_EditContext = RenderContext::Edit;
        bool m_Follow = true;
        std::string m_Search;
        std::string m_NameBuffer;
        std::string m_RenamedFor;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(RenderWindowLayer)
