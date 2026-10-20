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
                const double schwarzschildRadiusM = std::max(0.01, GPP::Units::SchwarzschildRadius(blackHole.Mass));
                scene.Registry().emplace<GPP::ColliderComponent>(entity, GPP::ColliderComponent{
                    .Shape = GPP::ColliderShape::Sphere,
                    .Radius = schwarzschildRadiusM,
                    .IsTrigger = true
                });
            }
        }
        // Geodesic bodies are moved by GeodesicSimulationModule; PhysX only follows them.
        for (auto [entity, geodesic, body] : scene.Registry().view<const GeodesicBodyComponent, GPP::RigidBodyComponent>().each())
        {
            if (body.Type == GPP::RigidBodyType::Dynamic) body.Type = GPP::RigidBodyType::Kinematic;
        }
    }

    void GravitySimulationModule::OnTick(GPP::Scene& scene, const float deltaTime)
    {
        m_CurrentScene = &scene;

        std::vector<GravitySource> sources;

        for (auto [entity, blackHole, transform] :
             scene.Registry().view<const BlackHoleComponent, const GPP::TransformComponent>().each())
        {
            sources.push_back({transform.Position, blackHole.Mass});
        }
        for (auto [entity, body, transform] :
             scene.Registry().view<const GPP::RigidBodyComponent, const GPP::TransformComponent>().each())
        {
            if (body.Type != GPP::RigidBodyType::Dynamic || scene.Registry().all_of<GeodesicBodyComponent>(entity)) continue;
            sources.push_back({transform.Position, body.Mass});
        }

        const double multiplier = m_GravityMultiplier.load(std::memory_order_relaxed);
        const auto precision = m_Precision.load(std::memory_order_relaxed);

        for (auto [entity, body, transform] :
             scene.Registry().view<const GPP::RigidBodyComponent, const GPP::TransformComponent>().each())
        {
            if (body.Type != GPP::RigidBodyType::Dynamic || !body.EnableGravity
                || scene.Registry().all_of<GeodesicBodyComponent>(entity))
            {
                continue;
            }

            auto* actor = m_Physics->FindActor(entity);
            auto* dynamic = actor ? actor->is<physx::PxRigidDynamic>() : nullptr;
            if (!dynamic) continue;

            const glm::dvec3 accelerationMs2 = NewtonianAcceleration(precision, sources, transform.Position, multiplier);
            const bool finite = std::isfinite(accelerationMs2.x) && std::isfinite(accelerationMs2.y)
                                && std::isfinite(accelerationMs2.z);
            if (!finite)
            {
                if (m_Logger)
                {
                    m_Logger->Warn("GravitySimulationModule: skipping a non-finite gravity acceleration "
                                   "(check mass/distance values are reasonable)");
                }
                continue;
            }
            // Boundary SI double -> PhysX float32
            dynamic->addForce(physx::PxVec3(static_cast<float>(accelerationMs2.x), static_cast<float>(accelerationMs2.y),
                                            static_cast<float>(accelerationMs2.z)),
                              physx::PxForceMode::eACCELERATION);
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
        // Scene::DestroyEntity, not Registry().destroy() -- the latter leaves the entity's guid
        // dangling in Scene's GuidIndex pointing at a now-invalid entity handle, which later crashes
        // Scene::SyncInto (e.g. a Play->Stop snapshot restore) when it resolves that stale guid.
        m_CurrentScene->DestroyEntity(otherEntity);
    }
}
