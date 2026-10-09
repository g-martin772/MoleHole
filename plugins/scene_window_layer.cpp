import GPP;
import MoleHole;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    template <typename Component, typename Field>
    void EditField(SimulationRunner& runner, std::uint64_t guid, Field Component::* member, Field newValue)
    {
        runner.EnqueueEdit([guid, member, newValue](Scene& scene)
        {
            const auto entity = scene.FindByGuid(guid);
            if (scene.IsValid(entity) && scene.Registry().all_of<Component>(entity))
            {
                scene.Registry().get<Component>(entity).*member = newValue;
            }
        });
    }

    struct SceneWindowLayer final : public HotReloadableLayer
    {
        using Dependencies = std::tuple<Logger, SceneManager, UiState, AppStateService, IFileSystem, FileDialog>;

        SceneWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                         std::shared_ptr<UiState> uiState, std::shared_ptr<AppStateService> appState,
                         std::shared_ptr<IFileSystem> fileSystem, std::shared_ptr<FileDialog> fileDialog)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState)),
              m_AppState(std::move(appState)), m_FileSystem(std::move(fileSystem)),
              m_FileDialog(std::move(fileDialog))
        {
        }

        void OnUiRender() override
        {
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

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s",
                               m_UiState->CurrentScenePath.empty() ? "(unsaved)"
                                                                   : m_UiState->CurrentScenePath.c_str());
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                               runner->IsPaused() ? "Paused (see viewport controls)" : "Running");

            SectionHeader("SIMULATION");
            ImGui::SliderFloat("Gravity Strength", &m_UiState->GravityMultiplier, 0.0f, 10.0f, "%.2fx");

            SectionHeader("ENTITIES");
            if (ImGui::Button("Add Black Hole"))
            {
                runner->EnqueueEdit([](Scene& scene)
                {
                    const auto entity = scene.CreateEntity("Black Hole", "BlackHole");
                    scene.Registry().emplace<TransformComponent>(
                        entity, TransformComponent{.Position = {0.0f, 0.0f, -10.0f}});
                    scene.Registry().emplace<BlackHoleComponent>(entity,
                                                                 BlackHoleComponent{.Mass = 1.0f, .Spin = 0.3f});
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Sphere"))
            {
                runner->EnqueueEdit([](Scene& scene)
                {
                    const auto entity = scene.CreateEntity("Sphere", "Sphere");
                    scene.Registry().emplace<TransformComponent>(
                        entity, TransformComponent{.Position = {5.0f, 0.0f, 0.0f}});
                    scene.Registry().emplace<SphereComponent>(
                        entity, SphereComponent{.Radius = 0.5f, .Color = {0.8f, 0.8f, 0.9f}});
                    scene.Registry().emplace<RigidBodyComponent>(
                        entity, RigidBodyComponent{.Type = RigidBodyType::Dynamic, .Mass = 5.972e24f,
                                                   .EnableGravity = true});
                    scene.Registry().emplace<ColliderComponent>(
                        entity, ColliderComponent{.Shape = ColliderShape::Sphere, .Radius = 0.5f});
                });
            }
            ImGui::SameLine();
            RenderAddMeshPopup(*runner);

            {
                auto sceneLock = runner->LockRenderScene();
                RenderEntityList(*runner, *sceneLock);
                RenderSelectedEntity(*runner, *sceneLock);
            }

            ImGui::End();
        }

    private:

        void RenderEntityList(SimulationRunner& runner, const Scene& scene)
        {
            ImGui::BeginChild("EntityList", ImVec2(0, 160), true);
            for (auto [entity, metadata] : scene.Registry().view<const MetadataComponent>().each())
            {
                // not a real scene object, not shown/selectable/deletable through entity list
                if (metadata.TypeTag == kAnimationGraphDataTypeTag) continue;

                const std::string label = metadata.Name.empty()
                                              ? (metadata.TypeTag + " #" + std::to_string(metadata.Guid))
                                              : metadata.Name;
                ImGui::PushID(static_cast<int>(metadata.Guid));
                const bool isSelected = m_UiState->SelectedEntityGuid == metadata.Guid;
                const float avail = ImGui::GetContentRegionAvail().x;
                if (ImGui::Selectable(label.c_str(), isSelected, 0, ImVec2(avail - 28.0f, 0)))
                {
                    m_UiState->SelectedEntityGuid = isSelected ? 0 : metadata.Guid;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("X"))
                {
                    const auto guid = metadata.Guid;
                    runner.EnqueueEdit([guid](Scene& s)
                    {
                        const auto e = s.FindByGuid(guid);
                        if (s.IsValid(e)) s.DestroyEntity(e);
                    });
                    if (isSelected) m_UiState->SelectedEntityGuid = 0;
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        void RenderAddMeshPopup(SimulationRunner& runner)
        {
            if (ImGui::Button("Add Mesh"))
            {
                std::ranges::fill(m_MeshPathBuffer, '\0');
                ImGui::OpenPopup("Add Mesh");
            }

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

        void RenderSelectedEntity(SimulationRunner& runner, const Scene& scene)
        {
            if (m_UiState->SelectedEntityGuid == 0) return;
            const auto entity = scene.FindByGuid(m_UiState->SelectedEntityGuid);
            if (!scene.IsValid(entity))
            {
                m_UiState->SelectedEntityGuid = 0;
                return;
            }
            const auto guid = m_UiState->SelectedEntityGuid;

            SectionHeader("SELECTED ENTITY");

            if (const auto* transform = scene.Registry().try_get<const TransformComponent>(entity))
            {
                glm::vec3 pos = transform->Position;
                if (ImGui::DragFloat3("Position", &pos.x, 0.1f))
                    EditField(runner, guid, &TransformComponent::Position, pos);
            }

            if (const auto* blackHole = scene.Registry().try_get<const BlackHoleComponent>(entity))
            {
                float mass = blackHole->Mass;
                if (ImGui::DragFloat("Mass (solar)", &mass, 0.01f, 0.0f, 1000.0f))
                    EditField(runner, guid, &BlackHoleComponent::Mass, mass);
                float spin = blackHole->Spin;
                if (ImGui::SliderFloat("Spin", &spin, 0.0f, 1.0f))
                    EditField(runner, guid, &BlackHoleComponent::Spin, spin);
                float charge = blackHole->Charge;
                if (ImGui::SliderFloat("Charge", &charge, 0.0f, 1.0f))
                    EditField(runner, guid, &BlackHoleComponent::Charge, charge);
                glm::vec3 spinAxis = blackHole->SpinAxis;
                if (ImGui::DragFloat3("Spin Axis", &spinAxis.x, 0.01f))
                    EditField(runner, guid, &BlackHoleComponent::SpinAxis, spinAxis);
            }

            if (const auto* sphere = scene.Registry().try_get<const SphereComponent>(entity))
            {
                float radius = sphere->Radius;
                if (ImGui::DragFloat("Radius", &radius, 0.01f, 0.01f, 100.0f))
                    EditField(runner, guid, &SphereComponent::Radius, radius);
                glm::vec3 color = sphere->Color;
                if (ImGui::ColorEdit3("Color", &color.x))
                    EditField(runner, guid, &SphereComponent::Color, color);
                float spin = sphere->Spin;
                if (ImGui::SliderFloat("Sphere Spin", &spin, 0.0f, 1.0f))
                    EditField(runner, guid, &SphereComponent::Spin, spin);
            }

            if (const auto* body = scene.Registry().try_get<const RigidBodyComponent>(entity))
            {
                SectionHeader("RIGID BODY");
                static constexpr std::array kBodyTypeNames{"Static", "Kinematic", "Dynamic"};
                int typeIndex = static_cast<int>(body->Type);
                if (ImGui::Combo("Type", &typeIndex, kBodyTypeNames.data(),
                                 static_cast<int>(kBodyTypeNames.size())))
                    EditField(runner, guid, &RigidBodyComponent::Type, static_cast<RigidBodyType>(typeIndex));
                float mass = body->Mass;
                if (ImGui::DragFloat("Mass (kg)", &mass, 1.0e22f, 0.0f, 0.0f, "%.3e"))
                    EditField(runner, guid, &RigidBodyComponent::Mass, mass);
                float linearDamping = body->LinearDamping;
                if (ImGui::DragFloat("Linear Damping", &linearDamping, 0.01f, 0.0f, 10.0f))
                    EditField(runner, guid, &RigidBodyComponent::LinearDamping, linearDamping);
                float angularDamping = body->AngularDamping;
                if (ImGui::DragFloat("Angular Damping", &angularDamping, 0.01f, 0.0f, 10.0f))
                    EditField(runner, guid, &RigidBodyComponent::AngularDamping, angularDamping);
                bool enableGravity = body->EnableGravity;
                if (ImGui::Checkbox("Enable Gravity", &enableGravity))
                    EditField(runner, guid, &RigidBodyComponent::EnableGravity, enableGravity);
            }

            if (const auto* collider = scene.Registry().try_get<const ColliderComponent>(entity))
            {
                SectionHeader("COLLIDER");
                ImGui::TextDisabled("Shape/size changes apply on next scene reload");
                static constexpr std::array kShapeNames{"Box", "Sphere", "Capsule", "Plane", "Convex Mesh"};
                int shapeIndex = static_cast<int>(collider->Shape);
                if (ImGui::Combo("Shape", &shapeIndex, kShapeNames.data(),
                                 static_cast<int>(kShapeNames.size())))
                    EditField(runner, guid, &ColliderComponent::Shape, static_cast<ColliderShape>(shapeIndex));

                switch (collider->Shape)
                {
                case ColliderShape::Box:
                {
                    glm::vec3 halfExtents = collider->HalfExtents;
                    if (ImGui::DragFloat3("Half Extents", &halfExtents.x, 0.01f, 0.01f, 1000.0f))
                        EditField(runner, guid, &ColliderComponent::HalfExtents, halfExtents);
                    break;
                }
                case ColliderShape::Sphere:
                {
                    float radius = collider->Radius;
                    if (ImGui::DragFloat("Radius", &radius, 0.01f, 0.01f, 1000.0f))
                        EditField(runner, guid, &ColliderComponent::Radius, radius);
                    break;
                }
                case ColliderShape::Capsule:
                {
                    float radius = collider->Radius;
                    if (ImGui::DragFloat("Radius", &radius, 0.01f, 0.01f, 1000.0f))
                        EditField(runner, guid, &ColliderComponent::Radius, radius);
                    float halfHeight = collider->HalfHeight;
                    if (ImGui::DragFloat("Half Height", &halfHeight, 0.01f, 0.01f, 1000.0f))
                        EditField(runner, guid, &ColliderComponent::HalfHeight, halfHeight);
                    break;
                }
                case ColliderShape::Plane:
                    break;
                case ColliderShape::ConvexMesh:
                    ImGui::Text("%zu hull points", collider->ConvexHullPoints.size());
                    break;
                }

                float staticFriction = collider->StaticFriction;
                if (ImGui::DragFloat("Static Friction", &staticFriction, 0.01f, 0.0f, 10.0f))
                    EditField(runner, guid, &ColliderComponent::StaticFriction, staticFriction);
                float dynamicFriction = collider->DynamicFriction;
                if (ImGui::DragFloat("Dynamic Friction", &dynamicFriction, 0.01f, 0.0f, 10.0f))
                    EditField(runner, guid, &ColliderComponent::DynamicFriction, dynamicFriction);
                float restitution = collider->Restitution;
                if (ImGui::DragFloat("Restitution", &restitution, 0.01f, 0.0f, 1.0f))
                    EditField(runner, guid, &ColliderComponent::Restitution, restitution);
                bool isTrigger = collider->IsTrigger;
                if (ImGui::Checkbox("Is Trigger", &isTrigger))
                    EditField(runner, guid, &ColliderComponent::IsTrigger, isTrigger);
            }

            if (const auto* velocity = scene.Registry().try_get<const VelocityComponent>(entity))
            {
                SectionHeader("VELOCITY (read-only)");
                glm::vec3 linear = velocity->Linear;
                ImGui::BeginDisabled();
                ImGui::DragFloat3("Linear", &linear.x);
                glm::vec3 angular = velocity->Angular;
                ImGui::DragFloat3("Angular", &angular.x);
                ImGui::EndDisabled();
            }

            ImGui::Spacing();
            if (ImGui::Button("Delete Entity", ImVec2(-1, 0)))
            {
                runner.EnqueueEdit([guid](Scene& s)
                {
                    const auto e = s.FindByGuid(guid);
                    if (s.IsValid(e)) s.DestroyEntity(e);
                });
                m_UiState->SelectedEntityGuid = 0;
            }
        }

        std::shared_ptr<SceneManager> m_Scenes;
        std::shared_ptr<UiState> m_UiState;
        std::shared_ptr<AppStateService> m_AppState;
        std::shared_ptr<IFileSystem> m_FileSystem;
        std::shared_ptr<FileDialog> m_FileDialog;

        std::array<char, 512> m_MeshPathBuffer{};
        bool m_MeshAddCollider = true;
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SceneWindowLayer)
