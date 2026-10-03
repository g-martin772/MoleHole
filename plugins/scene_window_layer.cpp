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
        using Dependencies = std::tuple<Logger, SceneManager, UiState>;

        SceneWindowLayer(const std::shared_ptr<Logger>& logger, std::shared_ptr<SceneManager> scenes,
                         std::shared_ptr<UiState> uiState)
            : HotReloadableLayer(logger), m_Scenes(std::move(scenes)), m_UiState(std::move(uiState))
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

            {
                auto sceneLock = runner->LockRenderScene();
                RenderEntityList(*runner, *sceneLock);
                RenderSelectedEntity(*runner, *sceneLock);
            }

            ImGui::End();
        }

    private:

        void RenderEntityList(SimulationRunner& runner, Scene& scene)
        {
            ImGui::BeginChild("EntityList", ImVec2(0, 160), true);
            for (auto [entity, metadata] : scene.Registry().view<const MetadataComponent>().each())
            {
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

        void RenderSelectedEntity(SimulationRunner& runner, Scene& scene)
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
                float mass = body->Mass;
                if (ImGui::DragFloat("Mass (kg)", &mass, 1.0e22f, 0.0f, 0.0f, "%.3e"))
                    EditField(runner, guid, &RigidBodyComponent::Mass, mass);
                bool enableGravity = body->EnableGravity;
                if (ImGui::Checkbox("Enable Gravity", &enableGravity))
                    EditField(runner, guid, &RigidBodyComponent::EnableGravity, enableGravity);
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
    };
}

GPP_DEFINE_HOT_RELOAD_LAYER(SceneWindowLayer)
