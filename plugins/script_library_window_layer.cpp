#include <imgui.h>

import GPP;
import MoleHole;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    constexpr float kStackedWidth = 520.0f;

    struct ScriptLibraryWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState, AssetDirectories, IFileSystem>;

        ScriptLibraryWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                                 std::shared_ptr<UiState> uiState, std::shared_ptr<AssetDirectories> assets,
                                 std::shared_ptr<IFileSystem> fileSystem)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState)),
              m_Assets(std::move(assets)), m_FileSystem(std::move(fileSystem))
        {
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowScriptLibraryWindow) return;
            ImGui::SetNextWindowSize(ImVec2(760.0f, 460.0f), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Script Library", &m_UiState->ShowScriptLibraryWindow))
            {
                ImGui::End();
                return;
            }
            Rescan(false);
            DrawToolbar();
            const bool stacked = ImGui::GetContentRegionAvail().x < kStackedWidth;
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (stacked)
            {
                if (ImGui::BeginChild("##list", ImVec2(0.0f, std::max(avail.y * 0.35f, 60.0f)), ImGuiChildFlags_Borders)) DrawList();
                ImGui::EndChild();
                if (ImGui::BeginChild("##details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) DrawDetails();
                ImGui::EndChild();
            }
            else
            {
                if (ImGui::BeginChild("##list", ImVec2(std::clamp(avail.x * 0.33f, 180.0f, 300.0f), 0.0f), ImGuiChildFlags_Borders)) DrawList();
                ImGui::EndChild();
                ImGui::SameLine();
                if (ImGui::BeginChild("##details", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) DrawDetails();
                ImGui::EndChild();
            }
            ImGui::End();
        }

    private:
        void Rescan(const bool force)
        {
            const auto version = m_Assets->Version();
            if (!force && m_Scanned && version == m_SeenVersion) return;
            m_Scanned = true;
            m_SeenVersion = version;
            m_SourceName.clear();
            const auto directories = m_Assets->Directories(kScriptAssetKind);
            m_Entries = BuildScriptLibrary(directories, m_FileSystem->ResolvePath("templates"));
            m_Order = FilterScriptLibrary(m_Entries, m_Query);
        }

        void DrawToolbar()
        {
            ImFont* icons = m_UiState->IconFont;
            if (SearchBox(icons, "ScriptLibrarySearch", m_Query, "Search scripts, properties, events..."))
                m_Order = FilterScriptLibrary(m_Entries, m_Query);
            if (ImGui::Button("Refresh")) Rescan(true);
            Tooltip("Rescan scripts/ and templates/ (also happens on file changes)");
            ImGui::SameLine();
            ImGui::TextDisabled("%zu of %zu", m_Order.size(), m_Entries.size());
        }

        const ScriptLibraryEntry* Selected() const
        {
            const auto found = std::ranges::find(m_Entries, m_Selected, &ScriptLibraryEntry::Name);
            return found == m_Entries.end() ? nullptr : &*found;
        }

        void DrawList()
        {
            if (m_Order.empty())
            {
                ImGui::TextDisabled(m_Entries.empty() ? "No scripts found" : "No matches");
                return;
            }
            bool libraryHeader = false;
            bool scriptHeader = false;
            for (const auto index : m_Order)
            {
                const auto& entry = m_Entries[index];
                if (m_Query.empty())
                {
                    if (!entry.Library && !std::exchange(scriptHeader, true)) ImGui::TextDisabled("Scripts");
                    if (entry.Library && !std::exchange(libraryHeader, true)) ImGui::TextDisabled("Libraries");
                }
                ImGui::PushID(entry.Name.c_str());
                if (ImGui::Selectable(entry.Name.c_str(), entry.Name == m_Selected)) m_Selected = entry.Name;
                ImGui::PopID();
            }
        }

        void DrawDetails()
        {
            const auto* entry = Selected();
            if (!entry)
            {
                ImGui::TextDisabled("Select a script to see its properties, events and source");
                return;
            }
            ImGui::TextUnformatted(entry->Name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s  |  %zu lines", entry->Library ? "library" : "component script", entry->Lines);
            ImGui::TextDisabled("%s", entry->Path.string().c_str());
            DrawActions(*entry);
            if (!ImGui::BeginTabBar("##scriptTabs")) return;
            if (ImGui::BeginTabItem("Overview"))
            {
                if (ImGui::BeginChild("##overview", ImVec2(0.0f, 0.0f))) DrawOverview(*entry);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Source"))
            {
                DrawSource(*entry);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        void DrawActions(const ScriptLibraryEntry& entry)
        {
            const auto runner = m_UiState->CurrentSceneName.empty() ? nullptr : m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            const std::uint64_t guid = m_UiState->SelectedEntityGuid;
            std::string target;
            std::string blocked;
            if (entry.Library) blocked = "Libraries are loaded with require(), not added to entities";
            else if (!runner) blocked = "No scene loaded";
            else if (guid == 0) blocked = "Select an entity first";
            else
            {
                auto scene = runner->LockRenderScene();
                const auto handle = scene->FindByGuid(guid);
                if (!scene->IsValid(handle)) blocked = "Select an entity first";
                else if (const auto* metadata = scene->Registry().try_get<MetadataComponent>(handle)) target = metadata->Name;
            }

            ImGui::BeginDisabled(!blocked.empty());
            const bool add = ImGui::Button("Add to Selected Entity");
            ImGui::EndDisabled();
            Tooltip(blocked.empty() ? std::format("Add to '{}' (undoable)", target).c_str() : blocked.c_str());
            if (add) AddToEntity(*runner, guid, entry.Name);
            ImGui::SameLine();
            if (ImGui::Button("Copy Path"))
            {
                ImGui::SetClipboardText(std::filesystem::absolute(entry.Path).string().c_str());
                m_Logger->Notifications()->Post(NotificationKind::Info, "Path copied", entry.Name, 2.0f, "script-library");
            }
            ImGui::SameLine();
            if (ImGui::Button("Open Folder"))
            {
                if (const auto opened = OpenPath(std::filesystem::absolute(entry.Path).parent_path()); !opened)
                    m_Logger->Notifications()->Post(NotificationKind::Error, "Could not open folder", opened.error(), 0.0f, "script-library");
            }
        }

        static void AddToEntity(SimulationRunner& runner, const std::uint64_t guid, const std::string& name)
        {
            ScriptsComponent edited;
            {
                auto scene = runner.LockRenderScene();
                const auto handle = scene->FindByGuid(guid);
                if (!scene->IsValid(handle)) return;
                if (const auto* current = scene->Registry().try_get<ScriptsComponent>(handle)) edited = *current;
            }
            edited.Entries.push_back(ScriptEntry{name, {}});
            CommandOptions options;
            options.Undoable = runner.IsPaused();
            options.Label = "Add Script";
            options.CoalesceKey = std::format("{}:Scripts", guid);
            runner.EnqueueCommand(MakeSetComponentCommand(guid, kScriptsComponentName, edited), std::move(options));
        }

        static void DrawNameList(const char* title, const std::vector<std::string>& names, const char* empty)
        {
            SectionHeader(title);
            if (names.empty())
            {
                ImGui::TextDisabled("%s", empty);
                return;
            }
            for (const auto& name : names) ImGui::BulletText("%s", name.c_str());
        }

        void DrawOverview(const ScriptLibraryEntry& entry)
        {
            if (!entry.Library)
            {
                SectionHeader("Properties");
                if (entry.Properties.empty()) ImGui::TextDisabled("None declared");
                else if (ImGui::BeginTable("##props", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Type");
                    ImGui::TableSetupColumn("Default");
                    ImGui::TableSetupColumn("Range");
                    ImGui::TableHeadersRow();
                    for (const auto& property : entry.Properties)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(property.Name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(property.Type.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(property.Default.empty() ? "-" : property.Default.c_str());
                        if (!property.Unit.empty())
                        {
                            ImGui::SameLine();
                            ImGui::TextDisabled("%s", property.Unit.c_str());
                        }
                        ImGui::TableNextColumn();
                        if (property.Min.empty() && property.Max.empty()) ImGui::TextDisabled("-");
                        else ImGui::Text("%s .. %s", property.Min.empty() ? "-" : property.Min.c_str(), property.Max.empty() ? "-" : property.Max.c_str());
                    }
                    ImGui::EndTable();
                }
                DrawNameList("Callbacks", entry.Callbacks, "None");
            }
            DrawNameList("Events emitted", entry.Emits, "None");
            DrawNameList("Events handled", entry.Handles, "None");
            DrawNameList("Requires", entry.Requires, "No libraries");
            if (entry.Library) DrawNameList("Required by", entry.RequiredBy, "Not required by any script");
            else DrawNameList("Used in templates", entry.Templates, "No template uses it");
        }

        void DrawSource(const ScriptLibraryEntry& entry)
        {
            if (m_SourceName != entry.Name || m_SourceLines.empty())
            {
                m_SourceName = entry.Name;
                m_SourceLines.clear();
                m_SourceColumns = 0;
                std::size_t begin = 0;
                while (begin <= entry.Source.size())
                {
                    auto end = entry.Source.find('\n', begin);
                    if (end == std::string::npos) end = entry.Source.size();
                    auto line = entry.Source.substr(begin, end - begin);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    m_SourceColumns = std::max(m_SourceColumns, line.size());
                    m_SourceLines.push_back(std::move(line));
                    begin = end + 1;
                }
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.09f, 0.09f, 1.0f));
            if (ImGui::BeginChild("##source", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
            {
                const float cell = ImGui::CalcTextSize("M").x * 0.9f;
                const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
                const std::size_t gutterDigits = std::to_string(m_SourceLines.size()).size();
                const float gutter = cell * static_cast<float>(gutterDigits + 2);
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                auto* list = ImGui::GetWindowDrawList();
                ImFont* font = ImGui::GetFont();
                const float size = ImGui::GetFontSize();
                const ImU32 numberColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(m_SourceLines.size()), lineHeight);
                while (clipper.Step())
                {
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                    {
                        const float y = origin.y + lineHeight * static_cast<float>(row);
                        const auto number = std::to_string(row + 1);
                        for (std::size_t i = 0; i < number.size(); ++i)
                        {
                            const float x = origin.x + cell * static_cast<float>(gutterDigits - number.size() + i);
                            list->AddText(font, size, ImVec2(x, y), numberColor, number.data() + i, number.data() + i + 1);
                        }
                        const auto& line = m_SourceLines[static_cast<std::size_t>(row)];
                        for (std::size_t i = 0; i < line.size(); ++i)
                        {
                            if (line[i] == ' ') continue;
                            const float x = origin.x + gutter + cell * static_cast<float>(i);
                            list->AddText(font, size, ImVec2(x, y), textColor, line.data() + i, line.data() + i + 1);
                        }
                    }
                }
                clipper.End();
                ImGui::Dummy(ImVec2(gutter + cell * static_cast<float>(m_SourceColumns + 1), 1.0f));
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AssetDirectories> m_Assets;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::vector<ScriptLibraryEntry> m_Entries;
        std::vector<std::size_t> m_Order;
        std::string m_Query;
        std::string m_Selected;
        std::string m_SourceName;
        std::vector<std::string> m_SourceLines;
        std::size_t m_SourceColumns = 0;
        std::uint64_t m_SeenVersion = 0;
        bool m_Scanned = false;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ScriptLibraryWindowLayer)
