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
}
