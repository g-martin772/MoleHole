export module MoleHole:Simulation.Gravity;

import std;
import glm;
import GPP;
import :Simulation.Components;

export namespace MoleHole
{
    struct GravitySource
    {
        glm::dvec3 PositionM{0.0};
        double MassKg{0.0};
    };

    template <typename Real>
    [[nodiscard]] glm::dvec3 NewtonianAcceleration(std::span<const GravitySource> sources, const glm::dvec3& positionM,
                                                   const double multiplier = 1.0)
    {
        const Real g = static_cast<Real>(GPP::Units::kGravitationalConstantW) * static_cast<Real>(multiplier);
        const std::array<Real, 3> p{static_cast<Real>(positionM.x), static_cast<Real>(positionM.y),
                                    static_cast<Real>(positionM.z)};
        std::array<Real, 3> acceleration{0, 0, 0};
        for (const auto& source : sources)
        {
            const std::array<Real, 3> delta{static_cast<Real>(source.PositionM.x) - p[0],
                                            static_cast<Real>(source.PositionM.y) - p[1],
                                            static_cast<Real>(source.PositionM.z) - p[2]};
            const Real distanceSquared = delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2];
            if (!(distanceSquared > static_cast<Real>(1e-6))) continue;
            const Real distance = std::sqrt(distanceSquared);
            const Real scale = g * static_cast<Real>(source.MassKg) / distanceSquared / distance;
            for (int i = 0; i < 3; ++i) acceleration[i] += scale * delta[i];
        }
        return {static_cast<double>(acceleration[0]), static_cast<double>(acceleration[1]),
                static_cast<double>(acceleration[2])};
    }

    [[nodiscard]] inline glm::dvec3 NewtonianAcceleration(const GPP::Units::Precision precision,
                                                          std::span<const GravitySource> sources,
                                                          const glm::dvec3& positionM, const double multiplier = 1.0)
    {
        using GPP::Units::Precision;
        switch (precision)
        {
        case Precision::F32: return NewtonianAcceleration<float>(sources, positionM, multiplier);
        case Precision::F128:
            if constexpr (GPP::Units::kHasFloat128) return NewtonianAcceleration<GPP::Units::Wide>(sources, positionM, multiplier);
            [[fallthrough]];
        case Precision::F64: default: return NewtonianAcceleration<double>(sources, positionM, multiplier);
        }
    }

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

        void SetGravityMultiplier(float multiplier) noexcept { m_GravityMultiplier.store(multiplier, std::memory_order_relaxed); }
        void SetPrecision(GPP::Units::Precision precision) noexcept
        {
            m_Precision.store(GPP::Units::CpuSupports(precision) ? precision : GPP::Units::Precision::F64,
                              std::memory_order_relaxed);
        }
        [[nodiscard]] GPP::Units::Precision GetPrecision() const noexcept { return m_Precision.load(std::memory_order_relaxed); }

    private:
        void HandleTrigger(const GPP::PhysicsTriggerEvent& event);

        std::shared_ptr<GPP::PhysicsSimulationModule> m_Physics;
        std::shared_ptr<GPP::Logger> m_Logger;
        GPP::EventSubscription m_TriggerSubscription;
        GPP::Scene* m_CurrentScene{nullptr};
        std::atomic<float> m_GravityMultiplier{1.0f};
        std::atomic<GPP::Units::Precision> m_Precision{GPP::Units::Precision::F64};
    };
}
