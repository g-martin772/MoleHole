export module MoleHole:Simulation.Gravity;

import std;
import glm;
import GPP;
import :Simulation.Components;

export namespace MoleHole
{
    constexpr float kGravitationalConstant = 6.67430e-11f;
    constexpr float kGravitySolarMassKg = 1.989e30f;

    class GravitySimulationModule final : public GPP::ISimulationModule
    {
    public:
        GravitySimulationModule(std::shared_ptr<GPP::PhysicsSimulationModule> physics,
                                const std::shared_ptr<GPP::EventDispatcher>& dispatcher,
                                std::shared_ptr<GPP::Logger> logger);

        void OnInit(GPP::Scene& scene) override;
        void OnTick(GPP::Scene& scene, float deltaTime) override;

    private:
        void HandleTrigger(const GPP::PhysicsTriggerEvent& event);

        std::shared_ptr<GPP::PhysicsSimulationModule> m_Physics;
        std::shared_ptr<GPP::Logger> m_Logger;
        GPP::EventSubscription m_TriggerSubscription;
        GPP::Scene* m_CurrentScene{nullptr};
    };
}
