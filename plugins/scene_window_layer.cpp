import GPP;
import MoleHole;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;
using namespace MoleHole::UiDetail;

namespace
{
    void EditField(SimulationRunner& runner, std::uint64_t guid, std::string component, std::string field,
                   FieldValue value, std::string label)
    {
        CommandOptions options;
        options.Undoable = runner.IsPaused();
        options.Label = std::move(label);
        options.CoalesceKey = std::format("{}:{}:{}", guid, component, field);
        runner.EnqueueCommand(SetFieldCommand{guid, std::move(component), std::move(field), std::move(value)},
                              std::move(options));
    }

    CommandOptions UndoStep(const SimulationRunner& runner, std::string label, std::string key = {})
    {
        CommandOptions options;
        options.Undoable = runner.IsPaused();
        options.Label = std::move(label);
        options.CoalesceKey = std::move(key);
        return options;
    }

    constexpr float kRowHeight = 24.0f;
    constexpr float kIndentWidth = 14.0f;

    bool FieldVisible(const FieldInfo& field, const ComponentTypeInfo& info, const entt::registry& registry,
                      entt::entity entity)
    {
        if (field.Meta.VisibleField.empty()) return true;
        for (const auto& other : info.Fields)
        {
            if (other.Name != field.Meta.VisibleField) continue;
            const auto value = other.Get(registry, entity);
            const auto* index = std::get_if<int>(&value);
            return index != nullptr && (field.Meta.VisibleMask & (1u << *index)) != 0;
        }
        return true;
    }

    // Picks a file for an AssetPath field and returns the project-relative path to store, or nullopt.
    using AssetBrowser = std::function<std::optional<std::string>(const FieldInfo&)>;

    // The single place a descriptor field becomes a widget; returns the edited value when the user changed it.
    std::optional<FieldValue> DrawField(ImFont* icons, const FieldInfo& field, const FieldValue& current,
                                        const FieldValue* fallback, const std::vector<EntityChoice>& entities,
                                        const AssetBrowser& browse = {}, AssetPicker* picker = nullptr,
                                        const AssetPickerHost* pickerHost = nullptr)
    {
        const auto& meta = field.Meta;
        const std::string label = meta.Label.empty() ? SpacedName(field.Name) : meta.Label;
        const float speed = meta.Speed > 0.0 ? static_cast<float>(meta.Speed) : 0.1f;
        const double upperBound = meta.Max > meta.Min ? meta.Max : (meta.Min > 0.0 ? 1.0e300 : 0.0);
        const char* format = meta.Format.empty() ? "%.3f" : meta.Format.c_str();
        const bool modified = fallback && !field.ReadOnly() && !(*fallback == current);

        std::optional<FieldValue> edited;
        const bool reset = PropertyRow(icons, label.c_str(), modified, [&]
        {
            ImGui::BeginDisabled(field.ReadOnly());
            switch (field.Type)
            {
            case FieldType::Bool:
            {
                bool v = std::get<bool>(current);
                if (ToggleSwitch("##v", &v)) edited = v;
                break;
            }
            case FieldType::Int:
            {
                int v = std::get<int>(current);
                if (DragIntValue("##v", &v, speed, meta.Min, meta.Max)) edited = v;
                break;
            }
            case FieldType::Float:
            {
                float v = std::get<float>(current);
                if (meta.Dim != Units::Dimension::None)
                {
                    double d = v;
                    if (QuantityValue("##v", &d, meta.Dim, meta.Speed, meta.Min, upperBound)) edited = static_cast<float>(d);
                }
                else if (DragFloatValue("##v", &v, speed, meta.Min, meta.Max, format)) edited = v;
                break;
            }
            case FieldType::Double:
            {
                double v = std::get<double>(current);
                const bool changed = meta.Dim != Units::Dimension::None
                                         ? QuantityValue("##v", &v, meta.Dim, meta.Speed, meta.Min, upperBound)
                                         : DragDoubleValue("##v", &v, speed, meta.Min, meta.Max, format);
                if (changed) edited = v;
                break;
            }
            case FieldType::DVec3:
            {
                glm::dvec3 v = std::get<glm::dvec3>(current);
                const glm::dvec3 d = fallback ? std::get<glm::dvec3>(*fallback) : v;
                if (QuantityVecValue("##v", &v.x, 3, fallback ? &d.x : nullptr, meta.Dim, meta.Speed, meta.Min,
                                     upperBound))
                {
                    edited = v;
                }
                break;
            }
            case FieldType::Vec2:
            {
                glm::vec2 v = std::get<glm::vec2>(current);
                const glm::vec2 d = fallback ? std::get<glm::vec2>(*fallback) : v;
                if (VecValue("##v", &v.x, 2, fallback ? &d.x : nullptr, speed, meta.Min, meta.Max, format)) edited = v;
                break;
            }
            case FieldType::Vec3:
            {
                glm::vec3 v = std::get<glm::vec3>(current);
                const glm::vec3 d = fallback ? std::get<glm::vec3>(*fallback) : v;
                const bool changed = meta.Kind == FieldKind::Color
                                         ? ColorValue("##v", &v.x, false)
                                         : VecValue("##v", &v.x, 3, fallback ? &d.x : nullptr, speed, meta.Min,
                                                    meta.Max, format);
                if (changed) edited = v;
                break;
            }
            case FieldType::Vec4:
            {
                glm::vec4 v = std::get<glm::vec4>(current);
                const glm::vec4 d = fallback ? std::get<glm::vec4>(*fallback) : v;
                const bool changed = meta.Kind == FieldKind::Color
                                         ? ColorValue("##v", &v.x, true)
                                         : VecValue("##v", &v.x, 4, fallback ? &d.x : nullptr, speed, meta.Min,
                                                    meta.Max, format);
                if (changed) edited = v;
                break;
            }
            case FieldType::String:
            {
                std::string text = std::get<std::string>(current);
                const bool asset = meta.Kind == FieldKind::AssetPath && browse;
                if (asset) ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemInnerSpacing.x);
                if (TextValue("##v", text, "", meta.Kind == FieldKind::Multiline)) edited = std::move(text);
                if (asset)
                {
                    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
                    if (picker && pickerHost)
                    {
                        if (auto picked = picker->Draw(*pickerHost, meta.Asset, text, [&] { return browse(field); }))
                        {
                            edited = std::move(*picked);
                        }
                    }
                    else
                    {
                        if (ImGui::Button("...", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                        {
                            if (auto picked = browse(field)) edited = std::move(*picked);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pick a file; images outside the project are imported");
                    }
                }
                break;
            }
            case FieldType::Entity:
            {
                auto v = std::get<std::uint64_t>(current);
                if (EntityPicker("##v", &v, entities)) edited = v;
                break;
            }
            case FieldType::Enum:
            {
                int v = std::get<int>(current);
                if (EnumCombo("##v", &v, meta.Options)) edited = v;
                break;
            }
            }
            ImGui::EndDisabled();
        });
        if (reset && fallback) edited = *fallback;
        return edited;
    }

    using DefaultsMap = std::unordered_map<std::string, std::unordered_map<std::string, FieldValue>>;

    const std::unordered_map<std::string, FieldValue>& DefaultsFor(DefaultsMap& cache, const ComponentTypeInfo& info)
    {
        const auto it = cache.find(info.Name);
        if (it != cache.end()) return it->second;
        auto& values = cache[info.Name];
        if (!info.Add) return values;
        entt::registry scratch;
        const auto entity = scratch.create();
        info.Add(scratch, entity);
        for (const auto& field : info.Fields)
        {
            auto value = field.Get(scratch, entity);
            if (!std::holds_alternative<std::monostate>(value)) values.emplace(field.Name, std::move(value));
        }
        return values;
    }

    struct SceneWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState, AppStateService, IFileSystem, FileDialog, AssetDirectories, AppPaths>;

        SceneWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                         std::shared_ptr<UiState> uiState, std::shared_ptr<AppStateService> appState,
                         std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<FileDialog> fileDialog,
                         std::shared_ptr<AssetDirectories> assets, std::shared_ptr<AppPaths> appPaths)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState)),
              m_AppState(std::move(appState)), m_FileSystem(std::move(fileSystem)),
              m_FileDialog(std::move(fileDialog)), m_Assets(std::move(assets)), m_AppPaths(std::move(appPaths))
        {
        }

        void OnAttach() override
        {
            EnsureIconFont(*m_UiState);
        }

        void OnUiRender() override
        {
            ProcessEditRequests();
            ProcessBoxSelect();
            if (!m_UiState->ShowSceneWindow) return;
            if (!ImGui::Begin("Scene", &m_UiState->ShowSceneWindow))
            {
                ImGui::End();
                return;
            }

            if (m_UiState->CurrentSceneName.empty())
            {
                ImGui::TextDisabled("No scene loaded");
                ImGui::End();
                return;
            }

            const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            if (!runner)
            {
                ImGui::TextDisabled("Scene has no active simulation");
                ImGui::End();
                return;
            }

            ImFont* icons = m_UiState->IconFont;
            ImGui::TextDisabled("%s%s", m_UiState->SceneState.IsDirty() ? "* " : "",
                                m_UiState->CurrentScenePath.empty() ? "(unsaved)" : m_UiState->CurrentScenePath.c_str());
            ImGui::TextDisabled("%s", runner->IsPaused() ? "Paused" : "Running");

            if (BeginSection(icons, "Outliner"))
            {
                auto sceneLock = runner->LockRenderScene();
                RenderToolbar(*runner, *sceneLock);
                RenderOutliner(*runner, *sceneLock);
                EndSection();
            }

            {
                auto sceneLock = runner->LockRenderScene();
                RenderInspector(*runner, *sceneLock);
            }

            ImGui::End();
        }

    private:
        void ProcessBoxSelect()
        {
            if (!m_UiState->PendingBoxSelect) return;
            const auto request = std::exchange(m_UiState->PendingBoxSelect, std::nullopt);
            if (!request->Additive) m_Selection.clear();
            else if (m_UiState->SelectedEntityGuid != 0) m_Selection.insert(m_UiState->SelectedEntityGuid);
            m_Selection.insert(request->Guids.begin(), request->Guids.end());
            if (!request->Guids.empty()) m_UiState->SelectedEntityGuid = request->Guids.front();
            else if (!request->Additive) m_UiState->SelectedEntityGuid = 0;
            m_Anchor = m_UiState->SelectedEntityGuid;
        }

        void ProcessEditRequests()
        {
            const auto requests = m_UiState->Edits.Take({EditAction::SelectAll, EditAction::Duplicate, EditAction::Delete,
                                                         EditAction::Rename, EditAction::Deselect});
            if (requests.empty() || m_UiState->CurrentSceneName.empty()) return;
            const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            if (!runner) return;
            const auto sceneLock = runner->LockRenderScene();
            const auto entries = CollectEntries(*sceneLock);
            for (const auto action : requests)
            {
                SyncSelection(entries);
                switch (action)
                {
                case EditAction::SelectAll:
                    for (const auto& entry : entries) m_Selection.insert(entry.Guid);
                    if (m_UiState->SelectedEntityGuid == 0 && !entries.empty()) m_UiState->SelectedEntityGuid = entries.front().Guid;
                    break;
                case EditAction::Duplicate:
                    if (!m_Selection.empty()) Duplicate(*runner, entries, {m_Selection.begin(), m_Selection.end()});
                    break;
                case EditAction::Delete:
                    if (!m_Selection.empty()) DeleteEntities(*runner, entries, m_Selection);
                    break;
                case EditAction::Rename:
                    if (const auto it = std::ranges::find(entries, m_UiState->SelectedEntityGuid, &OutlinerEntry::Guid);
                        it != entries.end())
                    {
                        m_UiState->ShowSceneWindow = true;
                        StartRename(*it);
                    }
                    break;
                case EditAction::Deselect:
                    m_Selection.clear();
                    m_UiState->SelectedEntityGuid = 0;
                    break;
                case EditAction::FrameSelected: break;
                }
            }
        }

        void RenderToolbar(SimulationRunner& runner, const Scene&)
        {
            ImFont* icons = m_UiState->IconFont;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float height = ImGui::GetFrameHeight();
            if (ImGui::Button("      Create", ImVec2(0.0f, height)))
            {
                m_UiState->CreatePopup.RequestAt({pos.x, pos.y + height});
            }
            DrawIcon(ImGui::GetWindowDrawList(), icons, Icon::Plus, "+", ImVec2(pos.x + 14.0f, pos.y + height * 0.5f), 12.0f,
                     ToU32(HexColor(0xe0e0e0)));
            Tooltip("Add an entity: body, black hole, light, camera, mesh, script... (Shift+A)");
            if (m_UiState->OpenAddMeshDialog) OpenAddMeshPopup();
            RenderAddMeshPopup(runner);
            ImGui::Spacing();
        }

        static void SpawnEmpty(SimulationRunner& runner, std::uint64_t parent)
        {
            runner.EnqueueCommand(MakeSpawnEmptyCommand(GPP::GenerateGuid(), parent), UndoStep(runner, "Add Empty"));
        }

        static std::vector<std::string> NamesOf(const std::vector<OutlinerEntry>& entries,
                                                const std::vector<std::uint64_t>& guids)
        {
            std::vector<std::string> names;
            for (const auto guid : guids)
            {
                if (const auto entry = std::ranges::find(entries, guid, &OutlinerEntry::Guid); entry != entries.end())
                    names.push_back(entry->Name);
            }
            return names;
        }

        static void Reparent(SimulationRunner& runner, const std::vector<OutlinerEntry>& entries,
                             std::vector<std::uint64_t> guids, std::uint64_t parent)
        {
            const auto label = CountLabel("Reparent", NamesOf(entries, guids));
            std::vector<Command> commands;
            for (const auto guid : guids) commands.push_back(SetParentCommand{guid, parent});
            runner.EnqueueCommands(std::move(commands), UndoStep(runner, label));
        }

        static void Rename(SimulationRunner& runner, std::uint64_t guid, std::string name, const std::string& oldName)
        {
            runner.EnqueueCommand(SetFieldCommand{guid, "Metadata", "Name", std::move(name)},
                                  UndoStep(runner, CountLabel("Rename", {oldName}), std::format("rename:{}", guid)));
        }

        static void Duplicate(SimulationRunner& runner, const std::vector<OutlinerEntry>& entries,
                              const std::vector<std::uint64_t>& guids)
        {
            const auto label = CountLabel("Duplicate", NamesOf(entries, guids));
            std::vector<Command> commands;
            for (const auto guid : guids)
            {
                const auto entry = std::ranges::find(entries, guid, &OutlinerEntry::Guid);
                if (entry == entries.end()) continue;
                const auto copy = GPP::GenerateGuid();
                commands.push_back(CloneEntityCommand{guid, copy});
                commands.push_back(SetFieldCommand{copy, "Metadata", "Name", entry->Name + " Copy"});
            }
            runner.EnqueueCommands(std::move(commands), UndoStep(runner, label));
        }

        void DeleteEntities(SimulationRunner& runner, const std::vector<OutlinerEntry>& entries,
                            const std::unordered_set<std::uint64_t>& guids)
        {
            const auto orphans = OrphanedChildren(entries, guids);
            const auto label = CountLabel("Delete", NamesOf(entries, {guids.begin(), guids.end()}));
            std::vector<Command> commands;
            for (const auto child : orphans) commands.push_back(SetParentCommand{child, 0});
            for (const auto guid : guids) commands.push_back(DestroyEntityCommand{guid});
            runner.EnqueueCommands(std::move(commands), UndoStep(runner, label));
            m_Selection.clear();
            m_UiState->SelectedEntityGuid = 0;
        }

        static std::vector<OutlinerEntry> CollectEntries(const Scene& scene)
        {
            std::vector<OutlinerEntry> entries;
            for (auto [entity, metadata] : scene.Registry().view<const MetadataComponent>().each())
            {
                if (metadata.TypeTag == kAnimationGraphDataTypeTag) continue;
                OutlinerEntry entry;
                entry.Guid = metadata.Guid;
                entry.Name = metadata.Name.empty() ? (metadata.TypeTag + " #" + std::to_string(metadata.Guid))
                                                   : metadata.Name;
                entry.Type = metadata.TypeTag;
                if (const auto* hierarchy = scene.Registry().try_get<HierarchyComponent>(entity))
                {
                    entry.Parent = hierarchy->ParentGuid;
                }
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        void SyncSelection(const std::vector<OutlinerEntry>& entries)
        {
            std::unordered_set<std::uint64_t> present;
            for (const auto& entry : entries) present.insert(entry.Guid);
            std::erase_if(m_Selection, [&](std::uint64_t g) { return !present.contains(g); });
            const auto primary = m_UiState->SelectedEntityGuid;
            if (primary == 0) m_Selection.clear();
            else if (!m_Selection.contains(primary)) m_Selection = {primary};
            if (primary != 0 && !present.contains(primary)) m_UiState->SelectedEntityGuid = 0;
        }

        void SelectRow(const std::vector<OutlinerRow>& rows, std::uint64_t guid)
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyShift)
            {
                for (const auto g : RowRange(rows, m_Anchor ? m_Anchor : m_UiState->SelectedEntityGuid, guid))
                {
                    m_Selection.insert(g);
                }
                m_UiState->SelectedEntityGuid = guid;
            }
            else if (io.KeyCtrl)
            {
                if (m_Selection.erase(guid) == 0) m_Selection.insert(guid);
                m_Anchor = guid;
                m_UiState->SelectedEntityGuid = m_Selection.contains(guid)
                                                    ? guid
                                                    : (m_Selection.empty() ? 0 : *m_Selection.begin());
            }
            else
            {
                m_Selection = {guid};
                m_Anchor = guid;
                m_UiState->SelectedEntityGuid = guid;
            }
        }

        void RenderOutliner(SimulationRunner& runner, const Scene& scene)
        {
            ImFont* icons = m_UiState->IconFont;
            const auto entries = CollectEntries(scene);
            SyncSelection(entries);

            SearchBox(icons, "OutlinerSearch", m_Filter, "Search entities...");
            ImGui::Spacing();

            std::unordered_map<std::uint64_t, const OutlinerEntry*> byGuid;
            for (const auto& entry : entries) byGuid.emplace(entry.Guid, &entry);
            const auto rows = BuildOutlinerRows(entries, m_Expanded, m_Filter);

            ImGui::BeginChild("OutlinerRows", ImVec2(0.0f, 260.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

            std::optional<std::pair<std::vector<std::uint64_t>, std::uint64_t>> dropped;
            for (const auto& row : rows)
            {
                const auto& entry = *byGuid.at(row.Guid);
                ImGui::PushID(static_cast<int>(row.Guid & 0x7fffffff));
                const bool selected = m_Selection.contains(row.Guid);
                const float width = ImGui::GetContentRegionAvail().x;
                const ImVec2 pos = ImGui::GetCursorScreenPos();

                const bool pressed = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowOverlap,
                                                       ImVec2(width, kRowHeight));
                const bool hovered = ImGui::IsItemHovered();
                const float arrowX = pos.x + 8.0f + static_cast<float>(row.Depth) * kIndentWidth;
                const bool onArrow = row.HasChildren && std::fabs(ImGui::GetIO().MousePos.x - arrowX) < 9.0f;
                if (pressed)
                {
                    if (onArrow)
                    {
                        if (row.Expanded) m_Expanded.erase(row.Guid);
                        else m_Expanded.insert(row.Guid);
                    }
                    else
                    {
                        SelectRow(rows, row.Guid);
                    }
                }
                if (hovered && ImGui::IsMouseDoubleClicked(0) && !onArrow) StartRename(entry);

                if (ImGui::BeginPopupContextItem("##ctx"))
                {
                    if (!m_Selection.contains(row.Guid))
                    {
                        m_Selection = {row.Guid};
                        m_Anchor = row.Guid;
                        m_UiState->SelectedEntityGuid = row.Guid;
                    }
                    if (ImGui::MenuItem("Rename", "F2")) StartRename(entry);
                    if (ImGui::MenuItem("Duplicate"))
                    {
                        Duplicate(runner, entries, {m_Selection.begin(), m_Selection.end()});
                    }
                    if (ImGui::MenuItem("Add Child"))
                    {
                        SpawnEmpty(runner, row.Guid);
                        m_Expanded.insert(row.Guid);
                    }
                    if (ImGui::MenuItem("Unparent", nullptr, false, entry.Parent != 0))
                    {
                        Reparent(runner, entries, {m_Selection.begin(), m_Selection.end()}, 0);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Delete", "Del")) DeleteEntities(runner, entries, m_Selection);
                    ImGui::EndPopup();
                }

                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    if (!m_Selection.contains(row.Guid))
                    {
                        m_Selection = {row.Guid};
                        m_UiState->SelectedEntityGuid = row.Guid;
                    }
                    ImGui::SetDragDropPayload("OUTLINER_ENTITY", &row.Guid, sizeof(row.Guid));
                    ImGui::Text("%s%s", entry.Name.c_str(), m_Selection.size() > 1 ? " (+)" : "");
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const auto* payload = ImGui::AcceptDragDropPayload("OUTLINER_ENTITY"))
                    {
                        dropped = {std::vector<std::uint64_t>{m_Selection.begin(), m_Selection.end()}, row.Guid};
                    }
                    ImGui::EndDragDropTarget();
                }

                auto* list = ImGui::GetWindowDrawList();
                const float midY = pos.y + kRowHeight * 0.5f;
                if (row.HasChildren)
                {
                    DrawIcon(list, icons, Icon::ChevronRight, ">", ImVec2(arrowX, midY), 10.0f,
                             ToU32(HexColor(onArrow && hovered ? 0xffffff : 0x9a9a9a)),
                             row.Expanded ? 1.5707964f : 0.0f);
                }
                const float iconX = arrowX + 16.0f;
                const auto glyph = Utf8Encode(IconCodepointForType(entry.Type));
                DrawIcon(list, icons, glyph.c_str(), "*", ImVec2(iconX, midY), 12.0f,
                         ToU32(selected ? HexColor(0xffc080) : HexColor(0xa8a8a8)));

                const ImVec2 textPos(iconX + 14.0f, pos.y + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f);
                if (m_RenameGuid == row.Guid)
                {
                    ImGui::SetCursorScreenPos(ImVec2(textPos.x - 4.0f, pos.y + 1.0f));
                    ImGui::SetNextItemWidth(std::max(60.0f, pos.x + width - textPos.x - 8.0f));
                    if (m_RenameFocus)
                    {
                        ImGui::SetKeyboardFocusHere();
                        m_RenameFocus = false;
                    }
                    const bool entered = ImGui::InputText("##rename", m_RenameBuffer.data(), m_RenameBuffer.size(),
                                                          ImGuiInputTextFlags_EnterReturnsTrue |
                                                          ImGuiInputTextFlags_AutoSelectAll);
                    const bool cancelled = ImGui::IsKeyPressed(ImGuiKey_Escape);
                    if (entered || (ImGui::IsItemDeactivated() && !cancelled))
                    {
                        if (m_RenameBuffer[0] != '\0') Rename(runner, row.Guid, m_RenameBuffer.data(), entry.Name);
                        m_RenameGuid = 0;
                    }
                    else if (cancelled)
                    {
                        m_RenameGuid = 0;
                    }
                }
                else
                {
                    list->AddText(textPos, ToU32(HexColor(selected ? 0xffffff : 0xdcdcdc)), entry.Name.c_str());
                }
                ImGui::PopID();
            }

            const ImVec2 rest(ImGui::GetContentRegionAvail().x, std::max(24.0f, ImGui::GetContentRegionAvail().y));
            if (ImGui::InvisibleButton("##empty", rest))
            {
                m_Selection.clear();
                m_UiState->SelectedEntityGuid = 0;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) m_UiState->CreatePopup.RequestAtMouse();
            if (ImGui::BeginDragDropTarget())
            {
                if (ImGui::AcceptDragDropPayload("OUTLINER_ENTITY"))
                {
                    dropped = {std::vector<std::uint64_t>{m_Selection.begin(), m_Selection.end()}, 0};
                }
                ImGui::EndDragDropTarget();
            }

            if (dropped)
            {
                std::vector<std::uint64_t> movable;
                for (const auto guid : dropped->first)
                {
                    if (CanReparent(entries, guid, dropped->second)) movable.push_back(guid);
                }
                if (!movable.empty())
                {
                    Reparent(runner, entries, std::move(movable), dropped->second);
                    if (dropped->second != 0) m_Expanded.insert(dropped->second);
                }
            }

            if ((focused || ImGui::IsWindowHovered()) && !ImGui::IsAnyItemActive() && m_RenameGuid == 0 &&
                ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_A))
            {
                m_UiState->CreatePopup.RequestAtMouse();
            }
            if (focused && !ImGui::IsAnyItemActive() && m_RenameGuid == 0)
            {
                auto& edits = m_UiState->Edits;
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_Selection.empty()) edits.Push(EditAction::Delete);
                else if (ImGui::IsKeyPressed(ImGuiKey_F2)) edits.Push(EditAction::Rename);
                else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D)) edits.Push(EditAction::Duplicate);
                else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) edits.Push(EditAction::SelectAll);
            }
            ImGui::EndChild();
        }

        void StartRename(const OutlinerEntry& entry)
        {
            m_RenameGuid = entry.Guid;
            m_RenameFocus = true;
            std::ranges::fill(m_RenameBuffer, '\0');
            const auto count = std::min(entry.Name.size(), m_RenameBuffer.size() - 1);
            std::ranges::copy(entry.Name.substr(0, count), m_RenameBuffer.begin());
        }

        void OpenAddMeshPopup()
        {
            m_UiState->OpenAddMeshDialog = false;
            std::ranges::fill(m_MeshPathBuffer, '\0');
            ImGui::OpenPopup("Add Mesh");
        }

        void RenderAddMeshPopup(SimulationRunner& runner)
        {

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
            if (!ImGui::BeginPopupModal("Add Mesh", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

            ImGui::InputText("Path", m_MeshPathBuffer.data(), m_MeshPathBuffer.size());
            ImGui::SameLine();
            if (ImGui::Button("Browse..."))
            {
                if (const auto path = m_FileDialog->OpenFile({FileDialogFilter{"glTF Model", "gltf,glb"}}))
                {
                    SetMeshPathBuffer(path->string());
                }
            }

            if (ImGui::BeginCombo("Scanned Models", "Select a scanned model..."))
            {
                const auto scanned = CollectScannedMeshes();
                if (scanned.empty())
                {
                    ImGui::TextDisabled("(no models found under the scanned directories)");
                }
                for (const auto& path : scanned)
                {
                    if (ImGui::Selectable(path.c_str()))
                    {
                        SetMeshPathBuffer(path);
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Add Scan Directory..."))
            {
                if (const auto dir = m_FileDialog->PickFolder())
                {
                    m_AppState->AddMeshScanDirectory(dir->string());
                }
            }

            ImGui::Checkbox("Add physics collider (convex hull)", &m_MeshAddCollider);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            const bool canCreate = m_MeshPathBuffer[0] != '\0';
            ImGui::BeginDisabled(!canCreate);
            if (ImGui::Button("Create", ImVec2(120, 0)))
            {
                const std::string assetPath = m_MeshPathBuffer.data();
                const bool addCollider = m_MeshAddCollider;
                runner.EnqueueEdit([assetPath, addCollider](Scene& scene)
                {
                    const auto entity = scene.CreateEntity("Mesh", "Mesh");
                    scene.Registry().emplace<TransformComponent>(entity);
                    scene.Registry().emplace<MeshComponent>(entity, MeshComponent{.AssetPath = assetPath});
                    if (addCollider)
                    {
                        scene.Registry().emplace<RigidBodyComponent>(
                            entity, RigidBodyComponent{.Type = RigidBodyType::Static});
                        scene.Registry().emplace<ColliderComponent>(
                            entity, ColliderComponent{.Shape = ColliderShape::ConvexMesh});
                    }
                });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        void SetMeshPathBuffer(const std::string& path)
        {
            std::ranges::fill(m_MeshPathBuffer, '\0');
            const auto count = std::min(path.size(), m_MeshPathBuffer.size() - 1);
            std::ranges::copy(path.substr(0, count), m_MeshPathBuffer.begin());
        }

        [[nodiscard]] std::vector<std::string> CollectScannedMeshes() const
        {
            std::vector<std::string> roots{m_FileSystem->ResolvePath("assets/models").string()};
            if (m_AppPaths->IsInstalled()) roots.push_back(m_AppPaths->UserSubdir("models").string());
            const auto extra = m_AppState->GetMeshScanDirectories();
            roots.insert(roots.end(), extra.begin(), extra.end());

            std::vector<std::string> result;
            std::error_code ec;
            for (const auto& root : roots)
            {
                if (!std::filesystem::exists(root, ec)) continue;
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         root, std::filesystem::directory_options::skip_permission_denied, ec))
                {
                    if (ec) break;
                    if (!entry.is_regular_file()) continue;
                    const auto ext = entry.path().extension();
                    if (ext == ".gltf" || ext == ".glb")
                    {
                        result.push_back(entry.path().string());
                    }
                }
            }
            std::ranges::sort(result);
            return result;
        }

        void RenderAddComponent(SimulationRunner& runner, const Scene& scene, entt::entity entity,
                                std::uint64_t guid)
        {
            ImFont* icons = m_UiState->IconFont;
            if (ImGui::Button("Add Component", ImVec2(-1, 0)))
            {
                m_AddFilter.clear();
                ImGui::OpenPopup("AddComponent");
            }
            if (!ImGui::BeginPopup("AddComponent")) return;

            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            SearchBox(icons, "AddComponentSearch", m_AddFilter, "Search components...", 220.0f);
            ImGui::Separator();
            bool any = false;
            ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
            {
                if (!info.Inspectable || !info.Add || info.Has(scene.Registry(), entity)) return;
                if (!ContainsInsensitive(info.DisplayName, m_AddFilter)) return;
                any = true;
                if (!ImGui::MenuItem(info.DisplayName.c_str())) return;
                runner.EnqueueCommand(AddComponentCommand{guid, info.Name, {}},
                                      UndoStep(runner, "Add " + info.DisplayName));
            });
            if (!any) ImGui::TextDisabled("No matching components");
            ImGui::EndPopup();
        }

        void RenderInspector(SimulationRunner& runner, const Scene& scene)
        {
            ImFont* icons = m_UiState->IconFont;
            if (m_Selection.size() > 1)
            {
                SectionHeader("INSPECTOR");
                ImGui::Text("%zu entities selected", m_Selection.size());
                if (ImGui::Button("Delete Selected", ImVec2(-1, 0)))
                {
                    DeleteEntities(runner, CollectEntries(scene), m_Selection);
                }
                return;
            }
            if (m_UiState->SelectedEntityGuid == 0) return;
            const auto entity = scene.FindByGuid(m_UiState->SelectedEntityGuid);
            if (!scene.IsValid(entity))
            {
                m_UiState->SelectedEntityGuid = 0;
                return;
            }
            const auto guid = m_UiState->SelectedEntityGuid;
            const auto* metadata = scene.Registry().try_get<MetadataComponent>(entity);

            SectionHeader("INSPECTOR");
            if (metadata)
            {
                std::string name = metadata->Name;
                ImGui::SetNextItemWidth(-1.0f);
                if (TextValue("##entityName", name, "Name")) Rename(runner, guid, name, metadata->Name);
                if (Chip(metadata->TypeTag.empty() ? "Entity" : metadata->TypeTag.c_str(), Palette::Accent) == 1)
                {
                    ImGui::SetClipboardText(std::to_string(guid).c_str());
                }
                Tooltip("Click to copy GUID");
                ImGui::SameLine();
                ImGui::TextDisabled("GUID %llu", static_cast<unsigned long long>(guid));
            }
            ImGui::Spacing();

            std::vector<EntityChoice> choices;
            for (const auto& entry : CollectEntries(scene))
            {
                if (entry.Guid != guid) choices.push_back({entry.Guid, entry.Name});
            }

            ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
            {
                if (!info.Inspectable || !info.Has(scene.Registry(), entity)) return;
                bool remove = false;
                if (BeginSection(icons, info.DisplayName.c_str(), true, &remove))
                {
                    DrawComponentFields(runner, scene, entity, guid, info, choices);
                    EndSection();
                }
                if (remove && info.Remove)
                {
                    runner.EnqueueCommand(RemoveComponentCommand{guid, info.Name},
                                          UndoStep(runner, "Remove " + info.DisplayName));
                }
            });

            RenderScripts(runner, scene, entity, guid, choices);
            RenderAddComponent(runner, scene, entity, guid);
            ImGui::Spacing();
            if (ImGui::Button("Delete Entity", ImVec2(-1, 0)))
            {
                DeleteEntities(runner, CollectEntries(scene), {guid});
            }
        }

        // Component graphs of the scene by name; refreshed a couple of times a second because the graphs live in YAML.
        const std::map<std::string, ScriptDescriptor>& ComponentGraphs(const Scene& scene)
        {
            const double now = ImGui::GetTime();
            if (now - m_ComponentGraphsStamp < 0.5) return m_ComponentGraphs;
            m_ComponentGraphsStamp = now;
            m_ComponentGraphs.clear();
            if (const auto* node = scene.FindExtension(kSceneGraphsKey))
            {
                for (const auto& item : SceneGraphsFromNode(*node).Items)
                {
                    if (item.IsComponent && !item.IsFunction) m_ComponentGraphs[item.Name] = DescribeComponentGraph(item);
                }
            }
            return m_ComponentGraphs;
        }

        static void EditScript(SimulationRunner& runner, const Scene& scene, std::uint64_t guid, const std::function<void(ScriptsComponent&)>& edit)
        {
            const auto entity = scene.FindByGuid(guid);
            if (!scene.IsValid(entity)) return;
            const auto* current = scene.Registry().try_get<ScriptsComponent>(entity);
            ScriptsComponent edited = current ? *current : ScriptsComponent{};
            edit(edited);
            runner.EnqueueCommand(MakeSetComponentCommand(guid, kScriptsComponentName, edited),
                                  UndoStep(runner, "Edit Scripts", std::format("{}:Scripts", guid)));
        }

        void RenderScripts(SimulationRunner& runner, const Scene& scene, entt::entity entity, std::uint64_t guid,
                           const std::vector<EntityChoice>& choices)
        {
            ImFont* icons = m_UiState->IconFont;
            if (!m_Catalog && m_Assets) m_Catalog = std::make_unique<ScriptCatalog>(*m_Assets);
            const auto& graphs = ComponentGraphs(scene);
            const auto* scripts = scene.Registry().try_get<ScriptsComponent>(entity);

            if (scripts)
            {
                bool removeAll = false;
                if (BeginSection(icons, "Scripts", true, &removeAll))
                {
                    std::optional<std::size_t> removeEntry;
                    for (std::size_t i = 0; i < scripts->Entries.size(); ++i)
                    {
                        const auto& entry = scripts->Entries[i];
                        ImGui::PushID(static_cast<int>(i));
                        std::shared_ptr<const ScriptDescriptor> external;
                        const ScriptDescriptor* descriptor = nullptr;
                        if (const auto graph = graphs.find(entry.Name); graph != graphs.end()) descriptor = &graph->second;
                        else if (m_Catalog)
                        {
                            external = m_Catalog->Describe(entry.Name);
                            descriptor = external.get();
                        }
                        const std::string error = m_UiState->ScriptStatuses.Error(guid, i);
                        const bool broken = !error.empty() || (descriptor && !descriptor->Ok());

                        ImGui::TextColored(broken ? ImVec4(0.95f, 0.35f, 0.3f, 1.0f) : ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s%s",
                                           broken ? "(!) " : "", entry.Name.c_str());
                        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 14.0f);
                        if (ImGui::SmallButton("x")) removeEntry = i;
                        Tooltip("Remove this script");
                        if (descriptor && !descriptor->Ok())
                        {
                            ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.3f, 1.0f), "%s", descriptor->Error.c_str());
                        }
                        if (!error.empty()) ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.3f, 1.0f), "%s", error.c_str());

                        if (descriptor)
                        {
                            for (const auto& property : descriptor->Properties)
                            {
                                FieldInfo field{.Name = property.Name, .Type = ToFieldType(property.Type), .Meta = PropertyMeta(property)};
                                field.Set = [](entt::registry&, entt::entity, const FieldValue&) { return true; };
                                FieldValue current = property.Default;
                                if (const auto stored = entry.Props.find(property.Name); stored != entry.Props.end())
                                {
                                    auto coerced = CoerceProperty(stored->second, property.Type);
                                    if (!std::holds_alternative<std::monostate>(coerced)) current = std::move(coerced);
                                }
                                ImGui::PushID(property.Name.c_str());
                                if (auto edited = DrawField(icons, field, current, &property.Default, choices))
                                {
                                    EditScript(runner, scene, guid, [i, name = property.Name, value = std::move(*edited),
                                                              fallback = property.Default](ScriptsComponent& component)
                                    {
                                        if (i >= component.Entries.size()) return;
                                        if (value == fallback) component.Entries[i].Props.erase(name);
                                        else component.Entries[i].Props[name] = value;
                                    });
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::Spacing();
                        ImGui::PopID();
                    }
                    if (removeEntry)
                    {
                        EditScript(runner, scene, guid, [index = *removeEntry](ScriptsComponent& component)
                        {
                            if (index < component.Entries.size()) component.Entries.erase(component.Entries.begin() + static_cast<std::ptrdiff_t>(index));
                        });
                    }
                    EndSection();
                }
                if (removeAll)
                {
                    runner.EnqueueCommand(RemoveComponentCommand{guid, kScriptsComponentName}, UndoStep(runner, "Remove Scripts"));
                }
            }

            if (ImGui::Button("Add Script", ImVec2(-1, 0))) ImGui::OpenPopup("AddScript");
            if (!ImGui::BeginPopup("AddScript")) return;
            std::vector<std::string> names = m_Assets ? ListScripts(*m_Assets) : std::vector<std::string>{};
            for (const auto& [name, descriptor] : graphs) names.push_back(name);
            if (names.empty()) ImGui::TextDisabled("No scripts in the Scripts directories and no component graphs");
            for (const auto& name : names)
            {
                if (!ImGui::MenuItem(name.c_str())) continue;
                EditScript(runner, scene, guid, [name](ScriptsComponent& component) { component.Entries.push_back(ScriptEntry{name, {}}); });
            }
            ImGui::EndPopup();
        }

        void DrawComponentFields(SimulationRunner& runner, const Scene& scene, entt::entity entity,
                                 std::uint64_t guid, const ComponentTypeInfo& info,
                                 const std::vector<EntityChoice>& choices)
        {
            ImFont* icons = m_UiState->IconFont;
            if (!info.Note.empty()) ImGui::TextDisabled("%s", info.Note.c_str());
            const auto& defaults = DefaultsFor(m_Defaults, info);
            const AssetPickerHost pickerHost{.Catalog = &m_AssetCatalog,
                                             .ConfigFor = [this](const AssetFieldKind kind) { return AssetConfig(kind); },
                                             .Recents = m_AppState->GetRecentAssets(),
                                             .Remember = [this](const std::string& path) { m_AppState->PushRecentAsset(path); }};

            std::vector<std::string> categories{""};
            for (const auto& field : info.Fields)
            {
                if (!std::ranges::contains(categories, field.Meta.Category)) categories.push_back(field.Meta.Category);
            }
            for (const auto& category : categories)
            {
                bool headerShown = category.empty();
                for (const auto& field : info.Fields)
                {
                    if (field.Meta.Category != category) continue;
                    if (!FieldVisible(field, info, scene.Registry(), entity)) continue;
                    const auto current = field.Get(scene.Registry(), entity);
                    if (std::holds_alternative<std::monostate>(current)) continue;
                    if (!headerShown)
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("%s", category.c_str());
                        ImGui::Separator();
                        headerShown = true;
                    }
                    const auto def = defaults.find(field.Name);
                    ImGui::PushID(field.Name.c_str());
                    if (auto edited = DrawField(icons, field, current,
                                                def != defaults.end() ? &def->second : nullptr, choices,
                                                [this](const FieldInfo& picked) { return BrowseAsset(picked); },
                                                &m_AssetPicker, &pickerHost))
                    {
                        const auto* metadata = scene.Registry().try_get<MetadataComponent>(entity);
                        EditField(runner, guid, info.Name, field.Name, std::move(*edited),
                                  SetFieldLabel(field.Meta.Label.empty() ? field.Name : field.Meta.Label,
                                                metadata ? metadata->Name : std::string_view{}));
                    }
                    if (m_ImportNote.first == field.Name && !m_ImportNote.second.empty())
                    {
                        ImGui::TextWrapped("%s", m_ImportNote.second.c_str());
                    }
                    ImGui::PopID();
                }
            }
        }

        [[nodiscard]] AssetScanConfig AssetConfig(const AssetFieldKind kind) const
        {
            AssetScanConfig config;
            config.ProjectRoot = m_FileSystem->GetWorkingDirectory();
            if (kind == AssetFieldKind::Script)
            {
                config.Roots.push_back(m_FileSystem->ResolvePath("scripts"));
                return config;
            }
            config.Roots.push_back(m_FileSystem->ResolvePath("assets"));
            if (m_AppPaths->IsInstalled())
            {
                config.Roots.push_back(m_AppPaths->UserSubdir(kind == AssetFieldKind::Mesh ? "models" : "textures"));
            }
            if (kind == AssetFieldKind::Mesh)
            {
                for (const auto& directory : m_AppState->GetMeshScanDirectories()) config.Roots.emplace_back(directory);
            }
            return config;
        }

        // AssetPath fields: textures are imported into the project, other assets are referenced where they are
        std::optional<std::string> BrowseAsset(const FieldInfo& field)
        {
            m_ImportNote = {};
            const auto path = m_FileDialog->OpenFile({FileDialogFilter{"Image", "png,jpg,jpeg,tga,bmp,psd,gif,hdr"},
                                                      FileDialogFilter{"Any file", "*"}});
            if (!path) return std::nullopt;
            const auto root = m_FileSystem->GetWorkingDirectory();
            if (!IsImportableTexture(*path))
            {
                std::error_code ec;
                const auto relative = std::filesystem::relative(*path, root, ec);
                return (!ec && !relative.empty() && *relative.begin() != "..") ? relative.generic_string() : path->string();
            }
            auto result = ImportTexture(*path, root);
            std::string note = result.Error;
            if (result.Copied) note = "Imported into " + result.AssetPath;
            for (const auto& warning : result.Warnings) note += (note.empty() ? "" : "\n") + warning;
            m_ImportNote = {field.Name, note};
            if (!result.Error.empty()) m_Logger->Warn("Texture import: {}", result.Error);
            if (result.AssetPath.empty()) return std::nullopt;
            return result.AssetPath;
        }

        AssetCatalog m_AssetCatalog;
        AssetPicker m_AssetPicker;
        std::pair<std::string, std::string> m_ImportNote; // field name, message shown under it
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<FileDialog> m_FileDialog;
        std::shared_ptr<AssetDirectories> m_Assets;
        std::shared_ptr<AppPaths> m_AppPaths;
        std::unique_ptr<ScriptCatalog> m_Catalog;
        std::map<std::string, ScriptDescriptor> m_ComponentGraphs;
        double m_ComponentGraphsStamp = -1.0;

        std::array<char, 512> m_MeshPathBuffer{};
        bool m_MeshAddCollider = true;

        std::string m_Filter;
        std::string m_AddFilter;
        std::unordered_set<std::uint64_t> m_Expanded;
        std::unordered_set<std::uint64_t> m_Selection;
        std::uint64_t m_Anchor = 0;
        std::uint64_t m_RenameGuid = 0;
        bool m_RenameFocus = false;
        std::array<char, 256> m_RenameBuffer{};
        DefaultsMap m_Defaults;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SceneWindowLayer)
