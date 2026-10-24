import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr const char* kPopup = "Settings";
    constexpr const char* kRebindPopup = "Rebind Shortcut";

    struct SettingsWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, FileDialog, UiState, UiPreferences, FontAssetCatalog, FontOptions, AppStateService,
                                        IFileSystem, RenderSettingsService, AppPaths>;

        SettingsWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<FileDialog> fileDialog,
                            std::shared_ptr<UiState> uiState, std::shared_ptr<UiPreferences> uiPreferences,
                            std::shared_ptr<FontAssetCatalog> fontAssets, std::shared_ptr<FontOptions> fontOptions,
                            std::shared_ptr<AppStateService> appState, std::shared_ptr<IFileSystem> fileSystem,
                            std::shared_ptr<RenderSettingsService> renderSettings, std::shared_ptr<AppPaths> appPaths)
            : HotReloadableLayer(logger), m_FileDialog(std::move(fileDialog)), m_UiState(std::move(uiState)),
              m_UiPreferences(std::move(uiPreferences)), m_FontAssets(std::move(fontAssets)),
              m_FontOptions(std::move(fontOptions)), m_AppState(std::move(appState)), m_FileSystem(std::move(fileSystem)),
              m_Render(std::move(renderSettings)), m_AppPaths(std::move(appPaths))
        {
        }

        void OnUiRender() override
        {
            if (m_UiState->ShowSettingsWindow && !ImGui::IsPopupOpen(kPopup)) ImGui::OpenPopup(kPopup);

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(560, 660), ImGuiCond_Appearing);
            if (!ImGui::BeginPopupModal(kPopup, &m_UiState->ShowSettingsWindow)) return;

            const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
            if (ImGui::BeginChild("##settingsBody", ImVec2(0.0f, -footer)))
            {
                DrawAbout();
                DrawInterface();
                DrawKeyboard();
                DrawEditing();
                DrawRendering();
                DrawUnits();
                DrawExport();
            }
            ImGui::EndChild();

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0)))
            {
                m_UiState->ShowSettingsWindow = false;
                ImGui::CloseCurrentPopup();
            }
            DrawRebindPopup();
            ImGui::EndPopup();
        }

    private:
        void DrawAbout()
        {
            SectionHeader("MOLEHOLE");
            ImGui::TextWrapped("A relativistic black hole visualizer -- gravitational lensing, accretion disks, "
                               "and spacetime curvature rendered in real time.");
        }

        void DrawInterface()
        {
            if (SectionHeaderWithReset("INTERFACE", "interface"))
            {
                m_UiState->IntroEnabled = true;
                m_UiPreferences->SetUiScale(m_FontOptions->UiScale);
                m_UiPreferences->SetFont(m_FontOptions->DefaultFont, m_FontOptions->DefaultFontSize);
            }
            ImGui::Checkbox("Play intro animation on startup", &m_UiState->IntroEnabled);
            float scale = m_UiPreferences->GetUiScale();
            if (ImGui::SliderFloat("UI Scale", &scale, 0.5f, 2.0f, "%.2f")) m_UiPreferences->SetUiScale(scale);

            const auto currentFont = m_UiPreferences->GetFontName();
            const auto currentSize = m_UiPreferences->GetFontSize();
            if (ImGui::BeginCombo("Font", currentFont.c_str()))
            {
                for (const auto& font : m_FontAssets->GetAvailableFonts())
                {
                    const bool selected = font.Name == currentFont;
                    if (ImGui::Selectable(font.Name.c_str(), selected)) m_UiPreferences->SetFont(font.Name, currentSize);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            float fontSize = currentSize;
            if (ImGui::SliderFloat("Font Size", &fontSize, 10.0f, 32.0f, "%.0f")) m_UiPreferences->SetFont(currentFont, fontSize);
        }

        void DrawEditing()
        {
            if (SectionHeaderWithReset("EDITING", "editing"))
            {
                m_UiState->AutosaveEnabled = false;
                m_UiState->AutosaveDelaySeconds = kDefaultAutosaveDelaySeconds;
                m_UiState->UndoHistoryCapacity = kDefaultUndoCapacity;
            }
            ImGui::Checkbox("Auto save", &m_UiState->AutosaveEnabled);
            if (ImGui::BeginCombo("Auto save delay", FormatAutosaveDelay(m_UiState->AutosaveDelaySeconds).c_str()))
            {
                for (const int seconds : kAutosaveDelaysSeconds)
                {
                    if (ImGui::Selectable(FormatAutosaveDelay(seconds).c_str(), m_UiState->AutosaveDelaySeconds == seconds))
                        m_UiState->AutosaveDelaySeconds = seconds;
                }
                ImGui::EndCombo();
            }
            UndoCapacityInput("Max undo steps", &m_UiState->UndoHistoryCapacity);
            TextDisabledWrapped("Auto save writes scenes that have a file; recovery copies are always kept");
        }

        void DrawRendering()
        {
            if (SectionHeaderWithReset("RENDERING", "rendering")) m_Render->ResetDefaultProfiles();
            const auto& defaults = m_Render->Defaults();
            for (const auto& [context, name] : {std::pair{RenderContext::Edit, "Edit profile"}, std::pair{RenderContext::Play, "Play profile"},
                                                std::pair{RenderContext::Export, "Export profile"}})
            {
                const std::string label = name;
                if (!ImGui::BeginCombo(label.c_str(), defaults.ProfileName(context).c_str())) continue;
                std::optional<std::string> picked;
                for (const auto& profile : defaults.Profiles())
                {
                    if (ImGui::Selectable(profile.Name.c_str(), profile.Name == defaults.ProfileName(context))) picked = profile.Name;
                    if (ImGui::IsItemHovered() && !profile.Description.empty()) ImGui::SetTooltip("%s", profile.Description.c_str());
                }
                ImGui::EndCombo();
                if (picked) m_Render->SetDefaultProfile(context, *picked);
            }
            TextDisabledWrapped("For new scenes and scenes without their own render settings");
        }

        void DrawUnits()
        {
            if (SectionHeaderWithReset("UNITS", "units")) Units::SetDisplayPreset(Units::DisplayPreset::Auto);
            const auto current = Units::GetDisplayPreset();
            ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
            if (ImGui::BeginCombo("Display units", std::string(Units::DisplayPresetName(current)).c_str()))
            {
                for (const auto preset : Units::kDisplayPresets)
                {
                    if (ImGui::Selectable(std::string(Units::DisplayPresetName(preset)).c_str(), preset == current))
                        Units::SetDisplayPreset(preset);
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Which units the inspector prefers for lengths, masses and times.\n"
                                  "Auto picks by magnitude; Human = m/km/kg, Stellar = Rsun/AU/Msun, Galactic = pc/Msun/yr.\n"
                                  "Right-click a value to force one unit for that field.");
            }
        }

        void DrawExport()
        {
            const auto factory = m_AppPaths->UserSubdir("exports").string();
            if (SectionHeaderWithReset("EXPORT", "export")) m_AppState->SetDefaultExportDirectory(factory);
            std::array<char, 512> exportDir{};
            const auto current = m_AppState->GetDefaultExportDirectory();
            std::ranges::copy(current.substr(0, exportDir.size() - 1), exportDir.begin());
            if (ImGui::InputText("Default Folder", exportDir.data(), exportDir.size()))
                m_AppState->SetDefaultExportDirectory(exportDir.data());
            if (ImGui::Button("Browse...##ExportFolder"))
            {
                if (const auto folder = m_FileDialog->PickFolder(m_FileSystem->ResolvePath(current)))
                    m_AppState->SetDefaultExportDirectory(folder->string());
            }
            ImGui::TextDisabled("Relative paths start at the working directory");
        }

        void DrawKeyboard()
        {
            auto& store = ShortcutOverrideStore::Instance();
            if (SectionHeaderWithReset("KEYBOARD", "keyboard", "Restore every default shortcut")) store.Set({});
            TextDisabledWrapped("Global shortcuts can be rebound here. Keys inside the viewport, the scene window and the graph "
                                "editor are fixed (F1 lists them).");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##shortcutFilter", "Filter actions...", m_KeyFilter.data(), m_KeyFilter.size());
            const std::string filter = m_KeyFilter.data();

            const auto& defaults = DefaultActions();
            const auto& effective = StaticActions();
            std::vector<std::string> categories;
            for (const auto& action : defaults)
            {
                if (IsRebindable(action) && !std::ranges::contains(categories, action.Category)) categories.push_back(action.Category);
            }

            if (!ImGui::BeginTable("##shortcuts", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) return;
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("##buttons", ImGuiTableColumnFlags_WidthFixed);
            for (const auto& category : categories)
            {
                bool headerShown = false;
                for (const auto& action : defaults)
                {
                    if (!IsRebindable(action) || action.Category != category) continue;
                    if (!filter.empty() && !FuzzyScore(filter, category + " " + action.Label)) continue;
                    if (!headerShown)
                    {
                        headerShown = true;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", category.c_str());
                    }
                    DrawShortcutRow(action, effective);
                }
            }
            ImGui::EndTable();
        }

        void DrawShortcutRow(const ActionDescriptor& action, const std::vector<ActionDescriptor>& effective)
        {
            const auto* live = FindAction(effective, action.Id);
            const auto text = live ? FormatShortcuts(live->Shortcut) : std::string();
            const bool overridden = ShortcutOverrideStore::Instance().Overrides().contains(action.Id);

            ImGui::PushID(action.Id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Indent();
            ImGui::TextUnformatted(action.Label.c_str());
            ImGui::Unindent();
            ImGui::TableNextColumn();
            if (text.empty()) ImGui::TextDisabled("unbound");
            else if (overridden) ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "%s", text.c_str());
            else ImGui::TextUnformatted(text.c_str());
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Change")) OpenRebind(action);
            ImGui::SameLine();
            ImGui::BeginDisabled(!overridden);
            if (ImGui::SmallButton("Default"))
            {
                auto overrides = ShortcutOverrideStore::Instance().Overrides();
                ResetShortcut(overrides, action.Id);
                ShortcutOverrideStore::Instance().Set(std::move(overrides));
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }

        void OpenRebind(const ActionDescriptor& action)
        {
            m_RebindId = action.Id;
            m_RebindLabel = action.Label;
            m_Attempt.clear();
            m_Problem.clear();
            m_RebindRequested = true;
        }

        void ApplyOverrides(ShortcutOverrides overrides) { ShortcutOverrideStore::Instance().Set(std::move(overrides)); }

        void DrawRebindPopup()
        {
            if (std::exchange(m_RebindRequested, false)) ImGui::OpenPopup(kRebindPopup);
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
            if (!ImGui::BeginPopupModal(kRebindPopup, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
                return;

            const auto& defaults = DefaultActions();
            const auto* live = FindAction(StaticActions(), m_RebindId);
            ImGui::TextUnformatted(m_RebindLabel.c_str());
            ImGui::TextDisabled("Current: %s", live && !live->Shortcut.empty() ? FormatShortcuts(live->Shortcut).c_str() : "unbound");
            ImGui::Spacing();
            ImGui::TextWrapped("Press the new key combination. Esc cancels.");

            if (const auto chord = CapturePressedChord())
            {
                auto overrides = ShortcutOverrideStore::Instance().Overrides();
                const auto outcome = TrySetShortcut(overrides, defaults, m_RebindId, *chord);
                m_Attempt = *chord;
                if (outcome.Applied)
                {
                    ApplyOverrides(std::move(overrides));
                    ImGui::CloseCurrentPopup();
                }
                else if (!outcome.Problem.empty()) m_Problem = outcome.Problem;
                else m_Problem = "Already used by " + JoinHolders(outcome.Holders);
            }

            if (!m_Attempt.empty() && !m_Problem.empty())
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.4f, 0.35f, 1.0f));
                ImGui::TextWrapped("%s: %s", m_Attempt.c_str(), m_Problem.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();
            if (ImGui::Button("Unbind", ImVec2(110, 0)))
            {
                auto overrides = ShortcutOverrideStore::Instance().Overrides();
                ClearShortcut(overrides, defaults, m_RebindId);
                ApplyOverrides(std::move(overrides));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Use default", ImVec2(110, 0)))
            {
                auto overrides = ShortcutOverrideStore::Instance().Overrides();
                ResetShortcut(overrides, m_RebindId);
                ApplyOverrides(std::move(overrides));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        [[nodiscard]] static std::string JoinHolders(const std::vector<std::string>& holders)
        {
            std::string joined;
            for (const auto& holder : holders)
            {
                if (!joined.empty()) joined += ", ";
                joined += holder;
            }
            return joined;
        }

        std::shared_ptr<FileDialog> m_FileDialog;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<UiPreferences> m_UiPreferences;
        std::shared_ptr<FontAssetCatalog> m_FontAssets;
        std::shared_ptr<FontOptions> m_FontOptions;
        std::shared_ptr<AppStateService> m_AppState;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<RenderSettingsService> m_Render;
        std::shared_ptr<AppPaths> m_AppPaths;
        std::array<char, 64> m_KeyFilter{};
        std::string m_RebindId;
        std::string m_RebindLabel;
        std::string m_Attempt;
        std::string m_Problem;
        bool m_RebindRequested = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SettingsWindowLayer)
