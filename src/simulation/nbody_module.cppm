module;
#include <glm/glm.hpp>
export module MoleHole:Simulation.NBody;

import std;
import GPP;
import :Simulation.Components;

// The Newtonian N-body system of the solar-system scene, in double precision.
//  - Every body with a GM pulls on every NBody entity (a GM of 0 is a test particle: it feels, it does not pull).
//  - The relativistic part is the 1PN Einstein-Infeld-Hoffmann acceleration of the JPL ephemerides (Newhall,
//    Standish & Williams 1983, beta = gamma = 1), which gives Mercury's 43"/century perihelion advance.
//  - An oblate body (J2 > 0) adds the quadrupole pull about its Sphere's spin axis and receives the reaction.
//  - Integration is Yoshida's 6th-order symplectic composition of leapfrog steps with a step of a fraction of the
//    shortest dynamical time sqrt(r^3 / GM) in the system, so the closest moon orbit is always resolved.
// Notes: docs/notes/solar_system.md
export namespace MoleHole
{
    struct NBodySystem
    {
        std::vector<glm::dvec3> Position; // m
        std::vector<glm::dvec3> Velocity; // m/s
        std::vector<double> Mu;           // G M, m^3/s^2
        std::vector<double> J2;
        std::vector<double> J2RadiusM;
        std::vector<glm::dvec3> Pole;     // unit vector of the J2 axis

        [[nodiscard]] std::size_t Size() const noexcept { return Position.size(); }

        std::size_t Add(const glm::dvec3& positionM, const glm::dvec3& velocityMs, const double mu, const double j2 = 0.0,
                        const double j2RadiusM = 0.0, const glm::dvec3& pole = {0.0, 1.0, 0.0})
        {
            Position.push_back(positionM);
            Velocity.push_back(velocityMs);
            Mu.push_back(mu);
            J2.push_back(j2);
            J2RadiusM.push_back(j2RadiusM);
            Pole.push_back(pole);
            return Position.size() - 1;
        }
    };

    struct NBodyWorkspace
    {
        std::vector<glm::dvec3> Acceleration, Newton;
        std::vector<double> Potential; // sum over massive l of mu_l / r_il
        std::vector<std::size_t> Massive;
    };

    // dv/dt of every body. Newton and J2 always; the 1PN terms when `relativistic`.
    inline void NBodyAcceleration(const NBodySystem& system, const bool relativistic, NBodyWorkspace& work)
    {
        const std::size_t n = system.Size();
        const double c2 = GPP::Units::kSpeedOfLight * GPP::Units::kSpeedOfLight;
        auto& acc = work.Acceleration;
        auto& newton = work.Newton;
        acc.assign(n, glm::dvec3(0.0));
        newton.assign(n, glm::dvec3(0.0));
        work.Potential.assign(n, 0.0);
        work.Massive.clear();
        for (std::size_t i = 0; i < n; ++i)
        {
            if (system.Mu[i] > 0.0) work.Massive.push_back(i);
        }

        for (std::size_t i = 0; i < n; ++i)
        {
            glm::dvec3 a{0.0};
            double potential = 0.0;
            for (const std::size_t j : work.Massive)
            {
                if (j == i) continue;
                const glm::dvec3 d = system.Position[j] - system.Position[i];
                const double r2 = glm::dot(d, d);
                const double invR = 1.0 / std::sqrt(r2);
                a += d * (system.Mu[j] * invR * invR * invR);
                potential += system.Mu[j] * invR;
            }
            newton[i] = a;
            work.Potential[i] = potential;
        }
        acc = newton;

        // Quadrupole of the oblate bodies: a = -(3/2) J2 GM R^2 / r^5 [(1 - 5 z^2/r^2) d + 2 z p], d from the oblate
        // body to the other, z = d . p. The oblate body gets the opposite momentum change.
        for (const std::size_t j : work.Massive)
        {
            if (!(system.J2[j] != 0.0) || !(system.J2RadiusM[j] > 0.0)) continue;
            const glm::dvec3 pole = system.Pole[j];
            const double k = -1.5 * system.J2[j] * system.Mu[j] * system.J2RadiusM[j] * system.J2RadiusM[j];
            for (std::size_t i = 0; i < n; ++i)
            {
                if (i == j) continue;
                const glm::dvec3 d = system.Position[i] - system.Position[j];
                const double r2 = glm::dot(d, d);
                const double z = glm::dot(d, pole);
                const double invR5 = 1.0 / (r2 * r2 * std::sqrt(r2));
                const glm::dvec3 a = k * invR5 * ((1.0 - 5.0 * z * z / r2) * d + 2.0 * z * pole);
                acc[i] += a;
                if (system.Mu[i] > 0.0) acc[j] -= (system.Mu[i] / system.Mu[j]) * a;
            }
        }

        if (!relativistic) return;
        // Einstein-Infeld-Hoffmann, beta = gamma = 1. d = r_j - r_i; the Newtonian accelerations of the sources enter.
        // `brace` is the bracket of the equation minus its leading 1, which is the Newtonian term already in acc.
        const std::vector<glm::dvec3>& newtonOnly = work.Newton;
        for (std::size_t i = 0; i < n; ++i)
        {
            const glm::dvec3 vi = system.Velocity[i];
            glm::dvec3 a{0.0};
            for (const std::size_t j : work.Massive)
            {
                if (j == i) continue;
                const glm::dvec3 vj = system.Velocity[j];
                const glm::dvec3 d = system.Position[j] - system.Position[i];
                const double r2 = glm::dot(d, d);
                const double r = std::sqrt(r2);
                const double invR3 = 1.0 / (r2 * r);
                const double radialV = glm::dot(d, vj) / r;
                const double brace = -4.0 * work.Potential[i] / c2 - work.Potential[j] / c2
                                     + (glm::dot(vi, vi) + 2.0 * glm::dot(vj, vj) - 4.0 * glm::dot(vi, vj)) / c2
                                     - 1.5 * radialV * radialV / c2 + 0.5 * glm::dot(d, newtonOnly[j]) / c2;
                a += d * (system.Mu[j] * invR3 * brace);
                a += (system.Mu[j] * invR3 / c2) * (-glm::dot(d, 4.0 * vi - 3.0 * vj)) * (vi - vj);
                a += (3.5 * system.Mu[j] / (c2 * r)) * newtonOnly[j];
            }
            acc[i] += a;
        }
    }

    // Yoshida (1990) 6th-order composition, solution A: S2(w3 h) S2(w2 h) S2(w1 h) S2(w0 h) S2(w1 h) S2(w2 h) S2(w3 h)
    // with the neighbouring half drifts merged, so one step costs seven force evaluations.
    inline void NBodyStep(NBodySystem& system, const double stepS, const bool relativistic, NBodyWorkspace& work)
    {
        constexpr double w1 = -1.17767998417887;
        constexpr double w2 = 0.235573213359357;
        constexpr double w3 = 0.784513610477560;
        constexpr double w0 = 1.0 - 2.0 * (w1 + w2 + w3);
        constexpr std::array<double, 7> kick{w3, w2, w1, w0, w1, w2, w3};
        constexpr std::array<double, 8> drift{0.5 * w3, 0.5 * (w3 + w2), 0.5 * (w2 + w1), 0.5 * (w1 + w0),
                                              0.5 * (w0 + w1), 0.5 * (w1 + w2), 0.5 * (w2 + w3), 0.5 * w3};
        const std::size_t n = system.Size();
        for (std::size_t stage = 0; stage < kick.size(); ++stage)
        {
            for (std::size_t i = 0; i < n; ++i) system.Position[i] += system.Velocity[i] * (drift[stage] * stepS);
            NBodyAcceleration(system, relativistic, work);
            for (std::size_t i = 0; i < n; ++i) system.Velocity[i] += work.Acceleration[i] * (kick[stage] * stepS);
        }
        for (std::size_t i = 0; i < n; ++i) system.Position[i] += system.Velocity[i] * (drift.back() * stepS);
    }

    // The shortest dynamical time sqrt(r^3 / (mu_i + mu_j)) over the pairs that attract each other, s. Infinite when
    // nothing does.
    [[nodiscard]] inline double NBodyDynamicalTime(const NBodySystem& system)
    {
        double shortest = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < system.Size(); ++j)
        {
            if (!(system.Mu[j] > 0.0)) continue;
            for (std::size_t i = 0; i < system.Size(); ++i)
            {
                if (i == j || (i < j && system.Mu[i] > 0.0)) continue; // each massive pair once
                const glm::dvec3 d = system.Position[j] - system.Position[i];
                const double r2 = glm::dot(d, d);
                shortest = std::min(shortest, std::sqrt(r2 * std::sqrt(r2) / (system.Mu[i] + system.Mu[j])));
            }
        }
        return shortest;
    }

    // Advances by `seconds` (either sign) in equal steps of at most `maxStepS`, but in no more than `maxSteps`; returns
    // the number of steps taken.
    inline std::size_t NBodyAdvance(NBodySystem& system, const double seconds, const double maxStepS,
                                    const bool relativistic, NBodyWorkspace& work, const std::size_t maxSteps = 1u << 20)
    {
        if (seconds == 0.0 || system.Size() == 0 || !(maxStepS > 0.0)) return 0;
        const double steps = std::ceil(std::abs(seconds) / maxStepS);
        const std::size_t count = static_cast<std::size_t>(std::clamp(steps, 1.0, static_cast<double>(maxSteps)));
        const double step = seconds / static_cast<double>(count);
        for (std::size_t k = 0; k < count; ++k) NBodyStep(system, step, relativistic, work);
        return count;
    }

    // E / G = sum 1/2 mu v^2 - sum_{i<j} mu_i mu_j / r, the Newtonian energy of the massive bodies, m^5/s^2 (the
    // 1PN and J2 parts are not in it; relative to it they are about 1e-8 and 1e-3 for the Moon and the planets).
    [[nodiscard]] inline double NBodyEnergy(const NBodySystem& system)
    {
        double energy = 0.0;
        for (std::size_t i = 0; i < system.Size(); ++i)
        {
            energy += 0.5 * system.Mu[i] * glm::dot(system.Velocity[i], system.Velocity[i]);
            for (std::size_t j = i + 1; j < system.Size(); ++j)
            {
                energy -= system.Mu[i] * system.Mu[j] / glm::length(system.Position[j] - system.Position[i]);
            }
        }
        return energy;
    }

    // sum mu v, the momentum over G, m^4/s^3
    [[nodiscard]] inline glm::dvec3 NBodyMomentum(const NBodySystem& system)
    {
        glm::dvec3 momentum{0.0};
        for (std::size_t i = 0; i < system.Size(); ++i) momentum += system.Mu[i] * system.Velocity[i];
        return momentum;
    }

    // The simulation module: gathers the NBody entities, advances them and writes Transform and Velocity back.
    class NBodySimulationModule final : public GPP::ISimulationModule
    {
    public:
        // The step is this fraction of the shortest dynamical time. The error of the 6th-order scheme per orbit of
        // that pair goes as (2 pi * fraction)^7; 0.05 gives about 1e-8 relative energy error.
        static constexpr double kStepFraction = 0.05;
        static constexpr std::size_t kMaxStepsPerTick = 4096;

        explicit NBodySimulationModule(std::shared_ptr<GPP::Logger> logger = nullptr) : m_Logger(std::move(logger)) {}

        [[nodiscard]] GPP::SimulationPhase Phase() const noexcept override { return GPP::SimulationPhase::Forces; }
        [[nodiscard]] bool ReportsChanges() const noexcept override { return true; }

        void SetRelativistic(const bool enabled) noexcept { m_Relativistic.store(enabled, std::memory_order_relaxed); }
        [[nodiscard]] bool Relativistic() const noexcept { return m_Relativistic.load(std::memory_order_relaxed); }

        // Steps taken by the last tick, for tests and the debug readout
        [[nodiscard]] std::size_t LastSteps() const noexcept { return m_LastSteps.load(std::memory_order_relaxed); }

        void OnTick(GPP::Scene& scene, const float deltaTime) override
        {
            const double seconds = static_cast<double>(deltaTime) * scene.Metadata().TimeScale;
            if (seconds == 0.0 || !std::isfinite(seconds)) return;

            auto& registry = scene.Registry();
            m_Entities.clear();
            m_System = {};
            for (auto [entity, body, transform] : registry.view<const NBodyComponent, const GPP::TransformComponent>().each())
            {
                double radiusM = body.J2Radius;
                glm::dvec3 pole{0.0, 1.0, 0.0};
                if (const auto* sphere = registry.try_get<const SphereComponent>(entity))
                {
                    if (!(radiusM > 0.0)) radiusM = sphere->Radius;
                    if (glm::length(sphere->SpinAxis) > 0.0f) pole = glm::normalize(glm::dvec3(sphere->SpinAxis));
                }
                m_System.Add(transform.Position, body.Velocity, body.GM, body.J2, radiusM, pole);
                m_Entities.push_back(entity);
            }
            if (m_System.Size() == 0) return;

            // Powers of two, so the step does not change on every tick as the bodies move
            const double limit = kStepFraction * NBodyDynamicalTime(m_System);
            const double stepS = std::isfinite(limit) ? std::exp2(std::floor(std::log2(limit))) : std::abs(seconds);
            const std::size_t needed = static_cast<std::size_t>(std::ceil(std::abs(seconds) / stepS));
            if (needed > kMaxStepsPerTick && !m_WarnedBudget)
            {
                m_WarnedBudget = true;
                if (m_Logger)
                {
                    m_Logger->Warn("NBodySimulationModule: {:.3g} s per tick needs {} steps of {:.3g} s; using {} longer steps, "
                                   "accuracy drops. Lower the time scale for exact orbits.",
                                   seconds, needed, stepS, kMaxStepsPerTick);
                }
            }
            m_LastSteps.store(NBodyAdvance(m_System, seconds, stepS, Relativistic(), m_Work, kMaxStepsPerTick),
                              std::memory_order_relaxed);

            for (std::size_t i = 0; i < m_Entities.size(); ++i)
            {
                const auto entity = m_Entities[i];
                registry.get<GPP::TransformComponent>(entity).Position = m_System.Position[i];
                registry.get<NBodyComponent>(entity).Velocity = m_System.Velocity[i];
                scene.MarkDirty(entity);
            }
        }

    private:
        std::shared_ptr<GPP::Logger> m_Logger;
        std::atomic<bool> m_Relativistic{true};
        std::atomic<std::size_t> m_LastSteps{0};
        bool m_WarnedBudget{false};
        NBodySystem m_System;
        NBodyWorkspace m_Work;
        std::vector<entt::entity> m_Entities;
    };
}
