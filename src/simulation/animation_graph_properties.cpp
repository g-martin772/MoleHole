module MoleHole;

import :Simulation.AnimationGraphProperties;
import :Simulation.AnimationGraph;
import :Simulation.Components;
import std;
import glm;
import GPP;

namespace MoleHole
{
    namespace
    {
        glm::vec3 RotationToEulerDegrees(const glm::quat& rotation)
        {
            return glm::degrees(glm::gtc::eulerAngles(rotation));
        }

        glm::quat EulerDegreesToRotation(const glm::vec3& eulerDegrees)
        {
            return glm::quat(glm::radians(eulerDegrees));
        }

        const std::vector<PropertyCategory>& BuildPropertyCategories()
        {
            static const std::vector<PropertyCategory> categories = []
            {
                std::vector<PropertyCategory> result;

                {
                    PropertyCategory category;
                    category.Category = NodeSubType::BlackHole;
                    category.DisplayName = "BlackHole";
                    category.Properties = {
                        PropertyEntry{
                            .Label = "Mass", .Type = PinType::Float,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* c = scene.Registry().try_get<BlackHoleComponent>(entity)) { return c->Mass; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* c = scene.Registry().try_get<BlackHoleComponent>(entity))
                                {
                                    c->Mass = GetValueAs<float>(value, c->Mass);
                                }
                            },
                        },
                        PropertyEntry{
                            .Label = "Spin", .Type = PinType::Float,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* c = scene.Registry().try_get<BlackHoleComponent>(entity)) { return c->Spin; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* c = scene.Registry().try_get<BlackHoleComponent>(entity))
                                {
                                    c->Spin = GetValueAs<float>(value, c->Spin);
                                }
                            },
                        },
                        PropertyEntry{
                            .Label = "Charge", .Type = PinType::Float,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* c = scene.Registry().try_get<BlackHoleComponent>(entity)) { return c->Charge; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* c = scene.Registry().try_get<BlackHoleComponent>(entity))
                                {
                                    c->Charge = GetValueAs<float>(value, c->Charge);
                                }
                            },
                        },
                        PropertyEntry{
                            .Label = "SpinAxis", .Type = PinType::Vec3,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* c = scene.Registry().try_get<BlackHoleComponent>(entity)) { return c->SpinAxis; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* c = scene.Registry().try_get<BlackHoleComponent>(entity))
                                {
                                    c->SpinAxis = GetValueAs<glm::vec3>(value, c->SpinAxis);
                                }
                            },
                        },
                    };
                    result.push_back(std::move(category));
                }

                {
                    PropertyCategory category;
                    category.Category = NodeSubType::Sphere;
                    category.DisplayName = "Sphere";
                    category.Properties = {
                        PropertyEntry{
                            .Label = "Radius", .Type = PinType::Float,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* c = scene.Registry().try_get<SphereComponent>(entity)) { return c->Radius; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* c = scene.Registry().try_get<SphereComponent>(entity))
                                {
                                    c->Radius = GetValueAs<float>(value, c->Radius);
                                }
                            },
                        },
                        PropertyEntry{
                            .Label = "Spin", .Type = PinType::Float,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* c = scene.Registry().try_get<SphereComponent>(entity)) { return c->Spin; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* c = scene.Registry().try_get<SphereComponent>(entity))
                                {
                                    c->Spin = GetValueAs<float>(value, c->Spin);
                                }
                            },
                        },
                        PropertyEntry{
                            .Label = "Color", .Type = PinType::Vec3,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* c = scene.Registry().try_get<SphereComponent>(entity)) { return c->Color; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* c = scene.Registry().try_get<SphereComponent>(entity))
                                {
                                    c->Color = GetValueAs<glm::vec3>(value, c->Color);
                                }
                            },
                        },
                        PropertyEntry{
                            .Label = "SpinAxis", .Type = PinType::Vec3,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* c = scene.Registry().try_get<SphereComponent>(entity)) { return c->SpinAxis; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* c = scene.Registry().try_get<SphereComponent>(entity))
                                {
                                    c->SpinAxis = GetValueAs<glm::vec3>(value, c->SpinAxis);
                                }
                            },
                        },
                    };
                    result.push_back(std::move(category));
                }

                {
                    PropertyCategory category;
                    category.Category = NodeSubType::Transform;
                    category.DisplayName = "Transform";
                    category.Properties = {
                        PropertyEntry{
                            .Label = "Position", .Type = PinType::Vec3,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* t = scene.Registry().try_get<GPP::TransformComponent>(entity)) { return t->Position; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* t = scene.Registry().try_get<GPP::TransformComponent>(entity))
                                {
                                    t->Position = GetValueAs<glm::vec3>(value, t->Position);
                                }
                            },
                        },
                        PropertyEntry{
                            .Label = "Rotation", .Type = PinType::Vec3,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* t = scene.Registry().try_get<GPP::TransformComponent>(entity))
                                {
                                    return RotationToEulerDegrees(t->Rotation);
                                }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* t = scene.Registry().try_get<GPP::TransformComponent>(entity))
                                {
                                    const glm::vec3 currentDegrees = RotationToEulerDegrees(t->Rotation);
                                    const glm::vec3 degrees = GetValueAs<glm::vec3>(value, currentDegrees);
                                    t->Rotation = EulerDegreesToRotation(degrees);
                                }
                            },
                        },
                        PropertyEntry{
                            .Label = "Scale", .Type = PinType::Vec3,
                            .Get = [](const GPP::Scene& scene, const entt::entity entity) -> Value
                            {
                                if (const auto* t = scene.Registry().try_get<GPP::TransformComponent>(entity)) { return t->Scale; }
                                return std::monostate{};
                            },
                            .Set = [](GPP::Scene& scene, const entt::entity entity, const Value& value)
                            {
                                if (auto* t = scene.Registry().try_get<GPP::TransformComponent>(entity))
                                {
                                    t->Scale = GetValueAs<glm::vec3>(value, t->Scale);
                                }
                            },
                        },
                    };
                    result.push_back(std::move(category));
                }

                return result;
            }();
            return categories;
        }
    }

    const std::vector<PropertyCategory>& GetPropertyCategories()
    {
        return BuildPropertyCategories();
    }

    const PropertyCategory* FindPropertyCategory(const NodeSubType category)
    {
        for (const auto& entry : GetPropertyCategories())
        {
            if (entry.Category == category) { return &entry; }
        }
        return nullptr;
    }
}
