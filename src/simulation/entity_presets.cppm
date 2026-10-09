export module MoleHole:Simulation.EntityPresets;

import std;
import glm;
import GPP;
import :Simulation.Components;

export namespace MoleHole
{
    inline constexpr std::array<std::string_view, 4> kEntityPresetNames{"Empty", "BlackHole", "Sphere", "Camera"};

    [[nodiscard]] inline std::optional<std::string_view> FindEntityPreset(const std::string_view name)
    {
        for (const auto preset : kEntityPresetNames)
        {
            if (std::ranges::equal(preset, name, [](const char a, const char b)
            {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }))
            {
                return preset;
            }
        }
        return std::nullopt;
    }

    // Returns entt::null when the preset is unknown or the guid is taken.
    inline entt::entity SpawnPreset(GPP::Scene& scene, const std::uint64_t guid, const std::string_view preset,
                                    const glm::vec3& position, std::string name = {})
    {
        const auto canonical = FindEntityPreset(preset);
        if (!canonical) return entt::entity{entt::null};

        static constexpr std::array<std::string_view, 4> kDefaultNames{"Empty", "Black Hole", "Sphere", "Camera"};
        const auto index = static_cast<std::size_t>(
            std::ranges::find(kEntityPresetNames, *canonical) - kEntityPresetNames.begin());
        if (name.empty()) name = std::string(kDefaultNames[index]);

        const auto entity = scene.CreateEntityWithGuid(guid, std::move(name), std::string(*canonical));
        if (entity == entt::entity{entt::null}) return entity;

        auto& registry = scene.Registry();
        registry.emplace<GPP::TransformComponent>(entity, GPP::TransformComponent{.Position = position});
        if (*canonical == "BlackHole")
        {
            registry.emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = 1.0f, .Spin = 0.3f});
        }
        else if (*canonical == "Sphere")
        {
            registry.emplace<SphereComponent>(entity, SphereComponent{.Radius = 0.5f, .Color = {0.8f, 0.8f, 0.9f}});
            registry.emplace<GPP::RigidBodyComponent>(
                entity, GPP::RigidBodyComponent{.Type = GPP::RigidBodyType::Dynamic, .Mass = 5.972e24f,
                                                .EnableGravity = true});
            registry.emplace<GPP::ColliderComponent>(
                entity, GPP::ColliderComponent{.Shape = GPP::ColliderShape::Sphere, .Radius = 0.5f});
        }
        else if (*canonical == "Camera")
        {
            const bool hasPrimary = std::ranges::any_of(
                registry.view<const GPP::CameraComponent>().each(),
                [](const auto& row) { return std::get<1>(row).Primary; });
            registry.emplace<GPP::CameraComponent>(entity, GPP::CameraComponent{.Primary = !hasPrimary});
        }
        return entity;
    }

    // The spawn as a replayable command; `reference` decides whether a new camera becomes the primary one.
    inline std::optional<GPP::SpawnEntityCommand> MakeSpawnPresetCommand(
        const GPP::Scene& reference, const std::uint64_t guid, const std::string_view preset,
        const glm::vec3& position, const std::optional<glm::quat>& rotation = std::nullopt)
    {
        GPP::Scene scratch;
        const auto entity = SpawnPreset(scratch, guid, preset, position);
        if (!scratch.IsValid(entity)) return std::nullopt;
        auto& registry = scratch.Registry();
        if (auto* camera = registry.try_get<GPP::CameraComponent>(entity))
        {
            camera->Primary = !std::ranges::any_of(reference.Registry().view<const GPP::CameraComponent>().each(),
                                                   [](const auto& row) { return std::get<1>(row).Primary; });
        }
        if (rotation) { registry.get<GPP::TransformComponent>(entity).Rotation = *rotation; }
        return GPP::SpawnEntityCommand{guid, scratch.SerializeEntity(entity)};
    }

    inline GPP::SpawnEntityCommand MakeSpawnEmptyCommand(const std::uint64_t guid, const std::uint64_t parent)
    {
        GPP::Scene scratch;
        const auto entity = scratch.CreateEntityWithGuid(guid, "Empty", "Empty");
        scratch.Registry().emplace<GPP::TransformComponent>(entity);
        if (parent != 0) { scratch.Registry().emplace<GPP::HierarchyComponent>(entity, parent); }
        return GPP::SpawnEntityCommand{guid, scratch.SerializeEntity(entity)};
    }
}
