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
                m_Executor = std::make_unique<GraphExecutor>(m_Graph, [logger = m_Logger](std::string message)
                {
                    logger->Info("[AnimationGraph] {}", message);
                });
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

            constexpr float inspectorWidth = 280.0f;
            const ImVec2 avail = ImGui::GetContentRegionAvail();

            ImGui::BeginChild("AnimationGraphEditorPanel", ImVec2(avail.x - inspectorWidth - 8.0f, 0), true);
            RenderGraphEditor(entities);
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
                SaveGraphToScene(*runner);
                m_Dirty = false;
            }
        }

    private:
        void LoadGraphFromScene()
        {
            m_Graph = AnimationGraphData{};
            if (!m_UiState->CurrentSceneName.empty())
            {
                if (const auto runner = m_Scenes->GetSimulation(m_UiState->CurrentSceneName))
                {
                    auto sceneLock = runner->LockRenderScene();
                    for (auto [entity, component] : sceneLock->Registry().view<const AnimationGraphComponent>().each())
                    {
                        m_Graph = DeserializeFromYaml(component.GraphYaml);
                        break;
                    }
                }
            }
            m_Dirty = false;
            m_SelectedNodeId = 0;
            m_NeedsPositionRestore = true;
            m_PendingNavigateFrames = 0;
        }

        void SaveGraphToScene(SimulationRunner& runner) const
        {
            const std::string yaml = SerializeToYaml(m_Graph);
            runner.EnqueueEdit([yaml](Scene& scene)
            {
                bool found = false;
                for (auto [entity, component] : scene.Registry().view<AnimationGraphComponent>().each())
                {
                    component.GraphYaml = yaml;
                    found = true;
                    break;
                }
                if (!found)
                {
                    const auto entity = scene.CreateEntity("", kAnimationGraphDataTypeTag);
                    scene.Registry().emplace<AnimationGraphComponent>(entity, AnimationGraphComponent{yaml});
                }
            });
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
            ed::Begin("AnimationGraphCanvas");

            if (m_NeedsPositionRestore)
            {
                for (const auto& node : m_Graph.Nodes)
                {
                    ed::SetNodePosition(ed::NodeId(node.Id), ImVec2(node.Position.x, node.Position.y));
                }
            }

            for (auto& node : m_Graph.Nodes)
            {
                DrawNode(node, entities);
            }

            for (const auto& link : m_Graph.Links)
            {
                ed::Link(ed::LinkId(link.Id), ed::PinId(link.StartPinId), ed::PinId(link.EndPinId));
            }

            HandleLinkCreation();
            HandleDeletion();

            ed::Suspend();
            HandleContextMenu();
            ed::Resume();

            ed::End();

            if (m_NeedsPositionRestore)
            {
                m_NeedsPositionRestore = false;
                m_PendingNavigateFrames = m_Graph.Nodes.empty() ? 0 : kNavigateToContentRetryFrames;
            }
            else if (m_PendingNavigateFrames > 0)
            {
                ed::NavigateToContent();
                --m_PendingNavigateFrames;
            }
            else
            {
                for (auto& node : m_Graph.Nodes)
                {
                    const auto pos = ed::GetNodePosition(ed::NodeId(node.Id));
                    if (std::abs(pos.x - node.Position.x) > 0.01f || std::abs(pos.y - node.Position.y) > 0.01f)
                    {
                        node.Position = glm::vec2(pos.x, pos.y);
                        m_Dirty = true;
                    }
                }
            }

            ed::SetCurrentEditor(nullptr);
        }

        const Pin* FindOutputPin(const int pinId) const
        {
            for (const auto& node : m_Graph.Nodes)
            {
                for (const auto& pin : node.Outputs) { if (pin.Id == pinId) return &pin; }
            }
            return nullptr;
        }

        const Pin* FindInputPin(const int pinId) const
        {
            for (const auto& node : m_Graph.Nodes)
            {
                for (const auto& pin : node.Inputs) { if (pin.Id == pinId) return &pin; }
            }
            return nullptr;
        }

        void HandleLinkCreation()
        {
            if (ed::BeginCreate())
            {
                ed::PinId startPinId, endPinId;
                if (ed::QueryNewLink(&startPinId, &endPinId))
                {
                    if (startPinId && endPinId && startPinId != endPinId)
                    {
                        const int startId = static_cast<int>(startPinId.Get());
                        const int endId = static_cast<int>(endPinId.Get());

                        const bool inputUsed = std::ranges::any_of(m_Graph.Links, [endId](const Link& link)
                        {
                            return link.EndPinId == endId;
                        });

                        const Pin* startPin = FindOutputPin(startId);
                        const Pin* endPin = FindInputPin(endId);

                        if (!inputUsed && startPin && endPin && ArePinsCompatible(startPin->Type, endPin->Type))
                        {
                            if (ed::AcceptNewItem())
                            {
                                m_Graph.Links.push_back(Link{m_Graph.AllocateId(), startId, endId});
                                m_Dirty = true;
                            }
                        }
                        else
                        {
                            ed::RejectNewItem(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), 2.0f);
                        }
                    }
                }
            }
            ed::EndCreate();
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
                        m_Graph.RemoveLink(static_cast<int>(deletedLinkId.Get()));
                        m_Dirty = true;
                    }
                }

                ed::NodeId deletedNodeId;
                while (ed::QueryDeletedNode(&deletedNodeId))
                {
                    if (ed::AcceptDeletedItem())
                    {
                        const int id = static_cast<int>(deletedNodeId.Get());
                        m_Graph.RemoveNode(id);
                        if (m_SelectedNodeId == id) m_SelectedNodeId = 0;
                        m_Dirty = true;
                    }
                }
            }
            ed::EndDelete();
        }

        void HandleContextMenu()
        {
            if (ed::ShowBackgroundContextMenu())
            {
                ImGui::OpenPopup("AnimationGraphCreateNode");
            }

            if (!ImGui::BeginPopup("AnimationGraphCreateNode")) return;

            const ImVec2 openPos = ImGui::GetMousePosOnOpeningCurrentPopup();
            const ImVec2 canvasPos = ed::ScreenToCanvas(openPos);

            auto addNode = [&](Node node)
            {
                node.Position = glm::vec2(canvasPos.x, canvasPos.y);
                const int id = node.Id;
                m_Graph.Nodes.push_back(std::move(node));
                ed::SetNodePosition(ed::NodeId(id), canvasPos);
                ed::SelectNode(ed::NodeId(id));
                m_SelectedNodeId = id;
                m_Dirty = true;
            };

            if (ImGui::BeginMenu("Events"))
            {
                if (ImGui::MenuItem("Start")) addNode(CreateStartEventNode(m_Graph.AllocateId()));
                if (ImGui::MenuItem("Tick")) addNode(CreateTickEventNode(m_Graph.AllocateId()));
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Constants"))
            {
                static constexpr std::pair<const char*, PinType> kTypes[] = {
                    {"Bool", PinType::Bool}, {"Float", PinType::Float}, {"Int", PinType::Int},
                    {"Vec2", PinType::Vec2}, {"Vec3", PinType::Vec3}, {"Vec4", PinType::Vec4},
                    {"String", PinType::String},
                };
                for (const auto& [label, type] : kTypes)
                {
                    if (ImGui::MenuItem(label)) addNode(CreateConstantNode(m_Graph.AllocateId(), type));
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Math"))
            {
                static constexpr std::pair<const char*, NodeSubType> kBinary[] = {
                    {"Add", NodeSubType::Add}, {"Subtract", NodeSubType::Sub}, {"Multiply", NodeSubType::Mul},
                    {"Divide", NodeSubType::Div}, {"Min", NodeSubType::Min}, {"Max", NodeSubType::Max},
                };
                for (const auto& [label, op] : kBinary)
                {
                    if (ImGui::MenuItem(label)) addNode(CreateMathNode(m_Graph.AllocateId(), op));
                }
                ImGui::Separator();
                static constexpr std::pair<const char*, NodeSubType> kUnary[] = {
                    {"Negate", NodeSubType::Negate}, {"Sin", NodeSubType::Sin}, {"Cos", NodeSubType::Cos},
                    {"Tan", NodeSubType::Tan}, {"Sqrt", NodeSubType::Sqrt}, {"Length", NodeSubType::Length},
                };
                for (const auto& [label, op] : kUnary)
                {
                    if (ImGui::MenuItem(label)) addNode(CreateMathNode(m_Graph.AllocateId(), op));
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Distance")) addNode(CreateMathNode(m_Graph.AllocateId(), NodeSubType::Distance));
                if (ImGui::MenuItem("Lerp")) addNode(CreateMathNode(m_Graph.AllocateId(), NodeSubType::Lerp));
                if (ImGui::MenuItem("Clamp")) addNode(CreateMathNode(m_Graph.AllocateId(), NodeSubType::Clamp));
                ImGui::Separator();
                if (ImGui::MenuItem("And")) addNode(CreateMathNode(m_Graph.AllocateId(), NodeSubType::And));
                if (ImGui::MenuItem("Or")) addNode(CreateMathNode(m_Graph.AllocateId(), NodeSubType::Or));
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Control Flow"))
            {
                if (ImGui::MenuItem("Branch")) addNode(CreateBranchNode(m_Graph.AllocateId()));
                if (ImGui::MenuItem("For Loop")) addNode(CreateForNode(m_Graph.AllocateId()));
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Objects"))
            {
                for (const auto* category : GetPropertyCategories())
                {
                    if (ImGui::BeginMenu(category->DisplayName.c_str()))
                    {
                        const auto& name = category->ComponentName;
                        if (ImGui::MenuItem("Get")) addNode(CreateGetterNode(m_Graph.AllocateId(), name));
                        if (ImGui::MenuItem("Decompose")) addNode(CreateDecomposerNode(m_Graph.AllocateId(), name));
                        if (ImGui::MenuItem("Set")) addNode(CreateSetterNode(m_Graph.AllocateId(), name));
                        ImGui::EndMenu();
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Variables"))
            {
                static constexpr std::pair<const char*, PinType> kVarTypes[] = {
                    {"Bool", PinType::Bool}, {"Float", PinType::Float}, {"Int", PinType::Int},
                    {"Vec2", PinType::Vec2}, {"Vec3", PinType::Vec3}, {"Vec4", PinType::Vec4},
                    {"String", PinType::String}, {"Object", PinType::Object},
                };
                if (ImGui::BeginMenu("Get"))
                {
                    for (const auto& [label, type] : kVarTypes)
                    {
                        if (ImGui::MenuItem(label))
                        {
                            Node node = CreateVariableGetNode(m_Graph.AllocateId(), "", type);
                            node.Name = std::string("Get Variable (") + label + ")";
                            addNode(std::move(node));
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Set"))
                {
                    for (const auto& [label, type] : kVarTypes)
                    {
                        if (ImGui::MenuItem(label))
                        {
                            Node node = CreateVariableSetNode(m_Graph.AllocateId(), "", type);
                            node.Name = std::string("Set Variable (") + label + ")";
                            addNode(std::move(node));
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Print")) addNode(CreatePrintNode(m_Graph.AllocateId()));

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

        static void DrawConstantValueInput(Node& node, const float nodeWidth)
        {
            ImGui::SetNextItemWidth(nodeWidth - kNodePadding * 2);
            const std::string id = "##const_" + std::to_string(node.Id);

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

            Node* node = m_Graph.FindNode(m_SelectedNodeId);
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
                m_Graph.RemoveNode(node->Id);
                m_SelectedNodeId = 0;
                m_Dirty = true;
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

                const bool anyMatching = std::ranges::any_of(m_Graph.Variables, [varType](const Variable& v)
                {
                    return v.Type == varType;
                });
                if (anyMatching) ImGui::Separator();

                for (const auto& variable : m_Graph.Variables)
                {
                    if (variable.Type != varType) continue;
                    const bool selected = variable.Name == node.VariableName;
                    if (ImGui::Selectable(variable.Name.c_str(), selected))
                    {
                        node.VariableName = variable.Name;
                        m_Dirty = true;
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
                    const bool exists = std::ranges::any_of(m_Graph.Variables, [&](const Variable& v)
                    {
                        return v.Name == name && v.Type == varType;
                    });
                    if (!exists) m_Graph.Variables.push_back(Variable{name, varType});
                    node.VariableName = name;
                    m_Dirty = true;
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
                        m_Dirty = true;
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
        AnimationGraphData m_Graph;
        bool m_Dirty = false;
        bool m_NeedsPositionRestore = true;
        int m_PendingNavigateFrames = 0;
        int m_SelectedNodeId = 0;

        std::string m_LastSceneName;
        bool m_WasPaused = true;
        std::unique_ptr<GraphExecutor> m_Executor;

        bool m_ShowNewVariableDialog = false;
        std::array<char, 128> m_NewVariableBuffer{};
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(AnimationGraphWindowLayer)
