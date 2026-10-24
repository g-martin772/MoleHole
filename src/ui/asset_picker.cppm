export module MoleHole:UI.AssetPicker;

import std;
import imgui;
import GPP;
import :UI.AssetCatalog;

export namespace MoleHole
{
    struct AssetPickerHost
    {
        AssetCatalog* Catalog = nullptr;
        std::function<AssetScanConfig(GPP::AssetFieldKind)> ConfigFor;
        std::vector<std::string> Recents;
        std::function<void(const std::string&)> Remember;
    };

    class AssetPicker
    {
    public:
        using Browse = std::function<std::optional<std::string>()>;

        std::optional<std::string> Draw(const AssetPickerHost& host, const GPP::AssetFieldKind kind, const std::string& current,
                                        const Browse& browse)
        {
            std::optional<std::string> picked;
            if (ImGui::Button("...", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
            {
                m_Filter.clear();
                m_Selected = 0;
                m_Folder = ParentFolder(current);
                m_Refresh = true;
                ImGui::OpenPopup("##asset_picker");
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a project asset");

            if (!ImGui::BeginPopup("##asset_picker")) return picked;
            picked = DrawContent(host, kind, current, browse);
            if (picked)
            {
                if (!picked->empty() && host.Remember) host.Remember(*picked);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
            return picked;
        }

    private:
        static std::string ParentFolder(const std::string& path)
        {
            const auto slash = path.rfind('/');
            return slash == std::string::npos ? std::string{} : path.substr(0, slash);
        }

        static std::string Join(const std::string& folder, const std::string& name)
        {
            return folder.empty() ? name : folder + '/' + name;
        }

        std::optional<std::string> DrawContent(const AssetPickerHost& host, const GPP::AssetFieldKind kind,
                                               const std::string& current, const Browse& browse)
        {
            std::optional<std::string> picked;
            const auto snapshot = host.Catalog->Get(kind, host.ConfigFor(kind), std::exchange(m_Refresh, false));
            const std::span<const AssetEntry> entries = snapshot.Items ? std::span<const AssetEntry>(*snapshot.Items)
                                                                        : std::span<const AssetEntry>{};

            const float refreshWidth = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.0f;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 30.0f - refreshWidth - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            std::array<char, 256> buffer{};
            std::copy_n(m_Filter.data(), std::min(m_Filter.size(), buffer.size() - 1), buffer.data());
            if (ImGui::InputTextWithHint("##filter", "Search assets...", buffer.data(), buffer.size(),
                                         ImGuiInputTextFlags_EscapeClearsAll))
            {
                m_Filter = buffer.data();
                m_Selected = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) m_Refresh = true;

            const bool searching = m_Filter.find_first_not_of(" \t") != std::string::npos;
            std::vector<std::string> kindRecents;
            for (const auto& recent : host.Recents)
            {
                if (AssetMatchesKind(recent, kind)) kindRecents.push_back(recent);
            }

            const float width = ImGui::GetFontSize() * 30.0f;
            ImGui::BeginChild("##asset_list", ImVec2(width, ImGui::GetFontSize() * 22.0f), ImGuiChildFlags_Borders);
            if (!snapshot.Items)
            {
                ImGui::TextDisabled("Scanning assets...");
            }
            else if (searching)
            {
                picked = DrawSearchResults(entries, kindRecents, current);
            }
            else
            {
                picked = DrawBrowse(entries, kindRecents, current);
            }
            ImGui::EndChild();

            if (ImGui::Button("Browse...") && browse)
            {
                if (auto path = browse()) picked = std::move(path);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a file from anywhere; images outside the project are imported");
            ImGui::SameLine();
            ImGui::BeginDisabled(current.empty());
            if (ImGui::Button("Clear")) picked = std::string{};
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (snapshot.Scanning) ImGui::TextDisabled("Scanning...");
            else ImGui::TextDisabled("%zu assets", entries.size());
            return picked;
        }

        std::optional<std::string> DrawSearchResults(const std::span<const AssetEntry> entries,
                                                     const std::vector<std::string>& recents, const std::string& current)
        {
            std::optional<std::string> picked;
            const auto ranked = RankAssets(entries, m_Filter, recents);
            if (ranked.empty())
            {
                ImGui::TextDisabled("No matches.");
                return picked;
            }
            m_Selected = std::min(m_Selected, ranked.size() - 1);
            const bool moved = ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && m_Selected + 1 < ranked.size()) ++m_Selected;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && m_Selected > 0) --m_Selected;
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            {
                return entries[ranked[m_Selected]].Path;
            }

            constexpr std::size_t kMaxRows = 300;
            for (std::size_t row = 0; row < std::min(ranked.size(), kMaxRows); ++row)
            {
                const auto& entry = entries[ranked[row]];
                if (DrawEntry(entry, entry.Path == current, row == m_Selected, true)) picked = entry.Path;
                if (moved && row == m_Selected) ImGui::SetScrollHereY();
            }
            if (ranked.size() > kMaxRows) ImGui::TextDisabled("%zu more: refine the search", ranked.size() - kMaxRows);
            return picked;
        }

        std::optional<std::string> DrawBrowse(const std::span<const AssetEntry> entries, const std::vector<std::string>& recents,
                                              const std::string& current)
        {
            std::optional<std::string> picked;
            const auto lookup = [&](const std::string& path) -> const AssetEntry*
            {
                const auto it = std::ranges::find(entries, path, &AssetEntry::Path);
                return it == entries.end() ? nullptr : &*it;
            };

            if (!recents.empty() && m_Folder.empty())
            {
                ImGui::TextDisabled("Recent");
                std::size_t shown = 0;
                for (const auto& path : recents)
                {
                    const auto* entry = lookup(path);
                    if (!entry || shown++ >= 6) continue;
                    ImGui::PushID(("recent:" + path).c_str());
                    if (DrawEntry(*entry, entry->Path == current, false, true)) picked = entry->Path;
                    ImGui::PopID();
                }
                if (shown > 0) ImGui::Separator();
            }

            DrawBreadcrumb();
            const auto view = ListAssetFolder(entries, m_Folder);
            for (const auto& [name, count] : view.Folders)
            {
                ImGui::PushID(name.c_str());
                const bool open = ImGui::Selectable(("[+] " + name).c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
                ImGui::SameLine();
                ImGui::TextDisabled("%zu", count);
                ImGui::PopID();
                if (open)
                {
                    m_Folder = Join(m_Folder, name);
                    break;
                }
            }
            for (const auto index : view.Files)
            {
                const auto& entry = entries[index];
                if (DrawEntry(entry, entry.Path == current, false, false)) picked = entry.Path;
            }
            if (view.Folders.empty() && view.Files.empty()) ImGui::TextDisabled("Nothing here.");
            return picked;
        }

        void DrawBreadcrumb()
        {
            if (ImGui::SmallButton("project")) m_Folder.clear();
            std::string accumulated;
            std::size_t start = 0;
            while (start < m_Folder.size())
            {
                auto end = m_Folder.find('/', start == 0 && m_Folder[0] == '/' ? 1 : start);
                if (end == std::string::npos) end = m_Folder.size();
                const auto segment = m_Folder.substr(start, end - start);
                accumulated = m_Folder.substr(0, end);
                ImGui::SameLine();
                ImGui::TextDisabled("/");
                ImGui::SameLine();
                ImGui::PushID(accumulated.c_str());
                if (ImGui::SmallButton(segment.c_str())) m_Folder = accumulated;
                ImGui::PopID();
                start = end + 1;
            }
        }

        static bool DrawEntry(const AssetEntry& entry, const bool isCurrent, const bool highlighted, const bool showFolder)
        {
            ImGui::PushID(entry.Path.c_str());
            const auto& style = ImGui::GetStyle();
            const float width = ImGui::GetContentRegionAvail().x;
            const ImVec2 min = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##entry", isCurrent || highlighted, 0, ImVec2(width, 0.0f));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.Path.c_str());

            auto* draw = ImGui::GetWindowDrawList();
            const std::string size = FormatAssetSize(entry.Size);
            const float sizeWidth = ImGui::CalcTextSize(size.c_str()).x;
            const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
            const float right = min.x + width - sizeWidth;
            draw->AddText(ImVec2(right, min.y), dim, size.c_str());

            draw->PushClipRect(min, ImVec2(right - style.ItemSpacing.x, min.y + ImGui::GetTextLineHeight()), true);
            const std::string name = entry.Name();
            draw->AddText(min, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            if (showFolder)
            {
                const auto folder = ParentFolder(entry.Path);
                if (!folder.empty())
                {
                    draw->AddText(ImVec2(min.x + ImGui::CalcTextSize(name.c_str()).x + style.ItemSpacing.x, min.y), dim,
                                  folder.c_str());
                }
            }
            draw->PopClipRect();
            ImGui::PopID();
            return clicked;
        }

        std::string m_Filter;
        std::string m_Folder;
        std::size_t m_Selected = 0;
        bool m_Refresh = false;
    };
}
