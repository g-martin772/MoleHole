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
                                    const glm::dvec3& position, std::string name = {})
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
            registry.emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = GPP::Units::kSolarMass, .Spin = 0.3});
        }
        else if (*canonical == "Sphere")
        {
            registry.emplace<SphereComponent>(entity, SphereComponent{.Radius = 0.5 * GPP::Units::kSolarGravitationalRadius, .Color = {0.8f, 0.8f, 0.9f}});
            registry.emplace<GPP::RigidBodyComponent>(
                entity, GPP::RigidBodyComponent{.Type = GPP::RigidBodyType::Dynamic, .Mass = GPP::Units::kEarthMass,
                                                .EnableGravity = true});
            registry.emplace<GPP::ColliderComponent>(
                entity, GPP::ColliderComponent{.Shape = GPP::ColliderShape::Sphere,
                                       .Radius = 0.5 * GPP::Units::kSolarGravitationalRadius});
        }
        else if (*canonical == "Camera")
        {
            const bool hasPrimary = std::ranges::any_of(
                registry.view<const GPP::CameraComponent>().each(),
                [](const auto& row) { return std::get<1>(row).Primary; });
            registry.emplace<GPP::CameraComponent>(
                entity, GPP::CameraComponent{.NearPlane = 0.1 * GPP::Units::kSolarGravitationalRadius,
                                             .FarPlane = 1000.0 * GPP::Units::kSolarGravitationalRadius,
                                             .Primary = !hasPrimary});
        }
        return entity;
    }

    inline void PopulateDefaultScene(GPP::Scene& scene, const std::string& entityName)
    {
        const auto entity = scene.CreateEntity(entityName, "BlackHole");
        scene.Registry().emplace<GPP::TransformComponent>(entity, GPP::TransformComponent{});
        scene.Registry().emplace<BlackHoleComponent>(entity, BlackHoleComponent{.Mass = GPP::Units::kSolarMass, .Spin = 0.5});
    }

    // The spawn as a replayable command; `reference` decides whether a new camera becomes the primary one.
    inline std::optional<GPP::SpawnEntityCommand> MakeSpawnPresetCommand(
        const GPP::Scene& reference, const std::uint64_t guid, const std::string_view preset,
        const glm::dvec3& position, const std::optional<glm::quat>& rotation = std::nullopt)
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
