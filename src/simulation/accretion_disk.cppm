module;
#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>
export module MoleHole:Simulation.AccretionDisk;

import std;
import GPP;
import :Simulation.Components;

export namespace MoleHole
{
    enum class DiskModel : int { Analytic, Particles };
    enum class DiskRateMode : int { MassRate, Eddington };
    enum class DiskIntegrator : int { PaczynskiWiita, Geodesic };

    // Radii are in r_g = GM/c^2 of the hole the component sits on, so a disk follows its hole's mass.
    struct AccretionDiskComponent
    {
        DiskModel Model{DiskModel::Analytic};
        DiskRateMode RateMode{DiskRateMode::MassRate};
        double MassRate{1.0e-8 * GPP::Units::kSolarMass / GPP::Units::kJulianYear}; // kg/s
        double Eddington{0.1};
        double Alpha{0.1};
        double AspectRatio{0.1};
        double InnerRadius{0.0}; // r_g, 0 = ISCO
        double OuterRadius{100.0}; // r_g
        double Opacity{0.034};     // m^2/kg
        int MaxParticles{1 << 18};
        DiskIntegrator Integrator{DiskIntegrator::PaczynskiWiita};
        bool FeedOuterEdge{true};
        bool FeedBackToHole{false};
        double RingRadius{0.0};    // r_g, 0 = no initial ring
        double RingEccentricity{0.0};
        double RingMass{0.0};      // kg
        int RingParticles{20000};
        int Seed{1};
    };

    inline constexpr std::size_t kDiskAlpha = 0;
    inline constexpr std::size_t kDiskAspect = 1;
    inline constexpr std::size_t kDiskInner = 2;
    inline constexpr std::size_t kDiskNormal = 3;
    inline constexpr std::size_t kDiskTemperature = 6;
    inline constexpr double kDiskRadiativeEfficiency = 1.0 / 16.0; // Paczynski-Wiita binding energy at the ISCO
    inline constexpr double kMaxAccretionSpin = 0.998;
    inline constexpr double kDiskSinkRadius = 1.5; // in r_s: the PW force diverges at r_s and the energy booked there is garbage
    inline constexpr std::uint64_t kDiskSinkId = 0x4D6F6C65486F6C01ull;
    inline constexpr std::uint64_t kDiskRingId = 0x4D6F6C65486F6C02ull;
    inline constexpr std::uint64_t kDiskFeedId = 0x4D6F6C65486F6C03ull;

    struct DiskStats
    {
        std::uint64_t Particles{0};
        double Mass{0.0};            // kg in the disk
        double MassRate{0.0};        // kg/s absorbed by the hole, measured over a window of orbital periods
        double MeanTemperature{0.0}; // K, mass weighted
        double InnerRadius{0.0};     // m, where viscosity stops and matter plunges
        double AbsorbedMass{0.0};    // kg since the particles were last reset
        double AbsorbedEnergy{0.0};  // J, kinetic plus Paczynski-Wiita potential energy
        glm::dvec3 AbsorbedAngularMomentum{0.0}; // kg m^2/s
        double SimTime{0.0};         // s of particle time
        std::uint64_t Dropped{0};
        std::uint32_t MacroSteps{0}; // in the last frame
        double RecordMs{0.0};        // CPU time of the last advance (recording for the GPU backend)
        bool Limited{false};
        bool Gpu{false};
        bool Active{false};
    };

    // Thread safe; written by the render thread's particle module, read by scripts, the inspector and the UI.
    class AccretionBoard
    {
    public:
        void Publish(const std::uint64_t hole, const DiskStats& stats)
        {
            std::scoped_lock lock(m_Mutex);
            m_Primary = hole;
            m_Stats[hole] = stats;
        }

        void Clear()
        {
            std::scoped_lock lock(m_Mutex);
            m_Stats.clear();
            m_Primary = 0;
        }

        [[nodiscard]] std::optional<DiskStats> Get(const std::uint64_t hole) const
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Stats.find(hole);
            if (it == m_Stats.end()) return std::nullopt;
            return it->second;
        }

        [[nodiscard]] DiskStats Primary() const
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Stats.find(m_Primary);
            return it == m_Stats.end() ? DiskStats{} : it->second;
        }

    private:
        mutable std::mutex m_Mutex;
        std::unordered_map<std::uint64_t, DiskStats> m_Stats;
        std::uint64_t m_Primary{0};
    };

    [[nodiscard]] AccretionBoard& GlobalAccretionBoard();
    [[nodiscard]] const std::shared_ptr<GPP::ParticleHub>& GlobalParticleHub();

    // Everything derived from the component and the hole's mass; all lengths in simulation units (r_g of the epoch).
    struct DiskSetup
    {
        GPP::ParticleForceParams Force;
        double InnerRadius{0.0};
        double OuterRadius{0.0};
        double MassRate{0.0}; // kg/s
        double Alpha{0.0};
        double AspectRatio{0.0};
        glm::dvec3 Normal{0.0, 1.0, 0.0};
    };

    [[nodiscard]] inline double DiskMassRate(const AccretionDiskComponent& disk, const double holeMassKg)
    {
        if (disk.RateMode == DiskRateMode::MassRate) return std::max(disk.MassRate, 0.0);
        const double c = GPP::Units::kSpeedOfLight;
        return std::max(disk.Eddington, 0.0) * GPP::Units::EddingtonLuminosity(holeMassKg)
               / (kDiskRadiativeEfficiency * c * c);
    }

    [[nodiscard]] inline glm::dvec3 DiskNormal(const BlackHoleComponent& hole)
    {
        glm::dvec3 axis(hole.SpinAxis);
        const double length = glm::length(axis);
        axis = length > 0.0 ? axis / length : glm::dvec3(0.0, 1.0, 0.0);
        return hole.Spin < 0.0 ? -axis : axis;
    }

    // Circular speed at cylindrical radius R in the Paczynski-Wiita potential, in units where GM = 1 sets mu.
    [[nodiscard]] inline double PwCircularSpeed(const double radius, const double mu, const double schwarzschild)
    {
        return std::sqrt(mu * radius) / std::max(radius - schwarzschild, 0.001 * schwarzschild);
    }

    [[nodiscard]] inline DiskSetup MakeDiskSetup(const AccretionDiskComponent& disk, const BlackHoleComponent& hole,
                                                 const GPP::ParticleUnits& units)
    {
        DiskSetup setup;
        const double rg = GPP::Units::GravitationalRadius(hole.Mass) / units.Length;
        const double mu = units.Mu(hole.Mass);
        const double rs = 2.0 * rg;
        setup.InnerRadius = disk.InnerRadius > 0.0 ? disk.InnerRadius * rg : 3.0 * rs;
        setup.OuterRadius = std::max(disk.OuterRadius * rg, setup.InnerRadius * 1.05);
        setup.MassRate = DiskMassRate(disk, hole.Mass);
        setup.Alpha = std::clamp(disk.Alpha, 0.0, 1.0);
        setup.AspectRatio = std::clamp(disk.AspectRatio, 0.001, 0.5);
        setup.Normal = DiskNormal(hole);

        // T_eff^4 = 3 G M Mdot (1 - sqrt(r_in / R)) / (8 pi sigma R^3) with R in metres, folded into simulation lengths
        const double length = units.Length;
        const double coefficient = 3.0 * GPP::Units::kGravitationalConstant * hole.Mass * setup.MassRate
                                   / (8.0 * std::numbers::pi * GPP::Units::kStefanBoltzmann * length * length * length);

        auto& force = setup.Force;
        force.Mu = static_cast<float>(mu);
        force.Softening = 0.0f;
        force.SchwarzschildRadius = static_cast<float>(rs);
        force.Kind = static_cast<std::uint32_t>(GPP::ParticleForceKind::Custom);
        force.User = {};
        force.User[kDiskAlpha] = static_cast<float>(setup.Alpha);
        force.User[kDiskAspect] = static_cast<float>(setup.AspectRatio);
        force.User[kDiskInner] = static_cast<float>(setup.InnerRadius);
        for (int i = 0; i < 3; ++i) force.User[kDiskNormal + i] = static_cast<float>(setup.Normal[i]);
        force.User[kDiskTemperature] = static_cast<float>(coefficient);
        return setup;
    }

    // Time for matter to drift from the outer to the inner edge at v_r = 3 nu / (2 R) (simulation time units).
    [[nodiscard]] inline double DiskDriftTime(const DiskSetup& setup)
    {
        const double mu = setup.Force.Mu;
        const double rs = setup.Force.SchwarzschildRadius;
        const double rate = setup.Alpha * setup.AspectRatio * setup.AspectRatio;
        if (!(rate > 0.0)) return 0.0;
        constexpr int kSteps = 512;
        double total = 0.0;
        const double ratio = setup.OuterRadius / setup.InnerRadius;
        for (int i = 0; i < kSteps; ++i)
        {
            const double R = setup.InnerRadius * std::pow(ratio, (i + 0.5) / kSteps);
            const double dR = R * std::log(ratio) / kSteps;
            const double omega = std::sqrt(mu / R) / std::max(R - rs, 0.001 * rs);
            const double edge = std::max(1.0 - std::sqrt(setup.InnerRadius / R), 0.04);
            total += dR / (1.5 * rate * omega * R / edge);
        }
        return total;
    }

    [[nodiscard]] inline GPP::ParticleSinkDef MakeDiskSink(const BlackHoleComponent& hole, const glm::dvec3& position)
    {
        GPP::ParticleSinkDef sink;
        sink.Id = kDiskSinkId;
        sink.Position = position;
        sink.Params.Active = true;
        sink.Params.Radius = kDiskSinkRadius * GPP::Units::SchwarzschildRadius(hole.Mass);
        sink.Params.Mode = GPP::ParticleSinkMode::Absorb;
        sink.Params.Accounting = true;
        return sink;
    }

    [[nodiscard]] inline glm::quat FaceAxis(const glm::dvec3& axis)
    {
        const glm::dvec3 z(0.0, 0.0, 1.0);
        const double cosine = glm::dot(z, axis);
        if (cosine > 0.999999) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        if (cosine < -0.999999) return glm::quat(0.0f, 1.0f, 0.0f, 0.0f);
        const glm::dvec3 rotation = glm::cross(z, axis);
        const double half = 0.5 * std::acos(std::clamp(cosine, -1.0, 1.0));
        const glm::dvec3 unit = glm::normalize(rotation);
        return glm::quat(static_cast<float>(std::cos(half)), static_cast<float>(unit.x * std::sin(half)),
                         static_cast<float>(unit.y * std::sin(half)), static_cast<float>(unit.z * std::sin(half)));
    }

    // Seeded ring released once at the start: every particle at the apocentre radius with the circular speed scaled by
    // sqrt(1 - e), so the eccentricity (Newtonian definition at apocentre) is the component's.
    [[nodiscard]] inline std::optional<GPP::ParticleEmitterDef> MakeDiskRing(
        const AccretionDiskComponent& disk, const BlackHoleComponent& hole, const DiskSetup& setup,
        const GPP::ParticleUnits& units, const glm::dvec3& position)
    {
        if (!(disk.RingRadius > 0.0) || disk.RingParticles <= 0) return std::nullopt;
        const double rg = GPP::Units::GravitationalRadius(hole.Mass);
        const double radius = std::max(disk.RingRadius * rg, setup.InnerRadius * units.Length);
        const double mu = setup.Force.Mu;
        const double rs = setup.Force.SchwarzschildRadius;
        const double circular = PwCircularSpeed(radius / units.Length, mu, rs) * units.Velocity();
        GPP::ParticleEmitterDef def;
        def.Id = kDiskRingId;
        def.Position = position;
        def.Rotation = FaceAxis(setup.Normal);
        auto& p = def.Params;
        p.Active = true;
        p.Shape = GPP::ParticleEmitterShape::Ring;
        p.Rate = 0.0;
        p.RateMode = GPP::ParticleRateMode::ParticleRate;
        p.ParticleMass = disk.RingMass / disk.RingParticles;
        p.Burst = disk.RingParticles;
        p.Seed = static_cast<std::uint64_t>(disk.Seed) * 0x9E3779B97F4A7C15ull + 17;
        p.Radius = radius;
        p.Thickness = 0.0;
        p.VelocityMode = GPP::ParticleVelocityMode::Azimuthal;
        p.Speed = circular * std::sqrt(std::clamp(1.0 - disk.RingEccentricity, 0.0, 1.0));
        p.Dispersion = 0.0;
        p.Temperature = 1.0e4;
        return def;
    }

    // Matter entering at the outer edge on circular orbits, at the component's mass rate.
    [[nodiscard]] inline std::optional<GPP::ParticleEmitterDef> MakeDiskFeed(
        const AccretionDiskComponent& disk, const BlackHoleComponent& hole, const DiskSetup& setup,
        const GPP::ParticleUnits& units, const glm::dvec3& position)
    {
        if (!disk.FeedOuterEdge || !(setup.MassRate > 0.0)) return std::nullopt;
        const double drift = DiskDriftTime(setup) * units.Time;
        if (!(drift > 0.0)) return std::nullopt;
        const double radius = setup.OuterRadius * units.Length;
        const double circular = PwCircularSpeed(setup.OuterRadius, setup.Force.Mu, setup.Force.SchwarzschildRadius)
                                * units.Velocity();
        const double target = 0.5 * std::max(disk.MaxParticles, 1024);
        const double rate = target / drift;
        GPP::ParticleEmitterDef def;
        def.Id = kDiskFeedId;
        def.Position = position;
        def.Rotation = FaceAxis(setup.Normal);
        auto& p = def.Params;
        p.Active = true;
        p.Shape = GPP::ParticleEmitterShape::Ring;
        p.RateMode = GPP::ParticleRateMode::MassRate;
        p.Rate = rate;
        p.MassRate = setup.MassRate;
        p.Seed = static_cast<std::uint64_t>(disk.Seed) * 0xD6E8FEB86659FD93ull + 29;
        p.Radius = radius;
        p.Thickness = 0.04 * radius;
        p.VelocityMode = GPP::ParticleVelocityMode::Azimuthal;
        p.Speed = circular;
        p.Dispersion = setup.AspectRatio * circular;
        p.Temperature = 1.0e4;
        return def;
    }

    // Weight that scales the fixed-point accumulators: about one particle's mass
    [[nodiscard]] inline double DiskReferenceWeight(const AccretionDiskComponent& disk, const BlackHoleComponent& hole,
                                                    const DiskSetup& setup, const GPP::ParticleUnits& units)
    {
        if (const auto feed = MakeDiskFeed(disk, hole, setup, units, glm::dvec3(0.0)))
        {
            return GPP::ParticleSystem::RateAndWeight(feed->Params).second;
        }
        if (const auto ring = MakeDiskRing(disk, hole, setup, units, glm::dvec3(0.0)))
        {
            return GPP::ParticleSystem::RateAndWeight(ring->Params).second;
        }
        return 1.0;
    }

    namespace DiskKernel
    {
        constexpr float kTwoPi = 6.28318530717958647692f;

        [[nodiscard]] inline float PwDistance(const float r, const float rs)
        {
            return std::max(r - rs, 0.001f * rs);
        }

        // The relaxation applied once per particle after its substeps; mirrored line by line in
        // shaders/particle_custom_force.glsl.
        inline void PostMacro(GPP::ParticleView& p, const float dt, const GPP::ParticleForceParams& f)
        {
            const float alpha = f.User[kDiskAlpha];
            const float aspect = f.User[kDiskAspect];
            const float inner = f.User[kDiskInner];
            const glm::vec3 normal(f.User[kDiskNormal], f.User[kDiskNormal + 1], f.User[kDiskNormal + 2]);
            const float height = glm::dot(p.Position, normal);
            const glm::vec3 planar = p.Position - height * normal;
            const float R = glm::length(planar);
            if (!(R > inner)) return;
            const glm::vec3 radial = planar / R;
            const glm::vec3 azimuth = glm::cross(normal, radial);
            const float d = PwDistance(R, f.SchwarzschildRadius);
            const float omega = std::sqrt(f.Mu / R) / d;
            const float rate = alpha * aspect * aspect * omega;
            const float keplerSpeed = R * omega;
            float vr = glm::dot(p.Velocity, radial);
            float vphi = glm::dot(p.Velocity, azimuth);
            float vz = glm::dot(p.Velocity, normal);

            vr *= std::exp(-rate * dt);

            const float edge = std::max(1.0f - std::sqrt(inner / R), 0.04f);
            const float drift = 1.5f * rate * R / edge;
            const float gradient = std::sqrt(f.Mu * R) * (0.5f * R - 1.5f * f.SchwarzschildRadius) / (d * d);
            float l = R * vphi;
            const float loss = drift * gradient * dt;
            l = l >= 0.0f ? std::max(l - loss, 0.0f) : std::min(l + loss, 0.0f);
            vphi = l / R;

            const auto bits = GPP::ParticleRng::Philox4x32(
                {p.Serial, std::bit_cast<std::uint32_t>(p.Age), p.Species, 0u},
                {0xD15C0001u, std::bit_cast<std::uint32_t>(dt)});
            const float xi = GPP::ParticleRng::Normal(GPP::ParticleRng::ToUnit(bits[0]), GPP::ParticleRng::ToUnit(bits[1])).first;
            if (alpha > 0.0f)
            {
                const float keep = std::exp(-omega * dt);
                vz = vz * keep + aspect * keplerSpeed * std::sqrt(std::max(1.0f - keep * keep, 0.0f)) * xi;
            }

            p.Velocity = vr * radial + vphi * azimuth + vz * normal;

            const float coefficient = f.User[kDiskTemperature];
            if (coefficient > 0.0f)
            {
                const float fourth = coefficient * std::max(1.0f - std::sqrt(inner / R), 0.0f) / (R * R * R);
                const float effective = std::sqrt(std::sqrt(fourth));
                const float relax = std::exp(-alpha * omega * dt);
                p.Temperature = effective + (p.Temperature - effective) * relax;
            }
        }
    }

    [[nodiscard]] inline GPP::ParticleHooks MakeDiskHooks()
    {
        GPP::ParticleHooks hooks;
        hooks.Acceleration = [](const glm::vec3& position, const glm::vec3&, const GPP::ParticleForceParams& force)
        {
            return GPP::ParticleKernel::PaczynskiWiita(position, force.Mu, force.SchwarzschildRadius);
        };
        hooks.MaxStableStep = [](const glm::vec3& position, const glm::vec3& velocity, const float eta,
                                 const GPP::ParticleForceParams& force)
        {
            const float speed = glm::length(velocity) + 1e-30f;
            const float r = std::max(glm::length(position), 1e-30f);
            const float d = GPP::ParticleKernel::PaczynskiWiitaDistance(r, force.SchwarzschildRadius);
            return eta * GPP::ParticleKernel::kTwoPi * std::min(std::sqrt(r * d * d / force.Mu), d / speed);
        };
        hooks.Potential = [](const glm::vec3& position, const GPP::ParticleForceParams& force)
        {
            return -force.Mu / GPP::ParticleKernel::PaczynskiWiitaDistance(glm::length(position), force.SchwarzschildRadius);
        };
        hooks.PostMacroStep = [](GPP::ParticleView& view, const float dt, const GPP::ParticleForceParams& force)
        {
            DiskKernel::PostMacro(view, dt, force);
        };
        return hooks;
    }

    struct AccretionDelta
    {
        double RestMass{0.0};        // kg
        double Energy{0.0};          // J, sum of m (v^2 / 2 + Phi)
        double AngularMomentum{0.0}; // kg m^2/s along the hole's spin axis
    };

    struct HoleUpdate
    {
        double Mass{0.0};
        double Spin{0.0};
    };

    // dM = sum E dm0 with E = c^2 + (v^2 / 2 + Phi), dJ = sum L dm0; a* saturates at 0.998 (Thorne 1974).
    [[nodiscard]] inline HoleUpdate ApplyAccretion(const double massKg, const double spin, const AccretionDelta& delta)
    {
        const double c = GPP::Units::kSpeedOfLight;
        const double g = GPP::Units::kGravitationalConstant;
        const double newMass = massKg + delta.RestMass + delta.Energy / (c * c);
        const double angular = spin * g * massKg * massKg / c + delta.AngularMomentum;
        double newSpin = newMass > 0.0 ? angular * c / (g * newMass * newMass) : spin;
        const double limit = std::max(kMaxAccretionSpin, std::abs(spin));
        newSpin = std::clamp(newSpin, -limit, limit);
        return {newMass, newSpin};
    }

    [[nodiscard]] inline AccretionDelta AccretionDeltaOf(const GPP::ParticleAccumulatorTotals& sink,
                                                         const GPP::ParticleUnits& units, const glm::dvec3& spinAxis)
    {
        return {sink.Mass(), sink.Energy(units), glm::dot(sink.AngularMomentum(units), spinAxis)};
    }
}

namespace MoleHole
{
    export void RegisterAccretionDiskComponent();
}
