#include <imgui.h>

import GPP;
import MoleHole;
import glm;
import std;
import node_editor;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;
using namespace MoleHole::UiDetail;

namespace ed = ax::NodeEditor;

namespace
{
    constexpr float kHeaderHeight = 28.0f;
    constexpr float kPinSize = 12.0f;
    constexpr float kPinMargin = 8.0f;
    constexpr float kNodeMinWidth = 150.0f;
    constexpr float kNodePadding = 8.0f;
    constexpr float kNodeRounding = 6.0f;
    constexpr float kCommentTitleHeight = 26.0f;

    constexpr int kNavigateToContentRetryFrames = 15;

    ImVec4 PinColor(const PinType type) { return ToVec4(GraphPinColor(type)); }

    void DrawPinIcon(const PinType type, const bool connected)
    {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImU32 color = ToU32(GraphPinColor(type));
        const ImU32 fill = ToU32(WithAlpha(GraphPinColor(type), 0.18f));
        constexpr float size = kPinSize;

        if (type == PinType::Flow)
        {
            const ImVec2 a(pos.x + 1.0f, pos.y + 1.0f);
            const ImVec2 b(pos.x + size, pos.y + size * 0.5f);
            const ImVec2 c(pos.x + 1.0f, pos.y + size - 1.0f);
            if (connected) drawList->AddTriangleFilled(a, b, c, color);
            else
            {
                drawList->AddTriangleFilled(a, b, c, fill);
                drawList->AddTriangle(a, b, c, color, 1.8f);
            }
        }
        else
        {
            const ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
            const float radius = size * 0.5f - 1.0f;
            if (connected) drawList->AddCircleFilled(center, radius, color, 16);
            else
            {
                drawList->AddCircleFilled(center, radius, fill, 16);
                drawList->AddCircle(center, radius, color, 16, 1.8f);
            }
        }
        ImGui::Dummy(ImVec2(size, size));
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
            if (!m_EditorContext) m_EditorContext = CreateEditorContext();
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
            if (m_Graphs.Items[m_Current].IsFunction) SyncFunction(m_Graphs, m_Graphs.Items[m_Current].Name);
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
                    const std::string label = (m_Graphs.Items[i].IsFunction ? "fn  " : "") + m_Graphs.Items[i].Name;
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Selectable(label.c_str(), i == m_Current)) SelectGraph(i);
                    ImGui::PopID();
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
            if (ImGui::Button("New Function"))
            {
                SelectGraph(AddFunctionGraph(m_Graphs));
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
                        const std::string oldName = m_Graphs.Items[m_Current].Name;
                        m_Graphs.Items[m_Current].Name.clear();
                        const std::string unique = m_Graphs.UniqueName(name);
                        m_Graphs.Items[m_Current].Name = oldName;
                        if (m_Graphs.Items[m_Current].IsFunction) RenameFunction(m_Graphs, oldName, unique);
                        else m_Graphs.Items[m_Current].Name = unique;
                        StructureChanged();
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
            if (ImGui::Button("Align")) ImGui::OpenPopup("AlignNodes");
            if (ImGui::BeginPopup("AlignNodes"))
            {
                const bool two = m_SelectionCount >= 2;
                const bool three = m_SelectionCount >= 3;
                const auto item = [&](const char* label, const bool enabled, const EditorAction action)
                {
                    if (ImGui::MenuItem(label, nullptr, false, enabled)) m_PendingAction = action;
                };
                item("Align Left", two, EditorAction::AlignLeft);
                item("Align Right", two, EditorAction::AlignRight);
                item("Align Top", two, EditorAction::AlignTop);
                item("Align Bottom", two, EditorAction::AlignBottom);
                item("Center Horizontally", two, EditorAction::AlignCenterH);
                item("Center Vertically", two, EditorAction::AlignCenterV);
                ImGui::Separator();
                item("Distribute Horizontally", three, EditorAction::DistributeH);
                item("Distribute Vertically", three, EditorAction::DistributeV);
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(m_SelectionCount == 0);
            if (ImGui::Button("Comment")) m_PendingAction = EditorAction::CommentSelection;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Wrap the selection in a comment box (C)");
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

        static ed::EditorContext* CreateEditorContext()
        {
            ed::Config config;
            config.EnableSmoothZoom = true;
            config.SmoothZoomPower = 1.15f;
            return ed::CreateEditor(&config);
        }

        void SyncSelectedNodeFromEditor()
        {
            if (!m_EditorContext) return;
            ed::SetCurrentEditor(m_EditorContext);
            ed::NodeId selected[1];
            const int count = ed::GetSelectedNodes(selected, 1);
            m_SelectedNodeId = count > 0 ? static_cast<int>(selected[0].Get()) : 0;
            m_SelectionCount = ed::GetSelectedObjectCount();
            ed::SetCurrentEditor(nullptr);
        }

        void ApplyEditorStyle()
        {
            auto& style = ed::GetStyle();
            style.NodeRounding = kNodeRounding;
            style.GroupRounding = kNodeRounding;
            style.NodeBorderWidth = 1.0f;
            style.HoveredNodeBorderWidth = 2.0f;
            style.SelectedNodeBorderWidth = 2.5f;
            style.PinRounding = 4.0f;
            style.LinkStrength = 140.0f;
            style.Colors[ed::StyleColor_Bg] = ToVec4(Darken(Palette::Panel, 0.35f));
            style.Colors[ed::StyleColor_Grid] = ImVec4(1.0f, 1.0f, 1.0f, 0.045f);
            style.Colors[ed::StyleColor_NodeBg] = ToVec4(WithAlpha(Palette::Panel, 0.96f));
            style.Colors[ed::StyleColor_NodeBorder] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
            style.Colors[ed::StyleColor_HovNodeBorder] = ToVec4(Palette::AccentHover);
            style.Colors[ed::StyleColor_SelNodeBorder] = ToVec4(Palette::Accent);
            style.Colors[ed::StyleColor_Flow] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ed::StyleColor_FlowMarker] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
            style.Colors[ed::StyleColor_GroupBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
            style.Colors[ed::StyleColor_GroupBorder] = ImVec4(1.0f, 1.0f, 1.0f, 0.2f);
        }

        void RebuildLinkedPins()
        {
            m_LinkedPins.clear();
            for (const auto& link : CurrentGraph().Links)
            {
                m_LinkedPins.insert(link.StartPinId);
                m_LinkedPins.insert(link.EndPinId);
            }
        }

        void RestoreEditorPositions()
        {
            for (const auto& node : CurrentGraph().Nodes)
            {
                ed::SetNodePosition(ed::NodeId(node.Id), ImVec2(node.Position.x, node.Position.y));
            }
            for (const auto& comment : CurrentGraph().Comments)
            {
                ed::SetNodePosition(ed::NodeId(comment.Id), ImVec2(comment.Position.x, comment.Position.y));
                ed::SetGroupSize(ed::NodeId(comment.Id), ImVec2(comment.Size.x, comment.Size.y));
            }
        }

        void SyncEditorPositions()
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
            for (auto& comment : CurrentGraph().Comments)
            {
                const auto pos = ed::GetNodePosition(ed::NodeId(comment.Id));
                const auto size = ed::GetNodeSize(ed::NodeId(comment.Id));
                if (std::abs(pos.x - comment.Position.x) > 0.01f || std::abs(pos.y - comment.Position.y) > 0.01f)
                {
                    comment.Position = glm::vec2(pos.x, pos.y);
                    MarkDirty("move");
                }
                if (size.x > 1.0f && (std::abs(size.x - comment.Size.x) > 0.5f || std::abs(size.y - comment.Size.y) > 0.5f))
                {
                    comment.Size = glm::vec2(size.x, size.y);
                    MarkDirty("resize");
                }
            }
        }

        void DrawLinks()
        {
            for (const auto& link : CurrentGraph().Links)
            {
                const Pin* start = FindPin(CurrentGraph(), link.StartPinId);
                const PinType type = start ? start->Type : PinType::Flow;
                ed::Link(ed::LinkId(link.Id), ed::PinId(link.StartPinId), ed::PinId(link.EndPinId), PinColor(type),
                         GraphLinkThickness(type));
            }
        }

        void DrawHoverTooltips()
        {
            if (m_HoveredPinId != 0)
            {
                if (const Pin* pin = FindPin(CurrentGraph(), m_HoveredPinId))
                {
                    ImGui::BeginTooltip();
                    ImGui::TextColored(PinColor(pin->Type), "%s", PinTypeName(pin->Type));
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s%s", pin->IsInput ? "input" : "output",
                                        m_LinkedPins.contains(pin->Id) ? ", connected" : "");
                    if (!pin->Name.empty()) ImGui::TextUnformatted(pin->Name.c_str());
                    ImGui::EndTooltip();
                }
                m_HoverNodeTime = 0.0f;
                return;
            }
            if (m_HoveredNodeId != m_LastHoveredNodeId)
            {
                m_LastHoveredNodeId = m_HoveredNodeId;
                m_HoverNodeTime = 0.0f;
            }
            if (m_HoveredNodeId == 0) return;
            m_HoverNodeTime += ImGui::GetIO().DeltaTime;
            if (m_HoverNodeTime < 0.6f || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
            if (const Node* node = CurrentGraph().FindNode(m_HoveredNodeId))
            {
                const std::string description = Registry().Describe(*node);
                if (description.empty()) return;
                ImGui::BeginTooltip();
                ImGui::TextUnformatted(node->Name.c_str());
                ImGui::Separator();
                ImGui::PushTextWrapPos(280.0f);
                ImGui::TextWrapped("%s", description.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        std::vector<NodeRect> SelectedRects()
        {
            std::vector<NodeRect> rects;
            for (const int id : SelectedNodeIds())
            {
                if (const Node* node = CurrentGraph().FindNode(id))
                {
                    const auto size = ed::GetNodeSize(ed::NodeId(id));
                    rects.push_back({id, node->Position, glm::vec2(size.x, size.y)});
                }
            }
            return rects;
        }

        void MoveRects(const std::vector<NodeRect>& rects)
        {
            for (const auto& rect : rects)
            {
                if (Node* node = CurrentGraph().FindNode(rect.Id))
                {
                    node->Position = rect.Position;
                    ed::SetNodePosition(ed::NodeId(rect.Id), ImVec2(rect.Position.x, rect.Position.y));
                }
            }
            MarkDirty();
        }

        void RunPendingAction()
        {
            const auto action = std::exchange(m_PendingAction, EditorAction::None);
            if (action == EditorAction::None) return;

            auto rects = SelectedRects();
            if (action == EditorAction::CommentSelection)
            {
                if (rects.empty()) return;
                const auto [pos, size] = BoundsOf(rects, 24.0f, kCommentTitleHeight);
                Comment comment;
                comment.Id = CurrentGraph().AllocateId();
                comment.Position = pos;
                comment.Size = size;
                CurrentGraph().Comments.push_back(comment);
                ed::SetNodePosition(ed::NodeId(comment.Id), ImVec2(pos.x, pos.y));
                ed::SetGroupSize(ed::NodeId(comment.Id), ImVec2(size.x, size.y));
                SelectNodes({comment.Id});
                MarkDirty();
                return;
            }

            switch (action)
            {
            case EditorAction::AlignLeft: MoveRects(AlignRects(rects, AlignMode::Left)); break;
            case EditorAction::AlignRight: MoveRects(AlignRects(rects, AlignMode::Right)); break;
            case EditorAction::AlignTop: MoveRects(AlignRects(rects, AlignMode::Top)); break;
            case EditorAction::AlignBottom: MoveRects(AlignRects(rects, AlignMode::Bottom)); break;
            case EditorAction::AlignCenterH: MoveRects(AlignRects(rects, AlignMode::CenterHorizontal)); break;
            case EditorAction::AlignCenterV: MoveRects(AlignRects(rects, AlignMode::CenterVertical)); break;
            case EditorAction::DistributeH: MoveRects(DistributeRects(rects, true)); break;
            case EditorAction::DistributeV: MoveRects(DistributeRects(rects, false)); break;
            default: break;
            }
        }

        void RenderGraphEditor(const std::vector<EntityOption>& entities)
        {
            if (!m_EditorContext) m_EditorContext = CreateEditorContext();
            ed::SetCurrentEditor(m_EditorContext);
            ApplyEditorStyle();
            RebuildLinkedPins();
            m_EditorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
            ed::Begin("AnimationGraphCanvas");

            if (m_NeedsPositionRestore || m_RestorePositions)
            {
                m_RestorePositions = false;
                RestoreEditorPositions();
            }

            for (auto& comment : CurrentGraph().Comments) DrawComment(comment);
            for (auto& node : CurrentGraph().Nodes) DrawNode(node, entities);
            DrawLinks();

            m_HoveredPinId = static_cast<int>(ed::GetHoveredPin().Get());
            m_HoveredNodeId = static_cast<int>(ed::GetHoveredNode().Get());

            HandleLinkCreation();
            HandleDeletion();
            HandleShortcuts();
            RunPendingAction();

            ed::Suspend();
            if (ed::ShowBackgroundContextMenu()) OpenPalette(std::nullopt, 0);
            RenderPalette();
            if (!ImGui::IsPopupOpen("AnimationGraphPalette")) DrawHoverTooltips();
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
                SyncEditorPositions();
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
            std::vector<ed::NodeId> selected(CurrentGraph().Nodes.size() + CurrentGraph().Comments.size() + 1);
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
            if (result.NodeIds.empty() && result.CommentIds.empty()) return;
            for (const int id : result.NodeIds)
            {
                const auto& pos = CurrentGraph().FindNode(id)->Position;
                ed::SetNodePosition(ed::NodeId(id), ImVec2(pos.x, pos.y));
            }
            auto selection = result.NodeIds;
            for (const int id : result.CommentIds)
            {
                const auto* comment = CurrentGraph().FindComment(id);
                ed::SetNodePosition(ed::NodeId(id), ImVec2(comment->Position.x, comment->Position.y));
                ed::SetGroupSize(ed::NodeId(id), ImVec2(comment->Size.x, comment->Size.y));
                selection.push_back(id);
            }
            SelectNodes(selection);
            StructureChanged();
        }

        void HandleShortcuts()
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (!m_EditorFocused || io.WantTextInput) return;
            if (!io.KeyCtrl)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_C, false)) m_PendingAction = EditorAction::CommentSelection;
                return;
            }

            const auto selected = SelectedNodeIds();
            const auto copyText = [&] { return CopyGraphSelection(CurrentGraph(), selected); };

            if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) ApplyHistory(!io.KeyShift);
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) ApplyHistory(false);
            else if (ImGui::IsKeyPressed(ImGuiKey_A, false))
            {
                std::vector<int> all;
                for (const auto& node : CurrentGraph().Nodes) all.push_back(node.Id);
                for (const auto& comment : CurrentGraph().Comments) all.push_back(comment.Id);
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
                for (const int id : selected)
                {
                    if (const Node* node = CurrentGraph().FindNode(id)) minPos = glm::min(minPos, node->Position);
                    else if (const Comment* comment = CurrentGraph().FindComment(id)) minPos = glm::min(minPos, comment->Position);
                }
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
                        RemoveItems(CurrentGraph(), {id});
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
                if (Node* node = graph.FindNode(id))
                {
                    node->Position += m_PaletteCanvasPos;
                    ed::SetNodePosition(ed::NodeId(id), ImVec2(node->Position.x, node->Position.y));
                }
                else if (Comment* comment = graph.FindComment(id))
                {
                    comment->Position += m_PaletteCanvasPos;
                    ed::SetNodePosition(ed::NodeId(id), ImVec2(comment->Position.x, comment->Position.y));
                    ed::SetGroupSize(ed::NodeId(id), ImVec2(comment->Size.x, comment->Size.y));
                }
            }
            if (m_PaletteFilter && m_PaletteDraggedPin != 0 && graph.FindNode(ids.front()))
            {
                ConnectNewNode(graph, ids.front(), m_PaletteDraggedPin);
            }
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

            auto extra = VariableNodeEntries(CurrentGraph());
            std::ranges::move(FunctionNodeEntries(m_Graphs, m_Current < m_Graphs.Items.size() ? &m_Graphs.Items[m_Current] : nullptr),
                              std::back_inserter(extra));
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
            case NodeType::Call:
                drawList->AddTriangleFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x + iconSize, pos.y + iconSize / 2),
                                            ImVec2(pos.x, pos.y + iconSize), ImColor(iconColor));
                break;
            case NodeType::Reroute:
                break;
            }
            ImGui::Dummy(ImVec2(iconSize, iconSize));
        }

        static void DrawHeader(const Node& node, const float nodeWidth)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 headerStart = ImGui::GetCursorScreenPos();
            const ImVec2 headerEnd(headerStart.x + nodeWidth, headerStart.y + kHeaderHeight);

            const glm::vec4 base = GraphNodeColor(node.Type);
            const ImU32 top = ToU32(Lighten(base, 0.12f));
            const ImU32 bottom = ToU32(Darken(base, 0.35f));
            drawList->AddRectFilled(headerStart, headerEnd, top, kNodeRounding, ImDrawFlags_RoundCornersTop);
            drawList->AddRectFilledMultiColor(ImVec2(headerStart.x, headerStart.y + kNodeRounding), headerEnd, top, top,
                                              bottom, bottom);

            ImGui::SetCursorScreenPos(ImVec2(headerStart.x + kNodePadding, headerStart.y + (kHeaderHeight - 16.0f) * 0.5f));
            DrawNodeIcon(node.Type);
            ImGui::SameLine(0, 4.0f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (16.0f - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", node.Name.c_str());

            ImGui::SetCursorScreenPos(ImVec2(headerStart.x, headerEnd.y));
            ImGui::Dummy(ImVec2(nodeWidth, 0));
        }

        static bool EditValue(const std::string& id, Value& value, const float width)
        {
            ImGui::SetNextItemWidth(width);
            if (auto* b = std::get_if<bool>(&value)) return ImGui::Checkbox(id.c_str(), b);
            if (auto* f = std::get_if<float>(&value)) return ImGui::DragFloat(id.c_str(), f, 0.1f);
            if (auto* i = std::get_if<int>(&value)) return ImGui::DragInt(id.c_str(), i);
            if (auto* v2 = std::get_if<glm::vec2>(&value)) return ImGui::DragFloat2(id.c_str(), &v2->x, 0.1f);
            if (auto* v3 = std::get_if<glm::vec3>(&value)) return ImGui::DragFloat3(id.c_str(), &v3->x, 0.1f);
            if (auto* v4 = std::get_if<glm::vec4>(&value)) return ImGui::DragFloat4(id.c_str(), &v4->x, 0.1f);
            if (auto* str = std::get_if<std::string>(&value)) return TextValue(id.c_str(), *str);
            return false;
        }

        void DrawConstantValueInput(Node& node, const float nodeWidth)
        {
            if (EditValue("##const_" + std::to_string(node.Id), node.ConstantValue, nodeWidth - kNodePadding * 2))
            {
                MarkDirty("const:" + std::to_string(node.Id));
            }
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
            drawList->AddRectFilled(contentStart, contentEnd, ToU32(WithAlpha(Darken(Palette::Panel, 0.25f), 0.96f)),
                                    kNodeRounding, ImDrawFlags_RoundCornersBottom);

            for (std::size_t i = 0; i < maxPins; ++i)
            {
                const ImVec2 rowStart(contentStart.x,
                                      contentStart.y + kNodePadding / 2 + static_cast<float>(i) * (kPinSize + 4.0f));

                if (i < node.Inputs.size())
                {
                    const auto& pin = node.Inputs[i];
                    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + kNodePadding, rowStart.y));
                    ed::BeginPin(ed::PinId(pin.Id), ed::PinKind::Input);
                    ed::PinPivotAlignment(ImVec2(0.0f, 0.5f));
                    ed::PinPivotSize(ImVec2(0.0f, 0.0f));
                    DrawPinIcon(pin.Type, m_LinkedPins.contains(pin.Id));
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
                    ed::PinPivotAlignment(ImVec2(1.0f, 0.5f));
                    ed::PinPivotSize(ImVec2(0.0f, 0.0f));
                    ImGui::Text("%s", pin.Name.c_str());
                    ImGui::SameLine(0, 4.0f);
                    DrawPinIcon(pin.Type, m_LinkedPins.contains(pin.Id));
                    ed::EndPin();
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(contentStart.x + kNodePadding, contentStart.y + contentHeight));
            DrawInlineContent(node, nodeWidth, entities);

            ImGui::SetCursorScreenPos(ImVec2(contentStart.x, contentEnd.y));
            ImGui::Dummy(ImVec2(nodeWidth, 0));
        }

        void DrawReroute(const Node& node)
        {
            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(6, 4, 6, 4));
            ed::PushStyleVar(ed::StyleVar_NodeRounding, 10.0f);
            ed::BeginNode(ed::NodeId(node.Id));
            const Pin& in = node.Inputs.front();
            const Pin& out = node.Outputs.front();
            ed::BeginPin(ed::PinId(in.Id), ed::PinKind::Input);
            ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
            ed::PinPivotSize(ImVec2(0.0f, 0.0f));
            DrawPinIcon(in.Type, m_LinkedPins.contains(in.Id));
            ed::EndPin();
            ImGui::SameLine(0, 6.0f);
            ed::BeginPin(ed::PinId(out.Id), ed::PinKind::Output);
            ed::PinPivotAlignment(ImVec2(0.5f, 0.5f));
            ed::PinPivotSize(ImVec2(0.0f, 0.0f));
            DrawPinIcon(out.Type, m_LinkedPins.contains(out.Id));
            ed::EndPin();
            ed::EndNode();
            ed::PopStyleVar(2);
        }

        void DrawComment(Comment& comment)
        {
            const ImVec4 fill = ToVec4(comment.Color);
            ed::PushStyleColor(ed::StyleColor_NodeBg, fill);
            ed::PushStyleColor(ed::StyleColor_NodeBorder, ToVec4(WithAlpha(Lighten(comment.Color, 0.3f), 0.7f)));
            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 6, 8, 8));
            ed::PushStyleVar(ed::StyleVar_NodeRounding, kNodeRounding);
            ed::BeginNode(ed::NodeId(comment.Id));
            ImGui::TextColored(ImVec4(1, 1, 1, 0.95f), "%s", comment.Title.c_str());
            ed::Group(ImVec2(comment.Size.x, comment.Size.y));
            ed::EndNode();
            ed::PopStyleVar(2);
            ed::PopStyleColor(2);
        }

        void DrawNode(Node& node, const std::vector<EntityOption>& entities)
        {
            if (node.Type == NodeType::Reroute && !node.Inputs.empty() && !node.Outputs.empty())
            {
                DrawReroute(node);
                return;
            }

            const bool dangling = node.SubType == NodeSubType::FunctionCall && !FunctionExists(node.FunctionName);
            if (dangling) ed::PushStyleColor(ed::StyleColor_NodeBorder, ImVec4(0.95f, 0.2f, 0.2f, 1.0f));
            ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(0, 0, 0, 0));
            ed::PushStyleVar(ed::StyleVar_NodeRounding, kNodeRounding);

            ed::BeginNode(ed::NodeId(node.Id));
            const float nodeWidth = CalculateNodeWidth(node);
            DrawHeader(node, nodeWidth);
            DrawPinsAndContent(node, nodeWidth, entities);
            ed::EndNode();

            ed::PopStyleVar(2);
            if (dangling) ed::PopStyleColor();
        }

        // ---- inspector panel -------------------------------------------------------------------

        void RenderInspector(const std::vector<EntityOption>& entities)
        {
            RenderSelectionDetails(entities);
            ImGui::Spacing();
            RenderSignaturePanel();
            RenderVariablesPanel();
        }

        bool FunctionExists(const std::string& name) const
        {
            return std::ranges::any_of(m_Graphs.Items, [&](const NamedGraph& g) { return g.IsFunction && g.Name == name; });
        }

        void RenderSignaturePanel()
        {
            if (m_Current >= m_Graphs.Items.size() || !m_Graphs.Items[m_Current].IsFunction) return;
            if (!ImGui::CollapsingHeader("Function Signature", ImGuiTreeNodeFlags_DefaultOpen)) return;

            auto& named = m_Graphs.Items[m_Current];
            auto& signature = named.Signature;
            bool changed = ImGui::Checkbox("Pure", &signature.Pure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pure functions have no flow pins and are evaluated on demand");

            for (const bool output : {false, true})
            {
                ImGui::Spacing();
                ImGui::TextDisabled(output ? "Outputs" : "Inputs");
                auto& params = output ? signature.Outputs : signature.Inputs;
                int removeKey = -1;
                for (auto& param : params)
                {
                    ImGui::PushID(param.Key + (output ? 10000 : 0));
                    const bool editing = m_ParamEdit.Active && m_ParamEdit.Output == output && m_ParamEdit.Key == param.Key;
                    std::string text = editing ? m_ParamEdit.Text : param.Name;
                    ImGui::SetNextItemWidth(100.0f);
                    if (TextValue("##paramname", text)) m_ParamEdit = {true, output, param.Key, text};
                    if (ImGui::IsItemDeactivatedAfterEdit())
                    {
                        if (RenameParam(signature, output, param.Key, m_ParamEdit.Text)) changed = true;
                        m_ParamEdit = {};
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(70.0f);
                    if (ImGui::BeginCombo("##paramtype", PinTypeName(param.Type)))
                    {
                        for (const PinType type : {PinType::Bool, PinType::Float, PinType::Int, PinType::Vec2, PinType::Vec3,
                                                   PinType::Vec4, PinType::String, PinType::Object})
                        {
                            if (ImGui::Selectable(PinTypeName(type), type == param.Type) &&
                                RetypeParam(signature, output, param.Key, type))
                            {
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) removeKey = param.Key;
                    ImGui::PopID();
                }
                if (removeKey >= 0) changed |= RemoveParam(signature, output, removeKey);
                ImGui::PushID(output ? "addout" : "addin");
                if (ImGui::SmallButton(output ? "+ Output" : "+ Input"))
                {
                    AddParam(signature, output, output ? "Result" : "Input", PinType::Float);
                    changed = true;
                }
                ImGui::PopID();
            }

            if (changed)
            {
                SyncFunction(m_Graphs, named.Name);
                StructureChanged();
            }
        }

        void RenderSelectionDetails(const std::vector<EntityOption>& entities)
        {
            if (m_SelectedNodeId == 0)
            {
                ImGui::TextDisabled("Select a node to edit");
                return;
            }

            if (Comment* comment = CurrentGraph().FindComment(m_SelectedNodeId))
            {
                ImGui::TextUnformatted("Comment");
                ImGui::Separator();
                ImGui::SetNextItemWidth(-1);
                if (TextValue("##commenttitle", comment->Title, "Title")) MarkDirty("title:" + std::to_string(comment->Id));
                float color[4] = {comment->Color.r, comment->Color.g, comment->Color.b, comment->Color.a};
                if (ColorValue("##commentcolor", color, true))
                {
                    comment->Color = glm::vec4(color[0], color[1], color[2], color[3]);
                    MarkDirty("color:" + std::to_string(comment->Id));
                }
                if (ImGui::Button("Delete Comment", ImVec2(-1, 0)))
                {
                    RemoveComment(CurrentGraph(), comment->Id);
                    m_SelectedNodeId = 0;
                    MarkDirty();
                }
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
            if (const std::string description = Registry().Describe(*node); !description.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::TextWrapped("%s", description.c_str());
                ImGui::PopStyleColor();
            }
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
            else if (node->Type == NodeType::Reroute)
            {
                ImGui::TextDisabled("Type: %s", PinTypeName(node->Inputs.front().Type));
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

        void RenderVariablesPanel()
        {
            if (m_Current >= m_Graphs.Items.size()) return;
            if (!ImGui::CollapsingHeader("Variables", ImGuiTreeNodeFlags_DefaultOpen)) return;

            auto& graph = CurrentGraph();
            int removeIndex = -1;
            for (int i = 0; i < static_cast<int>(graph.Variables.size()); ++i)
            {
                auto& variable = graph.Variables[i];
                ImGui::PushID(i);
                ImGui::TextColored(PinColor(variable.Type), "%s", PinTypeName(variable.Type));
                ImGui::SameLine();

                std::string text = m_RenamingVariable == i ? m_RenameText : variable.Name;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
                if (TextValue("##varname", text))
                {
                    m_RenamingVariable = i;
                    m_RenameText = text;
                }
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    if (RenameVariable(graph, variable.Name, m_RenameText)) MarkDirty();
                    m_RenamingVariable = -1;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) removeIndex = i;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete variable");

                if (std::holds_alternative<std::monostate>(variable.Default)) variable.Default = DefaultValueFor(variable.Type);
                if (EditValue("##vardefault", variable.Default, -1.0f)) MarkDirty("vardefault:" + std::to_string(i));
                ImGui::PopID();
                ImGui::Spacing();
            }
            if (removeIndex >= 0)
            {
                RemoveVariable(graph, graph.Variables[removeIndex].Name);
                m_RenamingVariable = -1;
                MarkDirty();
            }

            if (ImGui::Button("+ Add Variable")) ImGui::OpenPopup("AddVariableType");
            if (ImGui::BeginPopup("AddVariableType"))
            {
                for (const PinType type : {PinType::Bool, PinType::Float, PinType::Int, PinType::Vec2, PinType::Vec3,
                                           PinType::Vec4, PinType::String, PinType::Object})
                {
                    if (ImGui::MenuItem(PinTypeName(type)))
                    {
                        AddVariable(graph, "NewVariable", type);
                        MarkDirty();
                    }
                }
                ImGui::EndPopup();
            }
        }

        void RenderVariableSelector(Node& node)
        {
            PinType varType = PinType::Float;
            if (!node.Outputs.empty()) varType = node.Outputs[0].Type;
            else if (node.Inputs.size() > 1) varType = node.Inputs[1].Type;

            ImGui::Text("Variable (%s):", PinTypeName(varType));
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
                ImGui::Text("New %s Variable:", PinTypeName(varType));
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
                    if (!exists) CurrentGraph().Variables.push_back(Variable{name, varType, DefaultValueFor(varType)});
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
        int m_RenamingVariable = -1;
        std::string m_RenameText;
        int m_SelectionCount = 0;
        std::unordered_set<int> m_LinkedPins;
        int m_HoveredPinId = 0;
        int m_HoveredNodeId = 0;
        int m_LastHoveredNodeId = 0;
        float m_HoverNodeTime = 0.0f;
        enum class EditorAction
        {
            None, AlignLeft, AlignRight, AlignTop, AlignBottom, AlignCenterH, AlignCenterV, DistributeH, DistributeV,
            CommentSelection,
        };
        EditorAction m_PendingAction = EditorAction::None;
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

        struct ParamEdit
        {
            bool Active{false};
            bool Output{false};
            int Key{0};
            std::string Text;
        };
        ParamEdit m_ParamEdit;

        std::string m_LastSceneName;
        bool m_WasPaused = true;
        std::unique_ptr<GraphSetExecutor> m_Executor;

        bool m_ShowNewVariableDialog = false;
        std::array<char, 128> m_NewVariableBuffer{};
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(AnimationGraphWindowLayer)
