export module MoleHole:UI.CreateMenu;

import std;
import glm;
import imgui;
import GPP;
import :UI.State;
import :UI.Widgets;
import :UI.WidgetLogic;
import :UI.CreateCatalog;

export namespace MoleHole
{
    struct CreateMenuHost
    {
        std::span<const CreateEntry> Entries;
        const CreateNode* Tree = nullptr;
        ImFont* Icons = nullptr;
    };

    class CreateMenu
    {
    public:
        void Reset()
        {
            m_Query.clear();
            m_Highlight = 0;
            m_Appeared = 0;
        }

        [[nodiscard]] std::optional<std::size_t> Draw(const CreateMenuHost& host, const bool focusSearch)
        {
            std::optional<std::size_t> picked;
            if (focusSearch) ImGui::SetKeyboardFocusHere();
            const bool changed = SearchBox(host.Icons, "CreateSearch", m_Query, "Search (type to filter)...", ImGui::GetFontSize() * 17.0f);
            if (m_Appeared < 3 && (m_Query == "A" || m_Query == "a"))
            {
                m_Query.clear();
            }
            ++m_Appeared;
            if (changed) m_Highlight = 0;
            ImGui::Separator();

            if (m_Query.find_first_not_of(" \t") == std::string::npos)
            {
                if (host.Tree) picked = DrawChildren(host, *host.Tree);
                return picked;
            }
            return DrawResults(host);
        }

    private:
        static std::string Padded(const std::string_view label)
        {
            const float space = std::max(1.0f, ImGui::CalcTextSize(" ").x);
            const auto count = static_cast<std::size_t>(std::ceil(kIconColumn / space));
            return std::string(count, ' ') + std::string(label);
        }

        static void DrawEntryIcon(ImFont* icons, const std::uint32_t codepoint, const ImVec2 itemMin, const ImU32 color)
        {
            const float height = ImGui::GetFrameHeight();
            const auto glyph = Utf8Encode(codepoint);
            DrawIcon(ImGui::GetWindowDrawList(), icons, glyph.c_str(), "*",
                     ImVec2(itemMin.x + kIconColumn * 0.5f, itemMin.y + height * 0.5f), 12.0f, color);
        }

        static ImU32 IconColor(const bool folder)
        {
            return ToU32(folder ? HexColor(0xc8a060) : HexColor(0xb8b8b8));
        }

        std::optional<std::size_t> DrawChildren(const CreateMenuHost& host, const CreateNode& parent)
        {
            std::optional<std::size_t> picked;
            for (const auto& child : parent.Children)
            {
                ImGui::PushID(child.Path.c_str());
                const ImVec2 min = ImGui::GetCursorScreenPos();
                if (child.IsFolder())
                {
                    const bool open = ImGui::BeginMenu(Padded(child.Name).c_str());
                    DrawEntryIcon(host.Icons, CreateFolderIcon(child.Path), min, IconColor(true));
                    if (!open)
                    {
                        TooltipForFolder(child.Path);
                    }
                    else
                    {
                        if (const auto inner = DrawChildren(host, child)) picked = inner;
                        ImGui::EndMenu();
                    }
                }
                else
                {
                    const auto& entry = host.Entries[*child.Entry];
                    const bool clicked = ImGui::MenuItem(Padded(child.Name).c_str());
                    DrawEntryIcon(host.Icons, entry.Icon, min, IconColor(false));
                    if (ImGui::IsItemHovered() && !entry.Tooltip.empty()) ImGui::SetTooltip("%s", entry.Tooltip.c_str());
                    if (clicked) picked = *child.Entry;
                }
                ImGui::PopID();
            }
            return picked;
        }

        static void TooltipForFolder(const std::string& path)
        {
            const auto tip = CreateFolderTooltip(path);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !tip.empty())
                ImGui::SetTooltip("%.*s", static_cast<int>(tip.size()), tip.data());
        }

        std::optional<std::size_t> DrawResults(const CreateMenuHost& host)
        {
            std::optional<std::size_t> picked;
            const auto results = FilterCreateEntries(host.Entries, m_Query);
            if (results.empty())
            {
                ImGui::TextDisabled("Nothing matches '%s'", m_Query.c_str());
                return picked;
            }
            m_Highlight = std::clamp(m_Highlight, 0, static_cast<int>(results.size()) - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) m_Highlight = std::min(m_Highlight + 1, static_cast<int>(results.size()) - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) m_Highlight = std::max(m_Highlight - 1, 0);
            const bool accept = ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);

            const float rowHeight = ImGui::GetFrameHeight();
            const float listWidth = ImGui::GetFontSize() * 17.0f;
            ImGui::BeginChild("##createResults", ImVec2(listWidth, std::min(static_cast<float>(results.size()), 12.0f) * rowHeight + 4.0f));
            for (std::size_t row = 0; row < results.size(); ++row)
            {
                const auto& entry = host.Entries[results[row]];
                ImGui::PushID(entry.Id.c_str());
                const ImVec2 min = ImGui::GetCursorScreenPos();
                const float width = ImGui::GetContentRegionAvail().x;
                const bool selected = static_cast<int>(row) == m_Highlight;
                if (ImGui::Selectable("##row", selected, 0, ImVec2(0.0f, rowHeight))) picked = results[row];
                if (ImGui::IsItemHovered())
                {
                    if (ImGui::GetIO().MouseDelta.x != 0.0f || ImGui::GetIO().MouseDelta.y != 0.0f) m_Highlight = static_cast<int>(row);
                    if (!entry.Tooltip.empty()) ImGui::SetTooltip("%s", entry.Tooltip.c_str());
                }
                if (selected && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))) ImGui::SetScrollHereY();
                DrawEntryIcon(host.Icons, entry.Icon, min, IconColor(false));

                auto* list = ImGui::GetWindowDrawList();
                const float textY = min.y + (rowHeight - ImGui::GetTextLineHeight()) * 0.5f;
                const float folderWidth = ImGui::CalcTextSize(entry.Folder.c_str()).x;
                const float nameRight = min.x + width - (entry.Folder.empty() ? 0.0f : folderWidth + 12.0f);
                list->PushClipRect(ImVec2(min.x, min.y), ImVec2(std::max(nameRight, min.x + kIconColumn + 20.0f), min.y + rowHeight), true);
                list->AddText(ImVec2(min.x + kIconColumn, textY), ToU32(HexColor(0xe0e0e0)), entry.Name.c_str());
                list->PopClipRect();
                if (!entry.Folder.empty() && nameRight > min.x + kIconColumn + 40.0f)
                {
                    list->AddText(ImVec2(min.x + width - folderWidth - 4.0f, textY), ToU32(HexColor(0x707070)), entry.Folder.c_str());
                }
                if (selected && accept) picked = results[row];
                ImGui::PopID();
            }
            ImGui::EndChild();
            return picked;
        }

        static constexpr float kIconColumn = 26.0f;

        std::string m_Query;
        int m_Highlight = 0;
        int m_Appeared = 0;
    };

    inline bool CreateFromEntry(GPP::SimulationRunner& runner, UiState& ui, const CreateEntry& entry)
    {
        if (entry.Action == CreateAction::MeshFromFile)
        {
            ui.OpenAddMeshDialog = true;
            return true;
        }
        std::optional<CreatedEntities> built;
        {
            const auto sceneLock = runner.LockRenderScene();
            const CreatePlacement placement{ui.ViewPositionM, ui.ViewFront, ui.ViewUp, ui.CameraSpeedMs, std::nullopt};
            built = BuildCreateCommands(*sceneLock, entry, placement, [] { return GPP::GenerateGuid(); });
        }
        if (!built || built->Commands.empty()) return false;
        GPP::CommandOptions options;
        options.Undoable = runner.IsPaused();
        options.Label = built->Label;
        runner.EnqueueCommands(std::move(built->Commands), std::move(options));
        ui.PendingSelectGuid = built->Guids.front();
        return true;
    }
}
