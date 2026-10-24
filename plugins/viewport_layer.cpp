import GPP;
import MoleHole;
import vulkan;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    ImGuizmo::OPERATION ToImGuizmoOperation(GizmoOperation operation)
    {
        switch (operation)
        {
        case GizmoOperation::Translate: return ImGuizmo::TRANSLATE;
        case GizmoOperation::Rotate: return ImGuizmo::ROTATE;
        case GizmoOperation::Scale: return ImGuizmo::SCALE;
        }
        return ImGuizmo::TRANSLATE;
    }

    struct ViewportFrameParams
    {
        Camera ViewCamera;
        RenderSettings Render;
        std::uint64_t ObserverGuid = 0; // possessed scene camera entity, for a camera riding a Geodesic Body
        std::uint64_t SceneKey = 0;
        bool Interactive = true;
    };

    struct SimBinding
    {
        std::shared_ptr<SimulationRunner> Runner;
        std::shared_ptr<PlaySession> Play = std::make_shared<PlaySession>();
    };

    // Camera, picking, gizmos and scene switching; the drawing itself is the RenderPipeline's
    struct ViewportLayer final : public HotReloadableLayer
    {
        using Dependencies =
            std::tuple<Logger, Renderer, IFileSystem, EventDispatcher, SceneManager, InputState, UiState,
                       AppStateService, RenderSettingsService, AppPaths>;

        ViewportLayer(const std::shared_ptr<Logger>& logger,
                      const std::shared_ptr<Renderer>& renderer,
                      const std::shared_ptr<IFileSystem>& fileSystem,
                      const std::shared_ptr<EventDispatcher>& dispatcher,
                      const std::shared_ptr<SceneManager>& scenes,
                      const std::shared_ptr<InputState>& input,
                      const std::shared_ptr<UiState>& uiState,
                      const std::shared_ptr<AppStateService>& appState,
                      const std::shared_ptr<RenderSettingsService>& renderSettings,
                      const std::shared_ptr<AppPaths>& appPaths)
            : HotReloadableLayer(logger), m_Renderer(renderer), m_Scenes(scenes), m_Input(input), m_UiState(uiState),
              m_AppState(appState), m_RenderSettings(renderSettings),
              m_Meshes(std::make_shared<MeshCache>(renderer, fileSystem, logger)),
              m_Loader(scenes, m_Meshes, logger),
              m_Pipeline(renderer, fileSystem, dispatcher, logger, m_Meshes),
              m_Export(renderer, uiState, appState, renderSettings, appPaths, logger, kViewportBufferId,
                       [this] { return m_Sim.Load()->Runner; }),
              m_Dispatcher(dispatcher)
        {
        }

        void OnAttach() override
        {
            m_AttachTime = std::chrono::steady_clock::now();
            MoleHole::RegisterComponents();
            if (!m_UiState->PendingNewScene && !m_UiState->PendingLoadTemplatePath.Pending())
            {
                BeginSceneLoad(StartupScenePath(), SceneLoader::Result::Kind::Startup);
            }

            m_Pipeline.Initialize();
            m_RenderSettings->SetCapabilities(m_Pipeline.Capabilities());
            m_Logger->Info("ViewportLayer attached");
        }

        void OnDetach() override
        {
            m_Loader.WaitIdle();
            CommitCamera();
            m_RenderSettings->HoldForReload();
            if (const auto sim = m_Sim.Load(); sim->Runner) sim->Runner->Stop();
            m_Logger->Info("ViewportLayer detached");
        }

        void OnUpdate(float deltaTime) override
        {
            GPP::ProfileZone updateZone("Viewport.Update");
            PollSceneLoad();
            CheckPendingSceneSwitch();
            UpdateCamera(deltaTime);
            TrackCamera();
            m_Export.Update();
            UpdateRenderSettings();
            auto render = m_RenderSettings->Effective();
            std::uint64_t sceneKey = 0;
            if (const auto sim = m_Sim.Load(); sim->Runner)
            {
                ReportInitErrors(*sim->Runner);
                if (const auto gravity = sim->Runner->GetModule<GravitySimulationModule>())
                {
                    gravity->SetGravityMultiplier(m_UiState->GravityMultiplier);
                    gravity->SetPrecision(m_UiState->SimulationPrecision);
                }
                // Tick rate is a UI setting; the runner is told whenever it differs (also covers a
                // freshly started simulation).
                if (sim->Runner->GetTickRate() != m_UiState->SimulationTickRate)
                    sim->Runner->SetTickRate(m_UiState->SimulationTickRate);
                const auto published = sim->Runner->AcquireSnapshot();
                SignatureHasher key;
                key.Pointer(sim->Runner.get());
                key.Value(published.Generation());
                sceneKey = key.Digest();
                RecordPaths(render.ShowObjectPaths, !sim->Runner->IsPaused(), sceneKey, *published);
                m_UiState->SceneState.Observe(published.ContentRevision());
                if (m_UiState->PendingSelectGuid != 0 && published->IsValid(published->FindByGuid(m_UiState->PendingSelectGuid)))
                {
                    m_UiState->SelectedEntityGuid = std::exchange(m_UiState->PendingSelectGuid, 0);
                }
            }
            const auto step = StepRefinement(render, sceneKey);
            render.ResolutionScale *= step.Scale;
            render.Accumulate = render.Accumulate && step.Accumulate;
            m_UiState->RenderScaleApplied = step.Scale;
            LogRefinement(step);
            PublishObserverReadout(render, sceneKey);
            const bool interactive = m_ActiveContext != RenderContext::Export;
            RequestFrameOnChange(sceneKey, interactive, step);
            m_FrameParams.Publish(ViewportFrameParams{m_Camera, std::move(render), m_ObserverGuid, sceneKey, interactive});
        }

        void OnRender() override
        {
            FixupPendingConvexHulls();
            const bool shadersReady = m_Pipeline.AllStagesReady();
            if (shadersReady && !std::exchange(m_ShadersReady, true))
            {
                m_Logger->Notifications()->Post(
                    NotificationKind::Success, "Shaders compiled",
                    std::format("Render pipelines ready ({:.1f} s)",
                                std::chrono::duration<double>(std::chrono::steady_clock::now() - m_AttachTime).count()),
                    0.0f, "shaders-ready");
            }
            m_Export.SetPipelinesReady(shadersReady);
            m_Export.SetPipelineFailure(m_Pipeline.FirstFailure());
        }

        void OnRenderGraph(RenderGraph& graph) override
        {
            const auto sim = m_Sim.Load();
            if (!sim->Runner) return;
            const auto frameParams = m_FrameParams.Load();

            RenderInputs inputs;
            inputs.ViewCamera = frameParams->ViewCamera;
            inputs.Settings = frameParams->Render;
            inputs.Physics = sim->Runner->GetModule<PhysicsSimulationModule>();
            inputs.ObserverGuid = frameParams->ObserverGuid;
            inputs.SceneKey = frameParams->SceneKey;
            inputs.Interactive = frameParams->Interactive;
            const auto sceneLock = sim->Runner->AcquireSnapshot();
            inputs.Scene = &*sceneLock;
            m_Pipeline.Build(graph, inputs);
        }

        void OnUiRender() override
        {
            ImGuizmo::BeginFrame();
            if (!m_UiState->Edits.Take({EditAction::FrameSelected}).empty()) m_FocusRequest.store(true, std::memory_order_relaxed);
            const bool viewportOpen = ImGui::Begin("Viewport");
            m_UiState->ViewportVisible = viewportOpen;
            const bool viewportFocused =
                viewportOpen && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            m_CameraKeysEnabled.store(viewportFocused && !ImGui::GetIO().WantTextInput, std::memory_order_relaxed);
            m_CameraMouseHover.store(false, std::memory_order_relaxed);
            if (viewportFocused)
            {
                if (!ImGui::GetIO().WantTextInput)
                {
                    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) m_UiState->Edits.Push(EditAction::FrameSelected);
                    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) m_UiState->Edits.Push(EditAction::Delete);
                }
            }
            m_Renderer->SetBufferTargetVisible(m_LayerTarget.Id, viewportOpen || m_UiState->ExportActive);
            if (!viewportOpen)
            {
                m_UiState->ViewportScreenMin = {0.0f, 0.0f};
                m_UiState->ViewportScreenMax = {0.0f, 0.0f};
                ImGui::End();
                return;
            }

            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (!m_UiState->ExportActive && avail.x >= 1.0f && avail.y >= 1.0f)
            {
                m_Camera.SetAspect(avail.x / avail.y);
                m_Renderer->ResizeBufferTarget(
                    m_LayerTarget.Id,
                    glm::uvec2{static_cast<std::uint32_t>(avail.x), static_cast<std::uint32_t>(avail.y)});
            }

            if (const auto target = m_Renderer->GetRenderTargetInfo(m_LayerTarget.Id))
            {
                ImGui::Image(reinterpret_cast<ImTextureID>(target->ImGuiTexture), avail);
                const bool imageHovered = ImGui::IsItemHovered();
                m_CameraMouseHover.store(imageHovered, std::memory_order_relaxed);
                if (imageHovered) m_PendingScroll.fetch_add(ImGui::GetIO().MouseWheel, std::memory_order_relaxed);
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                m_UiState->ViewportScreenMin = {min.x, min.y};
                m_UiState->ViewportScreenMax = {max.x, max.y};
                if (m_UiState->ShowRenderStats)
                    DrawRenderStatsOverlay(min, m_Renderer->GetBufferTargetStats(kViewportBufferId), *m_RenderSettings,
                                           m_UiState->RenderScaleApplied, m_Pipeline.Refinement(), m_LastStep);

                m_UiState->ViewportToolbarLeft = max.x;
                if (!m_UiState->ExportActive)
                {
                    const bool toolbarHovered = RenderGizmoToolbar(min, max);
                    UpdateGizmoAndPicking(min, max, imageHovered, toolbarHovered);
                }
            }
            else
            {
                ImGui::TextUnformatted("Viewport buffer not ready yet.");
            }

            if (m_UiState->ExportActive)
            {
                ImGui::SetCursorPos(ImVec2(8, std::max(ImGui::GetFrameHeight() + 8.0f, ImGui::GetWindowHeight() - 112.0f)));
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.6f));
                ImGui::BeginChild("##ExportProgress", ImVec2(300, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                                  ImGuiWindowFlags_NoScrollbar);
                ImGui::TextUnformatted(m_UiState->ExportStatus.c_str());
                ImGui::ProgressBar(m_UiState->ExportProgress, ImVec2(-1, 0));
                ImGui::TextUnformatted(
                    FormatExportTiming(m_UiState->ExportElapsedSeconds, m_UiState->ExportRemainingSeconds).c_str());
                if (ImGui::Button("Cancel")) m_UiState->ExportCancelRequested = true;
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            ImGui::End();
        }

    private:
        [[nodiscard]] bool RenderGizmoToolbar(ImVec2 viewportMin, ImVec2 viewportMax)
        {
            struct ButtonSpec { const char* Label; GizmoOperation Op; const char* Tooltip; };
            const std::array<ButtonSpec, 3> operationButtons{{
                {"Move", GizmoOperation::Translate, "Translate (T / 1)"},
                {"Rotate", GizmoOperation::Rotate, "Rotate (R / 2)"},
                {"Scale", GizmoOperation::Scale, "Scale (S / 3)"},
            }};
            const char* modeLabel = m_UiState->ActiveGizmoMode == GizmoMode::Local ? "Local" : "World";
            const char* snapLabel = "Snap";

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            std::array<float, operationButtons.size()> opWidths{};
            float contentWidth = 0.0f;
            for (std::size_t i = 0; i < operationButtons.size(); ++i)
            {
                opWidths[i] = ViewportToolbar::TextButtonWidth(operationButtons[i].Label);
                contentWidth += opWidths[i] + spacing;
            }
            const float modeWidth = ViewportToolbar::TextButtonWidth(modeLabel);
            const float snapWidth = ViewportToolbar::TextButtonWidth(snapLabel);
            contentWidth += modeWidth + spacing + snapWidth;

            const ImVec2 panelSize(ViewportToolbar::BarWidth(contentWidth), ViewportToolbar::Height());
            const ImVec2 panelMin(std::max(viewportMax.x - panelSize.x - ViewportToolbar::kMargin, viewportMin.x + ViewportToolbar::kMargin),
                                  ViewportToolbar::Top(viewportMin.y));
            m_UiState->ViewportToolbarLeft = panelMin.x;

            if (ViewportToolbar::Begin("##GizmoToolbar", panelMin))
            {
                for (std::size_t i = 0; i < operationButtons.size(); ++i)
                {
                    const auto& button = operationButtons[i];
                    if (ViewportToolbar::Button(button.Label, opWidths[i], m_UiState->ActiveGizmoOperation == button.Op))
                        m_UiState->ActiveGizmoOperation = button.Op;
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", button.Tooltip);
                    ImGui::SameLine();
                }
                if (ViewportToolbar::Button(modeLabel, modeWidth))
                {
                    m_UiState->ActiveGizmoMode =
                        m_UiState->ActiveGizmoMode == GizmoMode::Local ? GizmoMode::World : GizmoMode::Local;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Gizmo space: local or world axes");
                ImGui::SameLine();
                if (ViewportToolbar::Button(snapLabel, snapWidth, m_UiState->GizmoSnapEnabled))
                    m_UiState->GizmoSnapEnabled = !m_UiState->GizmoSnapEnabled;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snap the gizmo to the configured steps");
            }
            ViewportToolbar::End();

            return ImGui::IsMouseHoveringRect(panelMin, ImVec2(panelMin.x + panelSize.x, panelMin.y + panelSize.y));
        }

        void UpdateGizmoAndPicking(ImVec2 min, ImVec2 max, bool imageHovered, bool toolbarHovered)
        {
            if (imageHovered && !ImGui::GetIO().WantTextInput)
            {
                if (ImGui::IsKeyPressed(ImGuiKey_1) || ImGui::IsKeyPressed(ImGuiKey_T))
                    m_UiState->ActiveGizmoOperation = GizmoOperation::Translate;
                if (ImGui::IsKeyPressed(ImGuiKey_2) || ImGui::IsKeyPressed(ImGuiKey_R))
                    m_UiState->ActiveGizmoOperation = GizmoOperation::Rotate;
                if (ImGui::IsKeyPressed(ImGuiKey_3) || ImGui::IsKeyPressed(ImGuiKey_S))
                    m_UiState->ActiveGizmoOperation = GizmoOperation::Scale;
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_UiState->SelectedEntityGuid = 0;
            }

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(min.x, min.y, max.x - min.x, max.y - min.y);

            bool gizmoActive = false;
            const auto sim = m_Sim.Load();
            if (m_UiState->SelectedEntityGuid != 0 && sim->Runner)
            {
                gizmoActive = ManipulateSelectedEntity(*sim);
            }

            if (imageHovered && !toolbarHovered && !ImGui::GetIO().WantTextInput && sim->Runner &&
                (ImGui::IsKeyChordPressed(ImGuiMod_Shift | ImGuiKey_A) ||
                 (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Right, 4.0f))))
            {
                m_UiState->CreatePopup.RequestAtMouse();
            }

            if (imageHovered && !toolbarHovered && !gizmoActive && !ImGuizmo::IsOver() && sim->Runner &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                PickEntityUnderMouse(min, max, *sim);
            }
        }

        bool ManipulateSelectedEntity(const SimBinding& sim)
        {
            glm::mat4 model(1.0f);
            bool hasTransform = false;
            RenderFrame frame;
            std::string entityName;
            {
                const auto sceneLock = sim.Runner->AcquireSnapshot();
                const Scene& scene = *sceneLock;
                frame = RenderFrame::ForScene(scene, m_RenderSettings->Effective().LengthUnit,
                                              m_Camera.GetPosition());
                const auto entity = scene.FindByGuid(m_UiState->SelectedEntityGuid);
                if (scene.IsValid(entity) && scene.Registry().all_of<TransformComponent>(entity))
                {
                    if (const auto* metadata = scene.Registry().try_get<MetadataComponent>(entity)) entityName = metadata->Name;
                    // Gizmo matrix: render-space translation and rotation, the entity's own Scale (dimensionless).
                    const auto& transform = scene.Registry().get<TransformComponent>(entity);
                    model = transform.GetRenderMatrix(frame.OriginM, frame.LengthUnitM, frame.LengthUnitM);
                    hasTransform = true;
                }
            }
            if (!hasTransform)
            {
                m_UiState->SelectedEntityGuid = 0;
                return false;
            }

            const glm::mat4 view = m_Camera.GetViewMatrix(frame);
            glm::mat4 projection = m_Camera.GetProjectionMatrix();
            projection[1][1] *= -1.0f;
            const auto operation = ToImGuizmoOperation(m_UiState->ActiveGizmoOperation);
            const auto mode = m_UiState->ActiveGizmoMode == GizmoMode::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

            float snapValues[3]{};
            const float* snapPtr = nullptr;
            if (m_UiState->GizmoSnapEnabled)
            {
                switch (m_UiState->ActiveGizmoOperation)
                {
                case GizmoOperation::Translate:
                    // Boundary SI -> render space: the gizmo works in render units.
                    snapValues[0] = static_cast<float>(m_UiState->GizmoTranslateSnapM.x / frame.LengthUnitM);
                    snapValues[1] = static_cast<float>(m_UiState->GizmoTranslateSnapM.y / frame.LengthUnitM);
                    snapValues[2] = static_cast<float>(m_UiState->GizmoTranslateSnapM.z / frame.LengthUnitM);
                    break;
                case GizmoOperation::Rotate:
                    snapValues[0] = snapValues[1] = snapValues[2] = m_UiState->GizmoRotateSnapDegrees;
                    break;
                case GizmoOperation::Scale:
                    snapValues[0] = snapValues[1] = snapValues[2] = m_UiState->GizmoScaleSnap;
                    break;
                }
                snapPtr = snapValues;
            }

            const glm::mat4 before = model;
            const bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection),
                                                           operation, mode, glm::value_ptr(model), nullptr, snapPtr);
            if (manipulated && GizmoMatrixChanged(before, model))
            {
                float t[3], r[3], s[3];
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), t, r, s);
                const glm::dvec3 position = frame.ToWorld(glm::vec3(t[0], t[1], t[2]));
                const glm::vec3 scale(s[0], s[1], s[2]);
                const auto guid = m_UiState->SelectedEntityGuid;
                CommandOptions options;
                options.Undoable = sim.Runner->IsPaused();
                const char* verb = m_UiState->ActiveGizmoOperation == GizmoOperation::Translate ? "Move"
                                   : m_UiState->ActiveGizmoOperation == GizmoOperation::Rotate  ? "Rotate"
                                                                                                : "Scale";
                options.Label = entityName.empty() ? std::string(verb) : std::format("{} {}", verb, ShortenLabel(entityName, 28));
                options.CoalesceKey = std::format("gizmo:{}:{}", guid, verb);
                sim.Runner->EnqueueCommands(
                    {SetFieldCommand{guid, "Transform", "Position", position},
                     SetFieldCommand{guid, "Transform", "Rotation", glm::vec3(r[0], r[1], r[2])},
                     SetFieldCommand{guid, "Transform", "Scale", scale}},
                    std::move(options));
            }

            return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
        }

        void PickEntityUnderMouse(ImVec2 min, ImVec2 max, const SimBinding& sim)
        {
            const glm::vec2 viewportMin{min.x, min.y};
            const glm::vec2 viewportSize{max.x - min.x, max.y - min.y};
            if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) return;

            const auto sceneLock = sim.Runner->AcquireSnapshot();
            const Scene& scene = *sceneLock;
            const RenderFrame frame = RenderFrame::ForScene(scene, m_RenderSettings->Effective().LengthUnit,
                                                            m_Camera.GetPosition());
            const auto mousePos = ImGui::GetMousePos();
            const auto ray = ScreenPointToRay({mousePos.x, mousePos.y}, viewportMin, viewportSize,
                                              m_Camera.GetViewMatrix(frame), m_Camera.GetProjectionMatrix(),
                                              m_Camera.GetPosition(), frame);
            const auto radiusOverride = MeshRadiusOverride();
            if (const auto hit = PickClosestEntity(scene, ray.OriginM, ray.Direction, radiusOverride))
            {
                m_UiState->SelectedEntityGuid = scene.Registry().get<MetadataComponent>(hit->Entity).Guid;
            }
            else
            {
                m_UiState->SelectedEntityGuid = 0;
            }
        }

        [[nodiscard]] BoundingRadiusOverride MeshRadiusOverride() const
        {
            return [this](const Scene& s, entt::entity entity) -> std::optional<double>
            {
                const auto* mesh = s.Registry().try_get<const MeshComponent>(entity);
                if (!mesh || mesh->AssetPath.empty()) return std::nullopt;
                const double localRadius = static_cast<double>(m_Meshes->BoundingRadius(mesh->AssetPath)) * mesh->MetresPerUnit;
                if (const auto* transform = s.Registry().try_get<const TransformComponent>(entity))
                {
                    return localRadius * static_cast<double>(std::max({transform->Scale.x, transform->Scale.y, transform->Scale.z}));
                }
                return localRadius;
            };
        }

        void FixupPendingConvexHulls()
        {
            const auto sim = m_Sim.Load();
            if (!sim->Runner) return;
            std::vector<std::pair<std::uint64_t, std::string>> pending;
            {
                const auto sceneLock = sim->Runner->AcquireSnapshot();
                for (auto [entity, mesh, collider, metadata] :
                     sceneLock->Registry()
                         .view<const MeshComponent, const ColliderComponent, const MetadataComponent>()
                         .each())
                {
                    if (collider.Shape == ColliderShape::ConvexMesh && collider.ConvexHullPoints.empty()
                        && !mesh.AssetPath.empty())
                    {
                        pending.emplace_back(metadata.Guid, mesh.AssetPath);
                    }
                }
            }
            for (const auto& [guid, assetPath] : pending)
            {
                const auto gltfScene = m_Meshes->GetOrLoad(assetPath);
                if (!gltfScene || gltfScene->Geometry.Vertices.empty()) continue;
                auto points = gltfScene->Geometry.Vertices;
                sim->Runner->EnqueueSystemEdit([guid, points = std::move(points)](Scene& scene)
                {
                    const auto entity = scene.FindByGuid(guid);
                    if (scene.IsValid(entity) && scene.Registry().all_of<ColliderComponent>(entity))
                    {
                        scene.Registry().get<ColliderComponent>(entity).ConvexHullPoints = points;
                    }
                });
            }
        }

        [[nodiscard]] std::string StartupScenePath() const
        {
            static constexpr auto kDefaultScenePath = "templates/test-scene.yaml";
            // Explicit --scene (or export request) wins; otherwise resume the last session's
            // scene if one was persisted; otherwise fall back to the bundled default.
            return m_UiState->StartupScenePath.value_or(
                [this]
                {
                    const auto lastScene = m_AppState->GetLastScenePath();
                    return lastScene.empty() ? std::string(kDefaultScenePath) : lastScene;
                }());
        }

        bool BeginSceneLoad(std::string path, const SceneLoader::Result::Kind kind)
        {
            return m_Loader.Begin(std::move(path), kind);
        }

        void CreateDefaultScene(const std::string& name, const std::string& entityName)
        {
            PopulateDefaultScene(m_Scenes->CreateScene(name), entityName);
        }

        void PollSceneLoad()
        {
            const auto result = m_Loader.Poll();
            if (!result) return;

            using Kind = SceneLoader::Result::Kind;
            const auto recovered = result->Ok && result->LoadKind == Kind::Startup && m_UiState->RecoveredFrom
                                           && m_UiState->RecoveredFrom->File == result->Path
                                       ? m_UiState->RecoveredFrom
                                       : std::nullopt;
            if (result->Ok)
            {
                CommitCamera();
                if (result->LoadKind == Kind::Template)
                {
                    m_UiState->CurrentScenePath.clear();
                }
                else if (recovered)
                {
                    m_UiState->CurrentScenePath = recovered->ScenePath;
                }
                else
                {
                    m_UiState->CurrentScenePath = result->Path;
                    m_AppState->NotifySceneOpened(result->Path);
                }
                if (result->LoadKind == Kind::Startup)
                {
                    m_Logger->Info("ViewportLayer: loaded scene '{}'", result->SceneName);
                }
                m_Logger->Notifications()->Post(
                    NotificationKind::Success, "Scene loaded",
                    std::format("{} ({} mesh{}, {:.1f} s)", result->SceneName, result->MeshCount,
                                result->MeshCount == 1 ? "" : "es", result->Seconds),
                    0.0f, "scene-load");
                if (!result->FailedMeshes.empty())
                {
                    m_Logger->Notifications()->Post(
                        NotificationKind::Warning, "Meshes failed to load",
                        std::format("{} of {} in {}, first: {}", result->FailedMeshes.size(), result->MeshCount,
                                    result->SceneName, result->FailedMeshes.front()),
                        0.0f, "scene-meshes");
                }
                if (result->WarningCount > 0)
                {
                    m_Logger->Notifications()->Post(
                        NotificationKind::Warning, "Scene warnings",
                        std::format("{} warning{} in {}, see log", result->WarningCount,
                                    result->WarningCount == 1 ? "" : "s", result->SceneName),
                        0.0f, "scene-warnings");
                }
                m_RenderSettings->LoadScene(result->RenderExtension, m_UiState->CurrentScenePath);
                if (const auto cached = m_AppState->LookupSceneCamera(m_UiState->CurrentScenePath))
                {
                    m_UiState->CameraPositionM = cached->PositionM;
                    m_UiState->CameraYaw = cached->Yaw;
                    m_UiState->CameraPitch = cached->Pitch;
                    if (cached->SpeedMs > 0.0) m_UiState->CameraSpeedMs = cached->SpeedMs;
                }
                else if (const auto& view = result->Editor)
                {
                    m_UiState->CameraPositionM = view->PositionM;
                    m_UiState->CameraYaw = view->Yaw;
                    m_UiState->CameraPitch = view->Pitch;
                    m_UiState->CameraFov = view->Fov;
                    if (view->SpeedMs) m_UiState->CameraSpeedMs = *view->SpeedMs;
                }
                m_TrackedPose = CurrentCameraPose();
                for (const auto& warning : m_RenderSettings->Warnings())
                {
                    m_Logger->Warn("ViewportLayer: {}", warning);
                    m_Logger->Notifications()->Post(NotificationKind::Warning, "Render settings", warning, 0.0f,
                                                    "render-warning");
                }
                StartSimulationFor(result->SceneName);
                if (recovered) AdoptRecovery(*recovered);
            }
            else if (result->LoadKind == Kind::Startup)
            {
                m_Logger->Warn("ViewportLayer: failed to load '{}' ({}), creating a default scene", result->Path,
                               result->Error);
                m_Logger->Notifications()->Post(NotificationKind::Warning, "Scene failed to load",
                                                std::format("{}: {}\nStarted with a default scene", result->Path, result->Error),
                                                0.0f, "scene-load");
                CreateDefaultScene("ViewportDefault", "BlackHole");
                m_UiState->CurrentScenePath.clear();
                m_RenderSettings->LoadScene({});
                StartSimulationFor("ViewportDefault");
            }
            else
            {
                m_Logger->Error("ViewportLayer: failed to load scene '{}': {}", result->Path, result->Error);
                m_Logger->Notifications()->Post(NotificationKind::Error, "Scene failed to load",
                                                std::format("{}: {}", result->Path, result->Error), 0.0f, "scene-load");
            }
        }

        void AdoptRecovery(const RecoveredScene& recovered)
        {
            m_UiState->SceneState.MarkSaved(m_UiState->SceneState.LiveRevision(), false);
            if (const auto adopted = m_UiState->Recovery.Adopt(recovered.File); !adopted)
                m_Logger->Warn("ViewportLayer: could not adopt recovery copy: {}", adopted.error());
            m_Logger->Notifications()->Post(NotificationKind::Info, "Unsaved changes restored",
                                            recovered.ScenePath.empty() ? recovered.SceneName : recovered.ScenePath,
                                            0.0f, "scene-recovery");
        }

        void ReportInitErrors(SimulationRunner& runner)
        {
            for (const auto& error : runner.TakeInitErrors())
            {
                m_Logger->Notifications()->Post(NotificationKind::Error, "Simulation failed to start",
                                                std::format("{}: {}", m_UiState->CurrentSceneName, error), 0.0f,
                                                "sim-init");
            }
        }

        void StartSimulationFor(const std::string& sceneName)
        {
            if (const auto previous = m_Sim.Load(); previous->Runner && !m_UiState->CurrentSceneName.empty())
            {
                // Joined on a worker: the old simulation may be in the middle of a long tick.
                m_Scenes->DestroySimulationAsync(m_UiState->CurrentSceneName);
            }

            auto physics = std::make_shared<PhysicsSimulationModule>(m_Dispatcher, m_Logger);
            auto gravity = std::make_shared<GravitySimulationModule>(physics, m_Dispatcher, m_Logger);
            gravity->SetGravityMultiplier(m_UiState->GravityMultiplier);
            SimulationOptions options;
            options.FixedTimestep = std::chrono::duration<float>(1.0f / std::max(m_UiState->SimulationTickRate, 1.0f));
            auto geodesics = std::make_shared<GeodesicSimulationModule>(m_Logger);
            auto rotation = std::make_shared<BodyRotationModule>();
            auto nbody = std::make_shared<NBodySimulationModule>(m_Logger);
            auto runner = m_Scenes->CreateSimulation(
                sceneName, std::vector<std::shared_ptr<ISimulationModule>>{gravity, physics, geodesics, nbody, rotation},
                options);
            runner->Start();
            runner->SetPaused(true);
            m_Sim.Publish(SimBinding{runner});
            m_UiState->SceneState.Reset(runner->AcquireSnapshot().ContentRevision());
            m_UiState->CurrentSceneName = sceneName;
            m_UiState->SelectedEntityGuid = 0;
            m_Pipeline.ClearObjectPaths();
        }

        void CheckPendingSceneSwitch()
        {
            if (m_UiState->PendingNewScene.exchange(false))
            {
                const auto name = "Untitled-" + std::to_string(++m_SceneCounter);
                CommitCamera();
                CreateDefaultScene(name, "Black Hole");
                m_UiState->CurrentScenePath.clear();
                m_RenderSettings->LoadScene({});
                StartSimulationFor(name);
            }
            else if (auto path = m_UiState->PendingLoadScenePath.Take())
            {
                if (!BeginSceneLoad(*path, SceneLoader::Result::Kind::File))
                    m_UiState->PendingLoadScenePath.Restore(std::move(*path));
            }
            else if (auto path = m_UiState->PendingLoadTemplatePath.Take())
            {
                if (!BeginSceneLoad(*path, SceneLoader::Result::Kind::Template))
                    m_UiState->PendingLoadTemplatePath.Restore(std::move(*path));
            }

            const auto sim = m_Sim.Load();
            if (m_UiState->PendingSnapshotForPlay.exchange(false) && sim->Runner)
            {
                if (sim->Play->Resume(sim->Runner)) m_Pipeline.ClearObjectPaths();
            }
            for (const auto& request : m_UiState->Transport.Take())
            {
                if (sim->Runner) ApplyTransportRequest(*sim, request);
            }
            m_UiState->PlaySessionActive = sim->Runner && sim->Play->Active();

            if (m_UiState->PendingStopSimulation.exchange(false) && sim->Runner)
            {
                m_RenderSettings->ClearRuntimeOverrides();
                // Render settings edited while playing are kept; everything else returns to the pre-play scene
                sim->Play->Stop(sim->Runner, m_RenderSettings->PersistsToScene()
                                                 ? std::optional(m_RenderSettings->SceneYaml())
                                                 : std::nullopt);
            }
            m_UiState->Bookmarks.Set(sim->Runner ? sim->Play->Bookmarks() : std::vector<BookmarkInfo>{});
        }

        void ApplyTransportRequest(const SimBinding& sim, const TransportRequest& request)
        {
            switch (request.What)
            {
            case TransportRequest::Kind::Step:
                if (!sim.Runner->IsPaused()) break;
                if (!sim.Play->Active() && sim.Play->Resume(sim.Runner, false)) m_Pipeline.ClearObjectPaths();
                sim.Runner->Step(request.Ticks);
                break;
            case TransportRequest::Kind::Snapshot:
                if (!sim.Play->Active() && sim.Play->Resume(sim.Runner, false)) m_Pipeline.ClearObjectPaths();
                sim.Play->Capture(sim.Runner);
                break;
            case TransportRequest::Kind::Restore: sim.Play->Restore(sim.Runner, request.Index); break;
            }
        }

        // Lowers the tracer resolution while the frame time is over the profile's budget; interactive contexts only
        void UpdateGovernor(const RenderContext context)
        {
            const auto& settings = m_RenderSettings->Effective();
            const bool active = settings.FrameGovernor && (context == RenderContext::Edit || context == RenderContext::Play);
            if (m_LastStep.Rung > 0) m_Governor.Hold(std::chrono::steady_clock::now() + std::chrono::milliseconds(700));
            const auto stats = active ? m_Renderer->GetBufferTargetStats(kViewportBufferId) : std::nullopt;
            const float previous = m_UiState->RenderGovernorScale;
            m_UiState->RenderGovernorScale = m_Governor.Update(active && stats && stats->Visible,
                                                               stats ? stats->AverageFrameMs : 0.0, settings.FrameBudgetMs);
            if (m_UiState->RenderGovernorScale != previous)
                m_Logger->Trace("Governor: {:.0f}% -> {:.0f}% (frame {:.1f} ms, budget {:.1f} ms)", previous * 100.0f,
                                m_UiState->RenderGovernorScale * 100.0f, stats ? stats->AverageFrameMs : 0.0, settings.FrameBudgetMs);
        }

        LadderStep StepRefinement(const RenderSettings& settings, const std::uint64_t sceneKey)
        {
            const auto now = std::chrono::steady_clock::now();
            SignatureHasher still;
            still.Value(m_Camera.GetPosition());
            still.Value(m_Camera.GetFront());
            still.Value(m_Camera.GetUp());
            still.Value(m_Camera.GetFov());
            still.Pointer(m_RenderSettings->Snapshot().get());
            still.Value(m_ObserverGuid);
            still.Value(sceneKey);
            still.Value(m_ActiveContext);
            if (still.Digest() != std::exchange(m_StillHash, still.Digest())) m_StillSince = now;

            const bool enabled = settings.FrameGovernor && m_ActiveContext == RenderContext::Edit;
            std::uint64_t frames = 0;
            if (enabled)
            {
                if (const auto stats = m_Renderer->GetBufferTargetStats(kViewportBufferId)) frames = stats->FramesRendered;
            }
            m_LastStep = m_Ladder.Next({.Enabled = enabled, .InteractiveScale = m_UiState->RenderGovernorScale,
                                        .StillMs = std::chrono::duration<double, std::milli>(now - m_StillSince).count(),
                                        .FramesDone = frames});
            return m_LastStep;
        }

        void LogRefinement(const LadderStep& step)
        {
            auto status = m_Pipeline.Refinement();
            status.Idle = status.Idle && status.Samples > 0;
            const bool checkpoint = status.Samples != m_LoggedSamples && (status.Samples % 16 == 0 || status.Samples == status.MaxSamples);
            if (step.Rung == m_LoggedRung && step.Scale == m_LoggedScale && status.Idle == m_LoggedIdle && !checkpoint) return;
            m_LoggedRung = step.Rung;
            m_LoggedScale = step.Scale;
            m_LoggedIdle = status.Idle;
            m_LoggedSamples = status.Samples;
            m_Logger->Trace("Refinement: rung {} scale {:.0f}% samples {}/{}{}", step.Rung, step.Scale * 100.0f, status.Samples,
                            status.MaxSamples, status.Idle ? " idle" : "");
        }

        void RequestFrameOnChange(const std::uint64_t sceneKey, const bool interactive, const LadderStep& step)
        {
            SignatureHasher inputs;
            inputs.Value(m_Camera.GetPosition());
            inputs.Value(m_Camera.GetFront());
            inputs.Value(m_Camera.GetUp());
            inputs.Value(m_Camera.GetFov());
            inputs.Pointer(m_RenderSettings->Snapshot().get());
            inputs.Value(step.Scale);
            inputs.Value(step.Accumulate);
            inputs.Value(m_ObserverGuid);
            inputs.Value(sceneKey);
            inputs.Value(interactive);
            if (inputs.Digest() == std::exchange(m_LastInputs, inputs.Digest())) return;
            m_Renderer->RequestViewportFrame();
        }

        // The profile for what the viewport is doing, scripts' overrides, and the mirror of edits into the scene
        void UpdateRenderSettings()
        {
            const auto sim = m_Sim.Load();
            auto context = RenderContext::Edit;
            if (m_UiState->ExportActive) context = RenderContext::Export;
            else if (m_UiState->ReferenceView) context = RenderContext::Reference;
            else if (sim->Runner && !sim->Runner->IsPaused()) context = RenderContext::Play;
            m_ActiveContext = context;
            m_RenderSettings->SetActiveContext(context);
            UpdateGovernor(context);

            std::vector<std::string> errors;
            m_RenderSettings->DrainPosted(&errors);
            for (const auto& error : errors) m_Logger->Warn("render.set: {}", error);

            using namespace std::chrono_literals;
            const auto now = std::chrono::steady_clock::now();
            if (sim->Runner && m_RenderSettings->SceneWritePending() && now - m_LastRenderWrite >= 200ms)
            {
                if (auto text = m_RenderSettings->TakeSceneWrite())
                {
                    sim->Runner->EnqueueEdit([text = std::move(*text)](Scene& scene) { StoreRenderExtension(scene, text); });
                    m_LastRenderWrite = now;
                }
            }
        }

        [[nodiscard]] SceneCameraPose CurrentCameraPose() const
        {
            return {m_UiState->CameraPositionM, m_UiState->CameraYaw, m_UiState->CameraPitch, m_UiState->CameraSpeedMs};
        }

        void CommitCamera()
        {
            if (!m_CameraPending) return;
            m_CameraPending = false;
            if (m_UiState->CurrentScenePath.empty()) return;
            m_AppState->StoreSceneCamera(m_UiState->CurrentScenePath, CurrentCameraPose());
            m_AppState->FlushSceneCameras();
        }

        void TrackCamera()
        {
            if (m_UiState->IntroActive || m_UiState->ExportActive || m_UiState->SceneCameraActive) return;
            const auto pose = CurrentCameraPose();
            const auto now = std::chrono::steady_clock::now();
            if (!m_TrackedPose || !m_TrackedPose->SamePose(pose))
            {
                if (m_TrackedPose)
                {
                    m_CameraPending = true;
                    m_CameraChangedAt = now;
                }
                m_TrackedPose = pose;
            }
            if (m_CameraPending && now - m_CameraChangedAt >= std::chrono::seconds(2)) CommitCamera();
        }

        void UpdateCamera(float deltaTime)
        {
            if (m_UiState->IntroActive) return;

            if (UpdateSceneCamera()) return;

            m_Camera.SetPosition(m_UiState->CameraPositionM);
            m_Camera.SetYawPitch(m_UiState->CameraYaw, m_UiState->CameraPitch);
            m_Camera.SetFov(m_UiState->CameraFov);
            m_Camera.SetClipPlanes(0.1f, 10000.0f);

            if (m_FocusRequest.exchange(false, std::memory_order_relaxed)) BeginFocus();
            const float scroll = m_PendingScroll.exchange(0.0f, std::memory_order_relaxed);

            const bool keysEnabled = m_CameraKeysEnabled.load(std::memory_order_relaxed);
            float forward = 0.0f, right = 0.0f, up = 0.0f;
            if (keysEnabled)
            {
                if (m_Input->IsKeyDown(KeyCode::W)) forward += 1.0f;
                if (m_Input->IsKeyDown(KeyCode::S)) forward -= 1.0f;
                if (m_Input->IsKeyDown(KeyCode::D)) right += 1.0f;
                if (m_Input->IsKeyDown(KeyCode::A)) right -= 1.0f;
                if (m_Input->IsKeyDown(KeyCode::E)) up += 1.0f;
                if (m_Input->IsKeyDown(KeyCode::Q)) up -= 1.0f;
            }
            if (m_FocusGuid != 0 && (forward != 0.0f || right != 0.0f || up != 0.0f)) m_FocusGuid = 0;
            if (m_FocusGuid != 0 && scroll != 0.0f)
            {
                m_FocusDistance = std::clamp(m_FocusDistance * std::pow(0.9, static_cast<double>(scroll)), m_FocusMinDistance, 1.0e30);
            }
            if (forward != 0.0f || right != 0.0f || up != 0.0f)
            {
                m_Camera.ProcessKeyboard(forward, right, up, deltaTime, m_UiState->CameraSpeedMs);
            }

            const float mouseX = m_Input->MouseX();
            const float mouseY = m_Input->MouseY();
            const bool rightDown = m_Input->IsMouseButtonDown(MouseButton::Right);
            if (!rightDown) m_MouseLookActive = false;
            else if (!m_MouseLookActive && m_CameraMouseHover.load(std::memory_order_relaxed)) m_MouseLookActive = true;
            if (m_MouseLookActive)
            {
                const bool orbit = m_FocusGuid != 0 && keysEnabled &&
                                   (m_Input->IsKeyDown(KeyCode::LeftAlt) || m_Input->IsKeyDown(KeyCode::RightAlt));
                if (m_FocusGuid != 0 && !orbit) m_FocusGuid = 0;
                if (m_HasLastMouse)
                {
                    const float dx = mouseX - m_LastMouseX;
                    const float dy = m_LastMouseY - mouseY;
                    m_Camera.ProcessMouse(dx, dy, m_UiState->CameraMouseSensitivity);
                }
                m_HasLastMouse = true;
            }
            else
            {
                m_HasLastMouse = false;
            }
            m_LastMouseX = mouseX;
            m_LastMouseY = mouseY;

            if (m_FocusGuid != 0) UpdateFocus();

            m_UiState->CameraPositionM = m_Camera.GetPosition();
            m_UiState->CameraYaw = m_Camera.GetYaw();
            m_UiState->CameraPitch = m_Camera.GetPitch();
            PublishView();
        }

        std::optional<std::pair<glm::dvec3, double>> FindFocusTarget(std::uint64_t guid)
        {
            const auto sim = m_Sim.Load();
            if (!sim->Runner || guid == 0) return std::nullopt;
            const auto sceneLock = sim->Runner->AcquireSnapshot();
            const Scene& scene = *sceneLock;
            const auto entity = scene.FindByGuid(guid);
            if (!scene.IsValid(entity)) return std::nullopt;
            const auto* transform = scene.Registry().try_get<const TransformComponent>(entity);
            if (!transform) return std::nullopt;

            const double radius = BoundingRadiusOf(scene, entity, MeshRadiusOverride());
            return std::pair{transform->Position, std::max(radius, 0.01)};
        }

        void BeginFocus()
        {
            const auto target = FindFocusTarget(m_UiState->SelectedEntityGuid);
            if (!target) return;
            m_FocusGuid = m_UiState->SelectedEntityGuid;
            const double halfFov = static_cast<double>(glm::radians(m_Camera.GetFov())) * 0.5;
            m_FocusDistance = target->second / std::sin(halfFov) * 1.5;
            m_FocusMinDistance = target->second * 0.5;
        }

        void UpdateFocus()
        {
            const auto target = FindFocusTarget(m_FocusGuid);
            if (!target)
            {
                m_FocusGuid = 0;
                return;
            }
            m_Camera.SetPosition(target->first - glm::dvec3(m_Camera.GetFront()) * m_FocusDistance);
        }

        bool UpdateSceneCamera()
        {
            m_UiState->SceneCameraActive = false;
            m_ObserverGuid = 0;
            const auto sim = m_Sim.Load();
            if (!sim->Runner) return false;
            const bool playing = !sim->Runner->IsPaused();
            const bool possess = m_UiState->PossessSceneCamera || m_UiState->ExportActive;
            if (!m_UiState->PreviewSceneCamera && !(playing && possess)) return false;

            const auto snapshot = sim->Runner->AcquireSnapshot();
            if (!snapshot) return false;
            const auto view = FindPrimarySceneCamera(*snapshot);
            if (!view) return false;

            const RenderFrame frame = RenderFrame::ForScene(*snapshot, m_RenderSettings->Effective().LengthUnit,
                                                            view->PositionM);
            m_Camera.SetView(view->PositionM, view->Front, view->Up);
            m_Camera.SetFov(view->Fov);
            m_Camera.SetClipPlanes(frame.ToRenderLength(view->NearPlaneM), frame.ToRenderLength(view->FarPlaneM));
            m_HasLastMouse = false;
            m_UiState->SceneCameraActive = true;
            m_ObserverGuid = view->Guid;
            PublishView();
            return true;
        }

        void RecordPaths(const bool shown, const bool playing, const std::uint64_t sceneKey, const Scene& scene)
        {
            constexpr auto kMinInterval = std::chrono::milliseconds(15);
            const auto now = std::chrono::steady_clock::now();
            if (!shown)
            {
                if (std::exchange(m_PathsRecording, false)) m_Pipeline.ClearObjectPaths();
                return;
            }
            if (!playing) return;
            if (m_PathsRecording && (sceneKey == m_LastPathKey || now - m_LastPathTime < kMinInterval)) return;
            m_PathsRecording = true;
            m_LastPathKey = sceneKey;
            m_LastPathTime = now;
            m_Pipeline.RecordObjectPaths(scene);
        }

        // What the camera's observer measures, for the camera window; the renderer applies the same observer.
        void PublishObserverReadout(const RenderSettings& render, const std::uint64_t sceneKey)
        {
            GPP::ProfileZone zone("Observer.Readout");
            const auto sim = m_Sim.Load();
            if (!sim->Runner) return;
            SignatureHasher key;
            key.Value(sceneKey);
            key.Value(render.Solution);
            key.Value(render.Observer);
            key.Value(render.ObserverOrbit);
            key.Value(render.TransportMode);
            key.Value(m_UiState->ViewPositionM);
            key.Value(m_ObserverGuid);
            const auto digest = key.Digest();
            if (m_ReadoutKeyValid && digest == m_ReadoutKey) return;
            m_ReadoutKey = digest;
            m_ReadoutKeyValid = true;
            const auto snapshot = sim->Runner->AcquireSnapshot();
            auto readout = ComputeObserverReadout(
                *snapshot, static_cast<std::int32_t>(render.Solution), static_cast<ObserverKind>(render.Observer),
                static_cast<OrbitDirection>(render.ObserverOrbit), m_UiState->ViewPositionM, m_ObserverGuid);
            readout.Rendered = render.TransportMode == Transport::Integrated;
            m_UiState->Observer.Publish(std::move(readout));
        }

        void PublishView()
        {
            m_UiState->ViewPositionM = m_Camera.GetPosition();
            m_UiState->ViewFront = m_Camera.GetFront();
            m_UiState->ViewUp = m_Camera.GetUp();
            m_UiState->ViewFov = m_Camera.GetFov();
        }

        std::shared_ptr<Renderer> m_Renderer;
        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<InputState> m_Input;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;
        std::optional<SceneCameraPose> m_TrackedPose;
        bool m_CameraPending = false;
        std::chrono::steady_clock::time_point m_CameraChangedAt;
        std::shared_ptr<RenderSettingsService> m_RenderSettings;
        std::shared_ptr<MeshCache> m_Meshes;
        SceneLoader m_Loader;
        std::chrono::steady_clock::time_point m_AttachTime;
        bool m_ShadersReady = false;
        RenderPipeline m_Pipeline;
        ExportController m_Export;
        std::shared_ptr<EventDispatcher> m_Dispatcher;

        LatestValue<ViewportFrameParams> m_FrameParams; // UI -> viewport: camera + render settings
        LatestValue<SimBinding> m_Sim;                  // UI -> viewport: which simulation to draw

        // ---- UI thread only ----
        std::chrono::steady_clock::time_point m_LastRenderWrite;
        FrameGovernor m_Governor;
        int m_SceneCounter{0};
        Camera m_Camera;
        bool m_HasLastMouse{false};
        bool m_MouseLookActive{false};
        std::uint64_t m_FocusGuid{0};
        std::uint64_t m_ObserverGuid{0}; // possessed scene camera entity while it is active
        std::uint64_t m_LastInputs{0};
        std::uint64_t m_LastPathKey{0};
        std::chrono::steady_clock::time_point m_LastPathTime;
        bool m_PathsRecording{false};
        std::uint64_t m_ReadoutKey{0};
        bool m_ReadoutKeyValid{false};
        std::uint64_t m_StillHash{0};
        std::chrono::steady_clock::time_point m_StillSince;
        RefinementLadder m_Ladder;
        LadderStep m_LastStep;
        int m_LoggedRung{-1};
        float m_LoggedScale{0.0f};
        bool m_LoggedIdle{false};
        std::uint32_t m_LoggedSamples{0};
        RenderContext m_ActiveContext{RenderContext::Edit};
        double m_FocusDistance{10.0};
        double m_FocusMinDistance{0.1};
        std::atomic<bool> m_FocusRequest{false};
        std::atomic<float> m_PendingScroll{0.0f};
        std::atomic<bool> m_CameraKeysEnabled{false};
        std::atomic<bool> m_CameraMouseHover{false};
        float m_LastMouseX{0.0f};
        float m_LastMouseY{0.0f};
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(ViewportLayer)
