import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr const char* kPopup = "##CommandPalette";
    constexpr int kVisibleRows = 12;

    struct CommandPaletteLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState, RenderSettingsService, IFileSystem, AssetDirectories>;

        CommandPaletteLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                            std::shared_ptr<UiState> uiState, std::shared_ptr<RenderSettingsService> renderSettings,
                            std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<AssetDirectories> assets)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState)),
              m_Render(std::move(renderSettings)), m_FileSystem(std::move(fileSystem)), m_Assets(std::move(assets))
        {
        }

        void OnUiRender() override
        {
            HandleHotkeys();
            DrawPalette();
            DrawShortcutSheet();
        }

    private:
        [[nodiscard]] std::shared_ptr<SimulationRunner> CurrentRunner() const
        {
            return m_UiState->CurrentSceneName.empty() ? nullptr : m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
        }

        void HandleHotkeys()
        {
            const bool anyPopup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
            bool open = std::exchange(m_UiState->PaletteRequested, false);
            open |= ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_P) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_P);
            if (open && !m_Open && !anyPopup) OpenPalette();

            if (!ImGui::GetIO().WantTextInput && !anyPopup && ImGui::IsKeyChordPressed(ImGuiKey_F1))
                m_UiState->ShowShortcutSheet = !m_UiState->ShowShortcutSheet;
        }

        void OpenPalette()
        {
            m_Open = true;
            m_Query.clear();
            m_Highlight = 0;
            m_Sources = CollectSources();
            ImGui::OpenPopup(kPopup);
        }

        [[nodiscard]] ActionSources CollectSources() const
        {
            ActionSources sources;
            sources.Templates = CollectTemplates(m_FileSystem->ResolvePath("templates"));
            for (const auto& profile : m_Render->Profiles().Profiles()) sources.ProfileNames.push_back(profile.Name);

            auto entries = BuiltinCreateEntries();
            const auto scripts = m_Assets ? ListScripts(*m_Assets) : std::vector<std::string>{};
            std::ranges::move(ScriptCreateEntries(scripts), std::back_inserter(entries));
            sources.CreateEntries = SupportedCreateEntries(std::move(entries), [](const std::string& name)
            {
                return ComponentRegistry::Instance().FindByName(name) != nullptr;
            });
            return sources;
        }

        void Close()
        {
            m_Open = false;
            ImGui::CloseCurrentPopup();
        }

        void DrawPalette()
        {
            if (!m_Open) return;
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float width = std::min(ImGui::GetFontSize() * 40.0f, viewport->Size.x * 0.9f);
            ImGui::SetNextWindowPos(ImVec2(viewport->GetCenter().x, viewport->Pos.y + viewport->Size.y * 0.12f), ImGuiCond_Always,
                                    ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
            if (!ImGui::BeginPopup(kPopup, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
            {
                m_Open = false;
                return;
            }

            const auto runner = CurrentRunner();
            const auto actions = BuildActions({m_UiState.get(), runner, m_Render.get()}, m_Sources);
            const auto context = MakeActionContext(*m_UiState, runner.get());

            const bool appearing = ImGui::IsWindowAppearing();
            if (appearing) ImGui::SetKeyboardFocusHere();
            if (SearchBox(m_UiState->IconFont, "PaletteSearch", m_Query, "Type a command...", width - 2.0f * ImGui::GetStyle().WindowPadding.x))
                m_Highlight = 0;

            const auto ranked = RankActions(m_Query, actions, context, m_UiState->RecentActionIds.Ids());
            ImGui::Separator();
            if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                Close();
                ImGui::EndPopup();
                return;
            }
            if (ranked.empty())
            {
                ImGui::TextDisabled("No command matches '%s'", m_Query.c_str());
                ImGui::EndPopup();
                return;
            }

            const int count = static_cast<int>(ranked.size());
            const bool moved = ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                               ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_PageUp);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) ++m_Highlight;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) --m_Highlight;
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) m_Highlight += kVisibleRows;
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) m_Highlight -= kVisibleRows;
            m_Highlight = std::clamp(m_Highlight, 0, count - 1);
            const bool accept = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);

            std::optional<std::size_t> picked;
            const float rowHeight = ImGui::GetFrameHeight();
            const float categoryColumn = ImGui::CalcTextSize("Simulation").x + 14.0f;
            ImGui::BeginChild("##paletteRows", ImVec2(0.0f, std::min(count, kVisibleRows) * (rowHeight + ImGui::GetStyle().ItemSpacing.y) + 4.0f));
            for (int row = 0; row < count; ++row)
            {
                const auto& item = ranked[static_cast<std::size_t>(row)];
                const auto& action = actions[item.Index];
                ImGui::PushID(row);
                const ImVec2 min = ImGui::GetCursorScreenPos();
                const float rowWidth = ImGui::GetContentRegionAvail().x;
                const bool selected = row == m_Highlight;
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selected ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImVec4(0, 0, 0, 0));
                if (ImGui::Selectable("##row", selected, 0, ImVec2(0.0f, rowHeight))) picked = item.Index;
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f)) m_Highlight = row;
                if (selected && moved) ImGui::SetScrollHereY();
                if (selected && accept) picked = item.Index;

                DrawRow(min, rowWidth, rowHeight, categoryColumn, action, item);
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::TextDisabled("Up/Down select   Enter run   Esc close   F1 shortcuts");

            if (picked)
            {
                const auto& item = *std::ranges::find(ranked, *picked, &RankedAction::Index);
                if (item.Enabled())
                {
                    const auto& action = actions[*picked];
                    m_UiState->RecentActionIds.Touch(action.Id);
                    Close();
                    action.Invoke();
                }
            }
            ImGui::EndPopup();
        }

        static void DrawRow(const ImVec2 min, const float width, const float height, const float categoryColumn,
                            const ActionDescriptor& action, const RankedAction& item)
        {
            auto* list = ImGui::GetWindowDrawList();
            const float textY = min.y + (height - ImGui::GetTextLineHeight()) * 0.5f;
            const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const ImU32 label = item.Enabled() ? ImGui::GetColorU32(ImGuiCol_Text) : dim;

            std::string right = item.Enabled() ? FormatShortcuts(action.Shortcut) : item.Reason;
            if (item.Enabled() && action.Toggle) right = (*action.Toggle ? "on" : "off") + (right.empty() ? "" : "   " + right);
            const float rightWidth = ImGui::CalcTextSize(right.c_str()).x;
            const float labelLeft = min.x + categoryColumn + 4.0f;
            const float labelRight = min.x + width - rightWidth - 14.0f;

            list->AddText(ImVec2(min.x + 4.0f, textY), dim, action.Category.c_str());
            list->PushClipRect(ImVec2(labelLeft, min.y), ImVec2(std::max(labelRight, labelLeft + 20.0f), min.y + height), true);
            list->AddText(ImVec2(labelLeft, textY), label, action.Label.c_str());
            list->PopClipRect();
            if (!right.empty())
                list->AddText(ImVec2(min.x + width - rightWidth - 4.0f, textY),
                              item.Enabled() ? dim : ImGui::ColorConvertFloat4ToU32(ImVec4(0.95f, 0.75f, 0.3f, 1.0f)), right.c_str());
        }

        void DrawShortcutSheet()
        {
            if (!m_UiState->ShowShortcutSheet) return;
            ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 42.0f, ImGui::GetFontSize() * 32.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Keyboard Shortcuts", &m_UiState->ShowShortcutSheet))
            {
                ImGui::End();
                return;
            }
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
                ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                m_UiState->ShowShortcutSheet = false;
            }

            SearchBox(m_UiState->IconFont, "ShortcutFilter", m_SheetFilter, "Filter shortcuts...");
            const auto groups = BuildShortcutSheet(StaticActions(), m_SheetFilter);
            if (groups.empty()) ImGui::TextDisabled("No shortcut matches '%s'", m_SheetFilter.c_str());
            for (const auto& group : groups)
            {
                SectionHeader(group.Category.c_str());
                if (!ImGui::BeginTable(group.Category.c_str(), 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) continue;
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_None, 3.0f);
                ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_None, 4.0f);
                ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_None, 3.0f);
                for (const auto& row : group.Rows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.Label.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(row.Shortcut.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", row.Scope.empty() ? "Anywhere" : row.Scope.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::End();
        }

        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<RenderSettingsService> m_Render;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<AssetDirectories> m_Assets;
        ActionSources m_Sources;
        std::string m_Query;
        std::string m_SheetFilter;
        int m_Highlight = 0;
        bool m_Open = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(CommandPaletteLayer)
