#include <imgui.h>

import GPP;
import MoleHole;
import glm;
import std;
import node_editor;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace ed = ax::NodeEditor;

namespace
{
    constexpr ImVec4 kEventColor(0.26f, 0.59f, 0.98f, 1.0f);
    constexpr ImVec4 kFunctionColor(0.18f, 0.8f, 0.44f, 1.0f);
    constexpr ImVec4 kVariableColor(0.95f, 0.77f, 0.06f, 1.0f);
    constexpr ImVec4 kConstantColor(0.75f, 0.57f, 0.06f, 1.0f);
    constexpr ImVec4 kDecomposerColor(0.8f, 0.36f, 0.36f, 1.0f);
    constexpr ImVec4 kSetterColor(0.6f, 0.36f, 0.8f, 1.0f);
    constexpr ImVec4 kGetterColor(0.2f, 0.6f, 0.7f, 1.0f);
    constexpr ImVec4 kControlColor(0.7f, 0.3f, 0.9f, 1.0f);
    constexpr ImVec4 kPrintColor(0.2f, 0.7f, 0.9f, 1.0f);
    constexpr ImVec4 kEntityColor(0.9f, 0.5f, 0.2f, 1.0f);
    constexpr ImVec4 kOtherColor(0.5f, 0.5f, 0.5f, 1.0f);

    constexpr ImVec4 kFlowColor(0.8f, 0.8f, 0.8f, 1.0f);
    constexpr ImVec4 kBoolColor(0.36f, 0.8f, 0.36f, 1.0f);
    constexpr ImVec4 kFloatColor(0.8f, 0.36f, 0.8f, 1.0f);
    constexpr ImVec4 kIntColor(0.36f, 0.36f, 0.8f, 1.0f);
    constexpr ImVec4 kVec2Color(0.7f, 0.46f, 0.7f, 1.0f);
    constexpr ImVec4 kVec3Color(0.6f, 0.56f, 0.6f, 1.0f);
    constexpr ImVec4 kVec4Color(0.5f, 0.66f, 0.5f, 1.0f);
    constexpr ImVec4 kStringColor(0.8f, 0.8f, 0.36f, 1.0f);
    constexpr ImVec4 kObjectColor(0.36f, 0.8f, 0.8f, 1.0f);

    constexpr float kHeaderHeight = 28.0f;
    constexpr float kPinSize = 12.0f;
    constexpr float kPinMargin = 8.0f;
    constexpr float kNodeMinWidth = 150.0f;
    constexpr float kNodePadding = 8.0f;
    constexpr ImVec4 kNodeBgColor(0.13f, 0.14f, 0.15f, 1.0f);

    constexpr int kNavigateToContentRetryFrames = 15;

    ImVec4 GetNodeColor(const NodeType type)
    {
        switch (type)
        {
        case NodeType::Event: return kEventColor;
        case NodeType::Function: return kFunctionColor;
        case NodeType::Variable: return kVariableColor;
        case NodeType::Constant: return kConstantColor;
        case NodeType::Decomposer: return kDecomposerColor;
        case NodeType::Setter: return kSetterColor;
        case NodeType::Getter: return kGetterColor;
        case NodeType::Control: return kControlColor;
        case NodeType::Print: return kPrintColor;
        case NodeType::Entity: return kEntityColor;
        }
        return kOtherColor;
    }

    ImVec4 GetPinColor(const PinType type)
    {
        switch (type)
        {
        case PinType::Flow: return kFlowColor;
        case PinType::Bool: return kBoolColor;
        case PinType::Float: return kFloatColor;
        case PinType::Int: return kIntColor;
        case PinType::Vec2: return kVec2Color;
        case PinType::Vec3: return kVec3Color;
        case PinType::Vec4: return kVec4Color;
        case PinType::String: return kStringColor;
        case PinType::Object: return kObjectColor;
        }
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }

    const char* PinTypeLabel(const PinType type)
    {
        switch (type)
        {
        case PinType::Flow: return "Flow";
        case PinType::Bool: return "Bool";
        case PinType::Float: return "Float";
        case PinType::Int: return "Int";
        case PinType::Vec2: return "Vec2";
        case PinType::Vec3: return "Vec3";
        case PinType::Vec4: return "Vec4";
        case PinType::String: return "String";
        case PinType::Object: return "Object";
        }
        return "Unknown";
    }

    void DrawPinIcon(const PinType type, const ImVec4& color)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        constexpr float radius = 6.0f;
        switch (type)
        {
        case PinType::Bool:
            drawList->AddRectFilled(pos, ImVec2(pos.x + radius * 2, pos.y + radius * 2), ImColor(color), 3.0f);
            break;
        case PinType::Int:
            drawList->AddRectFilled(pos, ImVec2(pos.x + radius * 2, pos.y + radius * 2), ImColor(color));
            break;
        case PinType::Float:
        case PinType::Vec2:
        case PinType::Vec3:
        case PinType::Vec4:
            drawList->AddTriangleFilled(ImVec2(pos.x + radius, pos.y), ImVec2(pos.x, pos.y + radius * 2),
                                        ImVec2(pos.x + radius * 2, pos.y + radius * 2), ImColor(color));
            break;
        case PinType::Object:
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImColor(color));
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius * 0.5f,
                                      ImColor(0.0f, 0.0f, 0.0f, 1.0f));
            break;
        case PinType::Flow:
        case PinType::String:
        default:
            drawList->AddCircleFilled(ImVec2(pos.x + radius, pos.y + radius), radius, ImColor(color));
            break;
        }
        ImGui::Dummy(ImVec2(radius * 2, radius * 2));
    }

    // ---- live-scene entity options (for Getter target pickers) ------------------------------------

    struct EntityOption
    {
        std::uint64_t Guid{0};
        std::string Label;
        std::unordered_set<std::string> Components;
    };

    std::vector<EntityOption> CollectEntityOptions(const Scene& scene)
    {
        std::vector<EntityOption> result;
        for (auto [entity, metadata] : scene.Registry().view<const MetadataComponent>().each())
        {
            // Hidden bookkeeping entity -- never a valid Getter target either.
            if (metadata.TypeTag == kAnimationGraphDataTypeTag) continue;

            EntityOption option;
            option.Guid = metadata.Guid;
            option.Label = metadata.Name.empty()
                               ? (metadata.TypeTag + " #" + std::to_string(metadata.Guid))
                               : metadata.Name;
            ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
            {
                if (info.Has(scene.Registry(), entity)) option.Components.insert(info.Name);
            });
            result.push_back(std::move(option));
        }
        return result;
    }

    const EntityOption* FindEntityOption(const std::vector<EntityOption>& entities, const std::uint64_t guid)
    {
        for (const auto& entity : entities) { if (entity.Guid == guid) return &entity; }
        return nullptr;
    }

    bool EntityMatchesCategory(const EntityOption& entity, const Node& node)
    {
        return node.Component.empty() || entity.Components.contains(node.Component);
    }

    struct AnimationGraphWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState>;

        AnimationGraphWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                                  std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState))
        {
        }

        void OnAttach() override
        {
            if (!m_EditorContext) m_EditorContext = ed::CreateEditor();
        }

        void OnDetach() override
        {
            if (m_EditorContext)
            {
                ed::DestroyEditor(m_EditorContext);
                m_EditorContext = nullptr;
            }
        }

        void OnUpdate(float deltaTime) override
        {
            if (m_UiState->CurrentSceneName != m_LastSceneName)
            {
                m_LastSceneName = m_UiState->CurrentSceneName;
                m_Executor.reset();
                m_WasPaused = true;
                LoadGraphFromScene();
            }

            if (m_UiState->CurrentSceneName.empty()) return;
            const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName);
            if (!runner) return;

            const bool paused = runner->IsPaused();

            if (m_WasPaused && !paused)
            {
                RebuildExecutor();
                auto sceneLock = runner->LockRenderScene();
                ApplyWrites(*runner, m_Executor->ExecuteStartEvent(*sceneLock));
            }
            else if (!m_WasPaused && paused)
            {
                m_Executor.reset();
            }

            if (!paused && m_Executor)
            {
                auto sceneLock = runner->LockRenderScene();
                ApplyWrites(*runner, m_Executor->ExecuteTickEvent(*sceneLock, deltaTime));
            }

            m_WasPaused = paused;
        }

        void OnUiRender() override
        {
            if (!m_UiState->ShowAnimationGraphWindow) return;

            ImGui::SetNextWindowSize(ImVec2(960, 640), ImGuiCond_FirstUseEver);
            if (!ImGui::Begin("Animation Graph", &m_UiState->ShowAnimationGraphWindow))
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

            std::vector<EntityOption> entities;
            {
                auto sceneLock = runner->LockRenderScene();
                entities = CollectEntityOptions(*sceneLock);
            }

            SyncSelectedNodeFromEditor();

            RenderGraphToolbar(entities);
            ImGui::Separator();

            constexpr float inspectorWidth = 280.0f;
            const ImVec2 avail = ImGui::GetContentRegionAvail();

            ImGui::BeginChild("AnimationGraphEditorPanel", ImVec2(avail.x - inspectorWidth - 8.0f, 0), true);
            if (m_Current < m_Graphs.Items.size()) RenderGraphEditor(entities);
            else ImGui::TextDisabled("No graph in this scene. Click New to create one.");
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("AnimationGraphInspectorPanel", ImVec2(inspectorWidth, 0), true);
            ImGui::TextUnformatted("Node Inspector");
            ImGui::Separator();
            ImGui::Spacing();
            RenderInspector(entities);
            ImGui::EndChild();

            ImGui::End();

            if (m_Dirty)
            {
                if (m_Current < m_Graphs.Items.size()) History().Record(CurrentGraph(), m_DirtyKey);
                SaveGraphToScene(*runner);
                m_Dirty = false;
                m_DirtyKey.clear();
            }
            if (m_Current < m_Graphs.Items.size() && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
                !ImGui::IsAnyItemActive())
            {
                History().Seal();
            }
        }

    private:
        void LoadGraphFromScene()
        {
            m_Graphs = SceneGraphs{};
            m_Histories.clear();
            m_Current = 0;
            if (!m_UiState->CurrentSceneName.empty())
            {
                if (const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName))
                {
                    auto sceneLock = runner->LockRenderScene();
                    m_Graphs = LoadSceneGraphs(*sceneLock);
                }
            }
            ResetEditorView();
        }

        void MarkDirty(const std::string& key = {})
        {
            if (m_Dirty && key != m_DirtyKey) m_DirtyKey.clear();
            else m_DirtyKey = key;
            m_Dirty = true;
        }

        GraphHistory& History()
        {
            m_Histories.resize(std::max(m_Histories.size(), m_Graphs.Items.size()));
            auto& history = m_Histories[m_Current];
            if (history.Size() == 0) history.Reset(CurrentGraph());
            return history;
        }

        void ApplyHistory(const bool undo)
        {
            if (m_Current >= m_Graphs.Items.size()) return;
            auto& history = History();
            if (!(undo ? history.Undo(CurrentGraph()) : history.Redo(CurrentGraph()))) return;
            m_SelectedNodeId = 0;
            m_RestorePositions = true;
            StructureChanged();
        }

        void ResetEditorView()
        {
            m_Dirty = false;
            m_DirtyKey.clear();
            m_SelectedNodeId = 0;
            m_NeedsPositionRestore = true;
            m_PendingNavigateFrames = 0;
        }

        void SaveGraphToScene(SimulationRunner& runner) const
        {
            runner.EnqueueEdit([graphs = m_Graphs](Scene& scene) { StoreSceneGraphs(scene, graphs); });
        }

        void RebuildExecutor()
        {
            m_Executor = std::make_unique<GraphSetExecutor>(m_Graphs, [logger = m_Logger](std::string message)
            {
                logger->Info("[AnimationGraph] {}", message);
            });
        }

        AnimationGraphData& CurrentGraph()
        {
            return m_Current < m_Graphs.Items.size() ? m_Graphs.Items[m_Current].Graph : m_EmptyGraph;
        }

        const AnimationGraphData& CurrentGraph() const
        {
            return m_Current < m_Graphs.Items.size() ? m_Graphs.Items[m_Current].Graph : m_EmptyGraph;
        }

        void SelectGraph(const std::size_t index)
        {
            m_Current = index;
            ResetEditorView();
        }

        void StructureChanged()
        {
            if (m_Executor) RebuildExecutor();
            MarkDirty();
        }

        void RenderGraphToolbar(const std::vector<EntityOption>& entities)
        {
            const bool hasGraph = m_Current < m_Graphs.Items.size();
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::BeginCombo("##graphselect", hasGraph ? m_Graphs.Items[m_Current].Name.c_str() : "(No Graph)"))
            {
                for (std::size_t i = 0; i < m_Graphs.Items.size(); ++i)
                {
                    if (ImGui::Selectable(m_Graphs.Items[i].Name.c_str(), i == m_Current)) SelectGraph(i);
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("New"))
            {
                m_Graphs.Items.push_back(NamedGraph{.Name = m_Graphs.UniqueName("Graph")});
                SelectGraph(m_Graphs.Items.size() - 1);
                StructureChanged();
            }
            ImGui::SameLine();
            if (ImGui::Button("Rename") && hasGraph)
            {
                std::ranges::fill(m_RenameBuffer, '\0');
                std::ranges::copy_n(m_Graphs.Items[m_Current].Name.begin(),
                                    std::min(m_Graphs.Items[m_Current].Name.size(), m_RenameBuffer.size() - 1),
                                    m_RenameBuffer.begin());
                ImGui::OpenPopup("RenameAnimationGraph");
            }
            ImGui::SameLine();
            if (ImGui::Button("Delete") && hasGraph)
            {
                m_Graphs.Items.erase(m_Graphs.Items.begin() + static_cast<std::ptrdiff_t>(m_Current));
                if (m_Current < m_Histories.size()) m_Histories.erase(m_Histories.begin() + static_cast<std::ptrdiff_t>(m_Current));
                SelectGraph(m_Current > 0 ? m_Current - 1 : 0);
                StructureChanged();
            }

            if (ImGui::BeginPopupModal("RenameAnimationGraph", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                const bool enter = ImGui::InputText("##renamegraph", m_RenameBuffer.data(), m_RenameBuffer.size(),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
                if ((ImGui::Button("OK") || enter) && m_RenameBuffer[0] != '\0' && m_Current < m_Graphs.Items.size())
                {
                    const std::string name = m_RenameBuffer.data();
                    if (name != m_Graphs.Items[m_Current].Name)
                    {
                        m_Graphs.Items[m_Current].Name.clear();
                        m_Graphs.Items[m_Current].Name = m_Graphs.UniqueName(name);
                        MarkDirty();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            if (!hasGraph) return;
            auto& named = m_Graphs.Items[m_Current];
            ImGui::SameLine();
            ImGui::BeginDisabled(!History().CanUndo());
            if (ImGui::Button("Undo")) ApplyHistory(true);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!History().CanRedo());
            if (ImGui::Button("Redo")) ApplyHistory(false);
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Checkbox("Enabled", &named.Enabled)) StructureChanged();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(180.0f);
            const EntityOption* bound = named.EntityGuid != 0 ? FindEntityOption(entities, named.EntityGuid) : nullptr;
            if (ImGui::BeginCombo("##graphentity", bound ? bound->Label.c_str()
                                                       : (named.EntityGuid != 0 ? "(Missing Entity)" : "(Scene)")))
            {
                if (ImGui::Selectable("(Scene)", named.EntityGuid == 0))
                {
                    named.EntityGuid = 0;
                    StructureChanged();
                }
                for (const auto& entity : entities)
                {
                    if (ImGui::Selectable(entity.Label.c_str(), entity.Guid == named.EntityGuid))
                    {
                        named.EntityGuid = entity.Guid;
                        StructureChanged();
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Entity this graph is bound to; unset Getter nodes target it");
        }

        // ---- live execution -------------------------------------------------------------------

        void ApplyWrites(SimulationRunner& runner, PendingWrites writes)
        {
            if (writes.empty()) return;
            // TODO
            runner.EnqueueEdit([writes = std::move(writes)](Scene& scene)
            {
                for (const auto& write : writes) { write(scene); }
            });
        }

        // ---- node-editor canvas ----------------------------------------------------------------

        void SyncSelectedNodeFromEditor()
        {
            if (!m_EditorContext) return;
            ed::SetCurrentEditor(m_EditorContext);
            ed::NodeId selected[1];
            const int count = ed::GetSelectedNodes(selected, 1);
            m_SelectedNodeId = count > 0 ? static_cast<int>(selected[0].Get()) : 0;
            ed::SetCurrentEditor(nullptr);
        }

        void RenderGraphEditor(const std::vector<EntityOption>& entities)
        {
            if (!m_EditorContext) m_EditorContext = ed::CreateEditor();
            ed::SetCurrentEditor(m_EditorContext);
            m_EditorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            ed::Begin("AnimationGraphCanvas");

            if (m_NeedsPositionRestore || m_RestorePositions)
            {
                m_RestorePositions = false;
                for (const auto& node : CurrentGraph().Nodes)
                {
                    ed::SetNodePosition(ed::NodeId(node.Id), ImVec2(node.Position.x, node.Position.y));
                }
            }

            for (auto& node : CurrentGraph().Nodes)
            {
                DrawNode(node, entities);
            }

            for (const auto& link : CurrentGraph().Links)
            {
                ed::Link(ed::LinkId(link.Id), ed::PinId(link.StartPinId), ed::PinId(link.EndPinId));
            }

            HandleLinkCreation();
            HandleDeletion();
            HandleShortcuts();

            ed::Suspend();
            if (ed::ShowBackgroundContextMenu()) OpenPalette(std::nullopt, 0);
            RenderPalette();
            ed::Resume();

            ed::End();

            if (m_NeedsPositionRestore)
            {
                m_NeedsPositionRestore = false;
                m_PendingNavigateFrames = CurrentGraph().Nodes.empty() ? 0 : kNavigateToContentRetryFrames;
            }
            else if (m_PendingNavigateFrames > 0)
            {
                ed::NavigateToContent();
                --m_PendingNavigateFrames;
            }
            else
            {
                for (auto& node : CurrentGraph().Nodes)
                {
                    const auto pos = ed::GetNodePosition(ed::NodeId(node.Id));
                    if (std::abs(pos.x - node.Position.x) > 0.01f || std::abs(pos.y - node.Position.y) > 0.01f)
                    {
                        node.Position = glm::vec2(pos.x, pos.y);
                        MarkDirty("move");
                    }
                }
            }

            ed::SetCurrentEditor(nullptr);
        }

        void HandleLinkCreation()
        {
            if (ed::BeginCreate())
            {
                ed::PinId startPinId, endPinId;
                ed::PinId droppedPinId;
                if (ed::QueryNewLink(&startPinId, &endPinId))
                {
                    if (startPinId && endPinId && startPinId != endPinId)
                    {
                        int startId = static_cast<int>(startPinId.Get());
                        int endId = static_cast<int>(endPinId.Get());
                        const Pin* startPin = FindPin(CurrentGraph(), startId);
                        if (startPin && startPin->IsInput) std::swap(startId, endId);

                        if (CanLink(CurrentGraph(), startId, endId))
                        {
                            if (ed::AcceptNewItem())
                            {
                                TryLink(CurrentGraph(), startId, endId);
                                StructureChanged();
                            }
                        }
                        else
                        {
                            ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                        }
                    }
                }
                else if (ed::QueryNewNode(&droppedPinId))
                {
                    if (const Pin* pin = FindPin(CurrentGraph(), static_cast<int>(droppedPinId.Get())); pin && ed::AcceptNewItem())
                    {
                        OpenPalette(PinFilter{pin->Type, !pin->IsInput}, pin->Id);
                    }
                }
            }
            ed::EndCreate();
        }

        std::vector<int> SelectedNodeIds() const
        {
            std::vector<ed::NodeId> selected(CurrentGraph().Nodes.size() + 1);
            const int count = ed::GetSelectedNodes(selected.data(), static_cast<int>(selected.size()));
            std::vector<int> ids;
            for (int i = 0; i < count; ++i) ids.push_back(static_cast<int>(selected[i].Get()));
            return ids;
        }

        void SelectNodes(const std::vector<int>& ids)
        {
            ed::ClearSelection();
            for (const int id : ids) ed::SelectNode(ed::NodeId(id), true);
            m_SelectedNodeId = ids.empty() ? 0 : ids.front();
        }

        void PasteText(const std::string& text, const glm::vec2 anchor)
        {
            const auto result = PasteGraphSelection(CurrentGraph(), text, anchor);
            if (result.NodeIds.empty()) return;
            for (const int id : result.NodeIds)
            {
                const auto& pos = CurrentGraph().FindNode(id)->Position;
                ed::SetNodePosition(ed::NodeId(id), ImVec2(pos.x, pos.y));
            }
            SelectNodes(result.NodeIds);
            StructureChanged();
        }

        void HandleShortcuts()
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (!m_EditorFocused || io.WantTextInput || !io.KeyCtrl) return;

            const auto selected = SelectedNodeIds();
            const auto copyText = [&] { return CopyGraphSelection(CurrentGraph(), selected); };

            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) ApplyHistory(!io.KeyShift);
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) ApplyHistory(false);
            else if (ImGui::IsKeyPressed(ImGuiKey_A, false))
            {
                std::vector<int> all;
                for (const auto& node : CurrentGraph().Nodes) all.push_back(node.Id);
                SelectNodes(all);
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_C, false) && !selected.empty())
            {
                ImGui::SetClipboardText(copyText().c_str());
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_X, false) && !selected.empty())
            {
                ImGui::SetClipboardText(copyText().c_str());
                for (const int id : selected) ed::DeleteNode(ed::NodeId(id));
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            {
                if (const char* clip = ImGui::GetClipboardText(); clip && IsGraphClipboardText(clip))
                {
                    const auto canvas = ed::ScreenToCanvas(io.MousePos);
                    PasteText(clip, glm::vec2(canvas.x, canvas.y));
                }
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_D, false) && !selected.empty())
            {
                glm::vec2 minPos{std::numeric_limits<float>::max()};
                for (const int id : selected) minPos = glm::min(minPos, CurrentGraph().FindNode(id)->Position);
                PasteText(copyText(), minPos + glm::vec2(40.0f, 40.0f));
            }
        }

        void HandleDeletion()
        {
            if (ed::BeginDelete())
            {
                ed::LinkId deletedLinkId;
                while (ed::QueryDeletedLink(&deletedLinkId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        CurrentGraph().RemoveLink(static_cast<int>(deletedLinkId.Get()));
                        MarkDirty();
                    }
                }

                ed::NodeId deletedNodeId;
                while (ed::QueryDeletedNode(&deletedNodeId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        const int id = static_cast<int>(deletedNodeId.Get());
                        CurrentGraph().RemoveNode(id);
                        if (m_SelectedNodeId == id) m_SelectedNodeId = 0;
                        MarkDirty();
                    }
                }
            }
            ed::EndDelete();
        }

        const NodeRegistry& Registry()
        {
            if (!m_Registry) m_Registry = std::make_unique<NodeRegistry>(BuildNodeRegistry());
            return *m_Registry;
        }

        void OpenPalette(const std::optional<PinFilter> filter, const int draggedPin)
        {
            m_PaletteFilter = filter;
            m_PaletteDraggedPin = draggedPin;
            const ImVec2 canvas = ed::ScreenToCanvas(ImGui::GetMousePos());
            m_PaletteCanvasPos = glm::vec2(canvas.x, canvas.y);
            m_OpenPalette = true;
        }

        void SpawnEntry(const NodeEntry& entry)
        {
            auto& graph = CurrentGraph();
            const auto ids = entry.Spawn(graph);
            if (ids.empty()) return;
            for (const int id : ids)
            {
                Node* node = graph.FindNode(id);
                node->Position += m_PaletteCanvasPos;
                ed::SetNodePosition(ed::NodeId(id), ImVec2(node->Position.x, node->Position.y));
            }
            if (m_PaletteFilter && m_PaletteDraggedPin != 0) ConnectNewNode(graph, ids.front(), m_PaletteDraggedPin);
            SelectNodes(ids);
            m_Recents = PushRecent(m_Recents, entry.Name);
            StructureChanged();
        }

        void RenderPalette()
        {
            if (m_OpenPalette)
            {
                ImGui::OpenPopup("AnimationGraphPalette");
                m_PaletteQuery.fill('\0');
                m_PaletteSelected = 0;
                m_PaletteFocus = true;
                m_OpenPalette = false;
            }

            ImGui::SetNextWindowSize(ImVec2(340, 400));
            if (!ImGui::BeginPopup("AnimationGraphPalette")) return;

            if (m_PaletteFocus)
            {
                ImGui::SetKeyboardFocusHere();
                m_PaletteFocus = false;
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("##palettequery", m_PaletteFilter ? "Search compatible nodes..." : "Search nodes...",
                                         m_PaletteQuery.data(), m_PaletteQuery.size()))
            {
                m_PaletteSelected = 0;
            }

            const auto extra = VariableNodeEntries(CurrentGraph());
            const std::string query = m_PaletteQuery.data();
            const auto results = Registry().Search(query, m_PaletteFilter, m_Recents, extra);
            const int count = static_cast<int>(results.size());

            bool moved = false;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                m_PaletteSelected = MoveSelection(m_PaletteSelected, 1, count);
                moved = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                m_PaletteSelected = MoveSelection(m_PaletteSelected, -1, count);
                moved = true;
            }
            m_PaletteSelected = std::min(m_PaletteSelected, std::max(count - 1, 0));

            const NodeEntry* chosen = nullptr;
            if ((ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) && count > 0)
            {
                chosen = results[m_PaletteSelected].Entry;
            }

            ImGui::Separator();
            ImGui::BeginChild("##paletteresults", ImVec2(0, 0));
            std::string lastHeader;
            for (int i = 0; i < count; ++i)
            {
                const NodeEntry& entry = *results[i].Entry;
                if (query.empty())
                {
                    const std::string header = results[i].Recent ? "Recent" : entry.Category;
                    if (header != lastHeader)
                    {
                        ImGui::TextDisabled("%s", header.c_str());
                        lastHeader = header;
                    }
                }
                ImGui::PushID(i);
                if (ImGui::Selectable(entry.Name.c_str(), i == m_PaletteSelected)) chosen = &entry;
                if (!query.empty())
                {
                    ImGui::SameLine();
                    const float width = ImGui::CalcTextSize(entry.Category.c_str()).x;
                    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - width);
                    ImGui::TextDisabled("%s", entry.Category.c_str());
                }
                if (ImGui::IsItemHovered() && !entry.Description.empty()) ImGui::SetTooltip("%s", entry.Description.c_str());
                if (moved && i == m_PaletteSelected) ImGui::SetScrollHereY();
                ImGui::PopID();
            }
            if (count == 0) ImGui::TextDisabled("No matching nodes");
            ImGui::EndChild();

            if (chosen)
            {
                SpawnEntry(*chosen);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // ---- per-node drawing (ported from legacy's NodeBuilder) -------------------------------

        static float CalculateNodeWidth(const Node& node)
        {
            float maxWidth = kNodeMinWidth;
            const float headerWidth = ImGui::CalcTextSize(node.Name.c_str()).x + 40.0f;
            maxWidth = std::max(maxWidth, headerWidth);
            for (const auto& pin : node.Inputs)
            {
                const float pinWidth = ImGui::CalcTextSize(pin.Name.c_str()).x + kPinSize + kPinMargin * 2;
                maxWidth = std::max(maxWidth, pinWidth * 1.5f);
            }
            for (const auto& pin : node.Outputs)
            {
                const float pinWidth = ImGui::CalcTextSize(pin.Name.c_str()).x + kPinSize + kPinMargin * 2;
                maxWidth = std::max(maxWidth, pinWidth * 1.5f);
            }
            return maxWidth;
        }

        static void DrawNodeIcon(const NodeType type)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            constexpr float iconSize = 16.0f;
            constexpr ImVec4 iconColor(1.0f, 1.0f, 1.0f, 0.9f);
            switch (type)
            {
            case NodeType::Event:
                drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize, pos.y + iconSize), ImColor(iconColor), 2.0f);
                break;
            case NodeType::Function:
                drawList->AddCircleFilled(ImVec2(pos.x + iconSize / 2, pos.y + iconSize / 2), iconSize / 2,
                                          ImColor(iconColor));
                break;
            case NodeType::Variable:
                drawList->AddTriangleFilled(ImVec2(pos.x + iconSize / 2, pos.y), ImVec2(pos.x, pos.y + iconSize),
                                            ImVec2(pos.x + iconSize, pos.y + iconSize), ImColor(iconColor));
                break;
            case NodeType::Constant:
                drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize, pos.y + iconSize), ImColor(iconColor));
                break;
            case NodeType::Decomposer:
            case NodeType::Getter:
                drawList->AddCircleFilled(ImVec2(pos.x + iconSize / 2, pos.y + iconSize / 2), iconSize / 3,
                                          ImColor(iconColor));
                break;
            case NodeType::Setter:
                drawList->AddTriangleFilled(ImVec2(pos.x, pos.y + iconSize / 2), ImVec2(pos.x + iconSize, pos.y),
                                            ImVec2(pos.x + iconSize, pos.y + iconSize), ImColor(iconColor));
                break;
            case NodeType::Control:
                drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize, pos.y + iconSize), ImColor(iconColor),
                                        iconSize / 4);
                break;
            case NodeType::Entity:
                drawList->AddRectFilled(pos, ImVec2(pos.x + iconSize, pos.y + iconSize), ImColor(iconColor), iconSize / 2);
                break;
            case NodeType::Print:
                drawList->AddRect(pos, ImVec2(pos.x + iconSize, pos.y + iconSize), ImColor(iconColor), 2.0f, 0, 2.0f);
                break;
            }
            ImGui::Dummy(ImVec2(iconSize, iconSize));
        }

        static void DrawHeader(const Node& node, const float nodeWidth)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 headerStart = ImGui::GetCursorScreenPos();
            const ImVec2 headerEnd(headerStart.x + nodeWidth, headerStart.y + kHeaderHeight);

            drawList->AddRectFilled(headerStart, headerEnd, ImColor(GetNodeColor(node.Type)), 4.0f,
                                    ImDrawFlags_RoundCornersTop);

            ImGui::SetCursorScreenPos(ImVec2(headerStart.x + kNodePadding, headerStart.y + (kHeaderHeight - 16.0f) * 0.5f));
            DrawNodeIcon(node.Type);
            ImGui::SameLine(0, 4.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (16.0f - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", node.Name.c_str());

            ImGui::SetCursorScreenPos(ImVec2(headerStart.x, headerEnd.y));
            ImGui::Dummy(ImVec2(nodeWidth, 0));
        }

        void DrawConstantValueInput(Node& node, const float nodeWidth)
        {
            ImGui::SetNextItemWidth(nodeWidth - kNodePadding * 2);
            const std::string id = "##const_" + std::to_string(node.Id);
            const Value before = node.ConstantValue;

            if (std::holds_alternative<bool>(node.ConstantValue))
            {
                bool value = std::get<bool>(node.ConstantValue);
                if (ImGui::Checkbox(id.c_str(), &value)) node.ConstantValue = value;
            }
            else if (std::holds_alternative<float>(node.ConstantValue))
            {
                float value = std::get<float>(node.ConstantValue);
                if (ImGui::DragFloat(id.c_str(), &value, 0.1f)) node.ConstantValue = value;
            }
            else if (std::holds_alternative<int>(node.ConstantValue))
            {
                int value = std::get<int>(node.ConstantValue);
                if (ImGui::DragInt(id.c_str(), &value)) node.ConstantValue = value;
            }
            else if (std::holds_alternative<glm::vec2>(node.ConstantValue))
            {
                glm::vec2 value = std::get<glm::vec2>(node.ConstantValue);
                if (ImGui::DragFloat2(id.c_str(), &value.x, 0.1f)) node.ConstantValue = value;
            }
            else if (std::holds_alternative<glm::vec3>(node.ConstantValue))
            {
                glm::vec3 value = std::get<glm::vec3>(node.ConstantValue);
                if (ImGui::DragFloat3(id.c_str(), &value.x, 0.1f)) node.ConstantValue = value;
            }
            else if (std::holds_alternative<glm::vec4>(node.ConstantValue))
            {
                glm::vec4 value = std::get<glm::vec4>(node.ConstantValue);
                if (ImGui::DragFloat4(id.c_str(), &value.x, 0.1f)) node.ConstantValue = value;
            }
            else if (std::holds_alternative<std::string>(node.ConstantValue))
            {
                const std::string current = std::get<std::string>(node.ConstantValue);
                std::array<char, 256> buffer{};
                const auto count = std::min(current.size(), buffer.size() - 1);
                std::ranges::copy(current.substr(0, count), buffer.begin());
                if (ImGui::InputText(id.c_str(), buffer.data(), buffer.size()))
                {
                    node.ConstantValue = std::string(buffer.data());
                }
            }
            if (node.ConstantValue != before) MarkDirty("const:" + std::to_string(node.Id));
        }

        void DrawInlineContent(Node& node, const float nodeWidth, const std::vector<EntityOption>& entities)
        {
            if (node.Type == NodeType::Constant)
            {
                DrawConstantValueInput(node, nodeWidth);
            }
            else if (node.Type == NodeType::Variable)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.2f, 1.0f));
                ImGui::TextWrapped("%s", node.VariableName.empty() ? "(No Variable)" : node.VariableName.c_str());
                ImGui::PopStyleColor();
            }
            else if (node.Type == NodeType::Getter)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.7f, 0.9f, 1.0f));
                const EntityOption* target = node.TargetGuid != 0 ? FindEntityOption(entities, node.TargetGuid) : nullptr;
                ImGui::TextWrapped("%s", target ? target->Label.c_str() : "(No Target)");
                ImGui::PopStyleColor();
            }
        }

        void DrawPinsAndContent(Node& node, const float nodeWidth, const std::vector<EntityOption>& entities)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 contentStart = ImGui::GetCursorScreenPos();

            const std::size_t maxPins = std::max(node.Inputs.size(), node.Outputs.size());
            const float contentHeight = maxPins > 0
                                            ? static_cast<float>(maxPins) * (kPinSize + 4.0f) + kNodePadding
                                            : kNodePadding;

            const ImVec2 contentEnd(contentStart.x + nodeWidth, contentStart.y + contentHeight);
            drawList->AddRectFilled(contentStart, contentEnd, ImColor(kNodeBgColor), 4.0f, ImDrawFlags_RoundCornersBottom);

            for (std::size_t i = 0; i < maxPins; ++i)
            {
                const ImVec2 rowStart(contentStart.x,
                                      contentStart.y + kNodePadding / 2 + static_cast<float>(i) * (kPinSize + 4.0f));

                if (i < node.Inputs.size())
                {
                    const auto& pin = node.Inputs[i];
                    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + kNodePadding, rowStart.y));
                    ed::BeginPin(ed::PinId(pin.Id), ed::PinKind::Input);
                    DrawPinIcon(pin.Type, GetPinColor(pin.Type));
                    ImGui::SameLine(0, 4.0f);
                    ImGui::Text("%s", pin.Name.c_str());
                    ed::EndPin();
                }

                if (i < node.Outputs.size())
                {
                    const auto& pin = node.Outputs[i];
                    const float textWidth = ImGui::CalcTextSize(pin.Name.c_str()).x;
                    ImGui::SetCursorScreenPos(ImVec2(contentEnd.x - kNodePadding - textWidth - kPinSize - 4.0f, rowStart.y));
                    ed::BeginPin(ed::PinId(pin.Id), ed::PinKind::Output);
                    ImGui::Text("%s", pin.Name.c_str());
                    ImGui::SameLine(0, 4.0f);
                    DrawPinIcon(pin.Type, GetPinColor(pin.Type));
                    ed::EndPin();
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(contentStart.x + kNodePadding, contentStart.y + contentHeight));
            DrawInlineContent(node, nodeWidth, entities);

            ImGui::SetCursorScreenPos(ImVec2(contentStart.x, contentEnd.y));
            ImGui::Dummy(ImVec2(nodeWidth, 0));
        }

        void DrawNode(Node& node, const std::vector<EntityOption>& entities)
        {
            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
            ed::PushStyleVar(ed::StyleVar_NodeRounding, 4.0f);
            ed::PushStyleVar(ed::StyleVar_NodeBorderWidth, 0.0f);
            ed::PushStyleVar(ed::StyleVar_HoveredNodeBorderWidth, 0.0f);
            ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderWidth, 0.0f);
            ed::PushStyleVar(ed::StyleVar_HoveredNodeBorderOffset, 0.0f);
            ed::PushStyleVar(ed::StyleVar_SelectedNodeBorderOffset, 0.0f);

            ed::BeginNode(ed::NodeId(node.Id));
            const float nodeWidth = CalculateNodeWidth(node);
            DrawHeader(node, nodeWidth);
            DrawPinsAndContent(node, nodeWidth, entities);
            ed::EndNode();

            ed::PopStyleVar(7);
        }

        // ---- inspector panel -------------------------------------------------------------------

        void RenderInspector(const std::vector<EntityOption>& entities)
        {
            if (m_SelectedNodeId == 0)
            {
                ImGui::TextDisabled("Select a node to edit");
                return;
            }

            Node* node = CurrentGraph().FindNode(m_SelectedNodeId);
            if (!node)
            {
                ImGui::TextDisabled("Node not found");
                m_SelectedNodeId = 0;
                return;
            }

            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", node->Name.c_str());
            ImGui::Separator();
            ImGui::Spacing();

            if (node->Type == NodeType::Variable)
            {
                RenderVariableSelector(*node);
            }
            else if (node->Type == NodeType::Getter)
            {
                RenderEntityTargetSelector(*node, entities);
            }
            else if (node->Type == NodeType::Constant)
            {
                ImGui::TextDisabled("Edit value directly on the node.");
            }
            else
            {
                ImGui::TextDisabled("No editable properties.");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            if (ImGui::Button("Delete Node", ImVec2(-1, 0)))
            {
                CurrentGraph().RemoveNode(node->Id);
                m_SelectedNodeId = 0;
                MarkDirty();
            }
        }

        void RenderVariableSelector(Node& node)
        {
            PinType varType = PinType::Float;
            if (!node.Outputs.empty()) varType = node.Outputs[0].Type;
            else if (node.Inputs.size() > 1) varType = node.Inputs[1].Type;

            ImGui::Text("Variable (%s):", PinTypeLabel(varType));
            ImGui::SetNextItemWidth(-1);
            const std::string preview = node.VariableName.empty() ? "(Select Variable)" : node.VariableName;

            if (ImGui::BeginCombo("##animgraphvariable", preview.c_str()))
            {
                if (ImGui::Selectable("+ New Variable...", false))
                {
                    m_ShowNewVariableDialog = true;
                    m_NewVariableBuffer[0] = '\0';
                }

                const bool anyMatching = std::ranges::any_of(CurrentGraph().Variables, [varType](const Variable& v)
                {
                    return v.Type == varType;
                });
                if (anyMatching) ImGui::Separator();

                for (const auto& variable : CurrentGraph().Variables)
                {
                    if (variable.Type != varType) continue;
                    const bool selected = variable.Name == node.VariableName;
                    if (ImGui::Selectable(variable.Name.c_str(), selected))
                    {
                        node.VariableName = variable.Name;
                        MarkDirty();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (m_ShowNewVariableDialog)
            {
                ImGui::OpenPopup("NewAnimationGraphVariable");
                m_ShowNewVariableDialog = false;
            }

            if (ImGui::BeginPopupModal("NewAnimationGraphVariable", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("New %s Variable:", PinTypeLabel(varType));
                if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                const bool enterPressed = ImGui::InputText("##newanimgraphvarname", m_NewVariableBuffer.data(),
                                                            m_NewVariableBuffer.size(),
                                                            ImGuiInputTextFlags_EnterReturnsTrue);
                ImGui::Spacing();
                if ((ImGui::Button("Create") || enterPressed) && m_NewVariableBuffer[0] != '\0')
                {
                    const std::string name = m_NewVariableBuffer.data();
                    const bool exists = std::ranges::any_of(CurrentGraph().Variables, [&](const Variable& v)
                    {
                        return v.Name == name && v.Type == varType;
                    });
                    if (!exists) CurrentGraph().Variables.push_back(Variable{name, varType});
                    node.VariableName = name;
                    MarkDirty();
                    m_NewVariableBuffer[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    m_NewVariableBuffer[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        void RenderEntityTargetSelector(Node& node, const std::vector<EntityOption>& entities)
        {
            ImGui::Text("Target Entity:");
            ImGui::SetNextItemWidth(-1);

            const EntityOption* current = node.TargetGuid != 0 ? FindEntityOption(entities, node.TargetGuid) : nullptr;
            const std::string preview = current ? current->Label
                                                 : (node.TargetGuid != 0 ? "(Missing Entity)" : "(Select Entity)");

            if (ImGui::BeginCombo("##animgraphtarget", preview.c_str()))
            {
                bool any = false;
                for (const auto& entity : entities)
                {
                    if (!EntityMatchesCategory(entity, node)) continue;
                    any = true;
                    const bool selected = entity.Guid == node.TargetGuid;
                    if (ImGui::Selectable(entity.Label.c_str(), selected))
                    {
                        node.TargetGuid = entity.Guid;
                        MarkDirty();
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                if (!any) ImGui::TextDisabled("(no matching entities in this scene)");
                ImGui::EndCombo();
            }
        }

        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;

        ed::EditorContext* m_EditorContext = nullptr;
        SceneGraphs m_Graphs;
        std::size_t m_Current = 0;
        AnimationGraphData m_EmptyGraph;
        std::array<char, 128> m_RenameBuffer{};
        bool m_Dirty = false;
        std::string m_DirtyKey;
        std::vector<GraphHistory> m_Histories;
        bool m_RestorePositions = false;
        bool m_EditorFocused = false;
        std::unique_ptr<NodeRegistry> m_Registry;
        std::vector<std::string> m_Recents;
        std::array<char, 128> m_PaletteQuery{};
        std::optional<PinFilter> m_PaletteFilter;
        int m_PaletteDraggedPin = 0;
        int m_PaletteSelected = 0;
        glm::vec2 m_PaletteCanvasPos{0.0f};
        bool m_OpenPalette = false;
        bool m_PaletteFocus = false;
        bool m_NeedsPositionRestore = true;
        int m_PendingNavigateFrames = 0;
        int m_SelectedNodeId = 0;

        std::string m_LastSceneName;
        bool m_WasPaused = true;
        std::unique_ptr<GraphSetExecutor> m_Executor;

        bool m_ShowNewVariableDialog = false;
        std::array<char, 128> m_NewVariableBuffer{};
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(AnimationGraphWindowLayer)
