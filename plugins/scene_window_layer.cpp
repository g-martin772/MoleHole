import GPP;
import MoleHole;
import glm;
import std;

#include <gpp/hot_reload_export.h>

using namespace GPP;
using namespace MoleHole;

namespace
{
    void EditField(SimulationRunner& runner, std::uint64_t guid, std::string component, std::string field,
                   FieldValue value)
    {
        runner.EnqueueTrackedEdit([guid, component = std::move(component), field = std::move(field),
                                   value = std::move(value)](Scene& scene)
        {
            const auto entity = scene.FindByGuid(guid);
            if (!scene.IsValid(entity)) return;
            const auto* info = ComponentRegistry::Instance().FindByName(component);
            if (!info) return;
            for (const auto& candidate : info->Fields)
            {
                if (candidate.Name != field || !candidate.Set) continue;
                if (candidate.Set(scene.Registry(), entity, value)) scene.MarkDirty(entity);
                return;
            }
        });
    }

    std::string SpacedName(const std::string& name)
    {
        std::string result;
        for (std::size_t i = 0; i < name.size(); ++i)
        {
            if (i > 0 && std::isupper(static_cast<unsigned char>(name[i]))) result += ' ';
            result += name[i];
        }
        return result;
    }

    std::string UpperName(std::string name)
    {
        std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return name;
    }

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

    // The single place a descriptor field becomes a widget; returns the edited value when the user changed it.
    std::optional<FieldValue> DrawField(const FieldInfo& field, const FieldValue& current)
    {
        const auto& meta = field.Meta;
        const char* label = meta.Label.c_str();
        const bool bounded = meta.Max > meta.Min;
        const float speed = meta.Speed > 0.0f ? meta.Speed : 0.1f;
        const char* format = meta.Format.empty() ? "%.3f" : meta.Format.c_str();

        ImGui::BeginDisabled(field.ReadOnly());
        std::optional<FieldValue> edited;
        switch (field.Type)
        {
        case FieldType::Bool:
        {
            bool v = std::get<bool>(current);
            if (ImGui::Checkbox(label, &v)) edited = v;
            break;
        }
        case FieldType::Int:
        {
            int v = std::get<int>(current);
            if (ImGui::DragInt(label, &v)) edited = v;
            break;
        }
        case FieldType::Float:
        {
            float v = std::get<float>(current);
            if (ImGui::DragFloat(label, &v, speed, meta.Min, meta.Max, format)) edited = v;
            break;
        }
        case FieldType::Vec2:
        {
            glm::vec2 v = std::get<glm::vec2>(current);
            if (ImGui::DragFloat2(label, &v.x, speed, meta.Min, meta.Max, format)) edited = v;
            break;
        }
        case FieldType::Vec3:
        {
            glm::vec3 v = std::get<glm::vec3>(current);
            const bool changed = meta.Kind == FieldKind::Color
                                     ? ImGui::ColorEdit3(label, &v.x)
                                     : ImGui::DragFloat3(label, &v.x, speed, meta.Min, meta.Max, format);
            if (changed) edited = v;
            break;
        }
        case FieldType::Vec4:
        {
            glm::vec4 v = std::get<glm::vec4>(current);
            const bool changed = meta.Kind == FieldKind::Color
                                     ? ImGui::ColorEdit4(label, &v.x)
                                     : ImGui::DragFloat4(label, &v.x, speed, meta.Min, meta.Max, format);
            if (changed) edited = v;
            break;
        }
        case FieldType::String:
        {
            std::array<char, 512> buffer{};
            const auto& text = std::get<std::string>(current);
            std::ranges::copy(text.substr(0, buffer.size() - 1), buffer.begin());
            const bool changed = meta.Kind == FieldKind::Multiline
                                     ? ImGui::InputTextMultiline(label, buffer.data(), buffer.size())
                                     : ImGui::InputText(label, buffer.data(), buffer.size());
            if (changed) edited = std::string(buffer.data());
            break;
        }
        case FieldType::Entity:
        {
            auto v = static_cast<ImU64>(std::get<std::uint64_t>(current));
            if (ImGui::InputScalar(label, ImGuiDataType_U64, &v)) edited = static_cast<std::uint64_t>(v);
            break;
        }
        case FieldType::Enum:
        {
            int index = std::get<int>(current);
            const auto preview = index >= 0 && index < static_cast<int>(meta.Options.size())
                                     ? SpacedName(meta.Options[index])
                                     : std::string{};
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (int i = 0; i < static_cast<int>(meta.Options.size()); ++i)
                {
                    if (ImGui::Selectable(SpacedName(meta.Options[i]).c_str(), i == index)) edited = i;
                }
                ImGui::EndCombo();
            }
            break;
        }
        }
        ImGui::EndDisabled();
        return edited;
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

        void RenderAddComponentMenu(SimulationRunner& runner, const Scene& scene, entt::entity entity,
                                    std::uint64_t guid)
        {
            ImGui::Spacing();
            if (ImGui::Button("Add Component", ImVec2(-1, 0))) ImGui::OpenPopup("AddComponent");
            if (!ImGui::BeginPopup("AddComponent")) return;

            ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
            {
                if (!info.Inspectable || !info.Add || info.Has(scene.Registry(), entity)) return;
                if (!ImGui::MenuItem(info.DisplayName.c_str())) return;
                runner.EnqueueTrackedEdit([guid, name = info.Name](Scene& s)
                {
                    const auto e = s.FindByGuid(guid);
                    const auto* type = ComponentRegistry::Instance().FindByName(name);
                    if (!s.IsValid(e) || !type) return;
                    type->Add(s.Registry(), e);
                    s.MarkDirty(e);
                });
            });
            ImGui::EndPopup();
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

            ComponentRegistry::Instance().ForEach([&](const ComponentTypeInfo& info)
            {
                if (!info.Inspectable || !info.Has(scene.Registry(), entity)) return;
                ImGui::PushID(info.Name.c_str());
                SectionHeader(UpperName(info.DisplayName).c_str());
                if (!info.Note.empty()) ImGui::TextDisabled("%s", info.Note.c_str());
                for (const auto& field : info.Fields)
                {
                    if (!FieldVisible(field, info, scene.Registry(), entity)) continue;
                    const auto current = field.Get(scene.Registry(), entity);
                    if (std::holds_alternative<std::monostate>(current)) continue;
                    ImGui::PushID(field.Name.c_str());
                    if (auto edited = DrawField(field, current))
                    {
                        EditField(runner, guid, info.Name, field.Name, std::move(*edited));
                    }
                    ImGui::PopID();
                }
                ImGui::PopID();
            });

            RenderAddComponentMenu(runner, scene, entity, guid);

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
