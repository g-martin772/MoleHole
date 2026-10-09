export module MoleHole:Simulation.Gravity;

import std;
import glm;
import GPP;
import :Simulation.Components;

export namespace MoleHole
{
    constexpr float kGravitySolarMassKg = 1.989e30f;
    constexpr float kBaseGravityStrength = 5.0e-29f;

    class GravitySimulationModule final : public GPP::ISimulationModule
    {
    public:
        GravitySimulationModule(std::shared_ptr<GPP::PhysicsSimulationModule> physics,
                                const std::shared_ptr<GPP::EventDispatcher>& dispatcher,
                                std::shared_ptr<GPP::Logger> logger);

        [[nodiscard]] GPP::SimulationPhase Phase() const noexcept override { return GPP::SimulationPhase::Forces; }
        [[nodiscard]] bool ReportsChanges() const noexcept override { return true; }

        void OnInit(GPP::Scene& scene) override;
        void OnTick(GPP::Scene& scene, float deltaTime) override;

        [[nodiscard]] std::string SaveState() const override
        {
            return std::to_string(m_GravityMultiplier.load(std::memory_order_relaxed));
        }
        void LoadState(const std::string& state) override
        {
            float value = 1.0f;
            if (std::from_chars(state.data(), state.data() + state.size(), value).ec == std::errc{})
            {
                m_GravityMultiplier.store(value, std::memory_order_relaxed);
            }
        }

        // Thread-safe: called from the main thread (ViewportLayer mirrors UiState::GravityMultiplier
        // here every frame), read from the simulation thread inside OnTick.
        void SetGravityMultiplier(float multiplier) noexcept { m_GravityMultiplier.store(multiplier, std::memory_order_relaxed); }

    private:
        void HandleTrigger(const GPP::PhysicsTriggerEvent& event);

        std::shared_ptr<GPP::PhysicsSimulationModule> m_Physics;
        std::shared_ptr<GPP::Logger> m_Logger;
        GPP::EventSubscription m_TriggerSubscription;
        GPP::Scene* m_CurrentScene{nullptr};
        std::atomic<float> m_GravityMultiplier{1.0f};
    };
}
