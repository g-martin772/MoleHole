module MoleHole;

import :Simulation.Gravity;
import std;
import GPP;
import glm;

namespace MoleHole
{
    GravitySimulationModule::GravitySimulationModule(
        std::shared_ptr<GPP::PhysicsSimulationModule> physics,
        const std::shared_ptr<GPP::EventDispatcher>& dispatcher,
        std::shared_ptr<GPP::Logger> logger)
        : m_Physics(std::move(physics)), m_Logger(std::move(logger))
    {
        m_TriggerSubscription = dispatcher->Subscribe<GPP::PhysicsTriggerEvent>(
            [this](const GPP::PhysicsTriggerEvent& event) { HandleTrigger(event); });
    }

    void GravitySimulationModule::OnInit(GPP::Scene& scene)
    {
        for (auto [entity, blackHole] : scene.Registry().view<const BlackHoleComponent>().each())
        {
            if (!scene.Registry().all_of<GPP::RigidBodyComponent>(entity))
            {
                scene.Registry().emplace<GPP::RigidBodyComponent>(entity, GPP::RigidBodyComponent{
                    .Type = GPP::RigidBodyType::Static,
                    .EnableGravity = false
                });
            }
            if (!scene.Registry().all_of<GPP::ColliderComponent>(entity))
            {
                const float schwarzschildRadius = std::max(0.01f, 2.0f * blackHole.Mass);
                scene.Registry().emplace<GPP::ColliderComponent>(entity, GPP::ColliderComponent{
                    .Shape = GPP::ColliderShape::Sphere,
                    .Radius = schwarzschildRadius,
                    .IsTrigger = true
                });
            }
        }
    }

    void GravitySimulationModule::OnTick(GPP::Scene& scene, const float deltaTime)
    {
        m_CurrentScene = &scene;

        struct Source
        {
            glm::vec3 Position;
            float MassKg;
        };
        std::vector<Source> sources;

        for (auto [entity, blackHole, transform] :
             scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            sources.push_back({transform.Position, blackHole.Mass * kGravitySolarMassKg});
        }
        for (auto [entity, body, transform] :
             scene.Registry().view<const GPP::RigidBodyComponent, const GPP::TransformComponent>().each())
        {
            if (body.Type != GPP::RigidBodyType::Dynamic) continue;
            sources.push_back({transform.Position, body.Mass});
        }

        for (auto [entity, body, transform] :
             scene.Registry().view<const GPP::RigidBodyComponent, const GPP::TransformComponent>().each())
        {
            if (body.Type != GPP::RigidBodyType::Dynamic || !body.EnableGravity) continue;

            auto* actor = m_Physics->FindActor(entity);
            auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
            if (!dynamic) continue;

            glm::vec3 acceleration{0.0f};
            for (const auto& source : sources)
            {
                const glm::vec3 delta = source.Position - transform.Position;
                const float distSq = glm::dot(delta, delta);
                if (distSq < 1e-6f) continue;
                acceleration += kGravitationalConstant * source.MassKg / distSq * glm::normalize(delta);
            }

            const glm::vec3 force = acceleration * body.Mass;
            dynamic->addForce(physx::PxVec3(force.x, force.y, force.z), physx::PxForceMode::eFORCE);
        }
    }

    void GravitySimulationModule::HandleTrigger(const GPP::PhysicsTriggerEvent& event)
    {
        if (!event.Entered || !m_CurrentScene) return;

        const auto blackHoleEntity = m_CurrentScene->FindByGuid(event.TriggerGuid);
        if (!m_CurrentScene->IsValid(blackHoleEntity)
            || !m_CurrentScene->Registry().all_of<BlackHoleComponent>(blackHoleEntity))
        {
            return;
        }

        const auto otherEntity = m_CurrentScene->FindByGuid(event.OtherGuid);
        if (!m_CurrentScene->IsValid(otherEntity)) return;

        if (m_Logger) m_Logger->Info("GravitySimulationModule: entity absorbed by black hole.");
        m_CurrentScene->Registry().destroy(otherEntity);
    }
}
