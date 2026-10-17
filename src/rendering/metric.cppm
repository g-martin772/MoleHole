export module MoleHole:Rendering.Metric;

import std;
import GPP;

// Characteristic radii of the Kerr–Newman family in Boyer–Lindquist coordinates (G = c = 1).
// Schwarzschild (a = Q = 0), Kerr (Q = 0) and Reissner–Nordström (a = 0) are special cases, so the
// renderer only ever deals with one metric. Background and sources: docs/notes/metric_rendering.md
export namespace MoleHole
{
    // Mirrors RenderToggles::MetricType.
    enum class MetricKind : std::int32_t { Schwarzschild = 0, Kerr = 1, ReissnerNordstrom = 2, KerrNewman = 3 };

    // Thorne (1974) limit; also keeps a² + Q² < M² so the horizon exists (no naked singularity).
    constexpr double kMaxHorizonParameter = 0.998;

    struct KerrNewmanParams
    {
        double Mass{1.0};
        double SpinParameter{0.0}; // a = J / M, in units of length
        double Charge{0.0};        // geometrized charge Q
    };

    // `spin` and `charge` are the dimensionless BlackHoleComponent fields a/M and Q/M. The metric type
    // zeroes the parameters it does not model, then a² + Q² is clamped below kMaxHorizonParameter² M².
    [[nodiscard]] inline KerrNewmanParams MakeKerrNewmanParams(const std::int32_t metricType, const double mass,
                                                                const double spin, const double charge)
    {
        const auto kind = static_cast<MetricKind>(metricType);
        const bool rotating = kind == MetricKind::Kerr || kind == MetricKind::KerrNewman;
        const bool charged = kind == MetricKind::ReissnerNordstrom || kind == MetricKind::KerrNewman;

        double a = rotating ? std::abs(spin) : 0.0;
        double q = charged ? std::abs(charge) : 0.0;
        const double magnitude = std::sqrt(a * a + q * q);
        if (magnitude > kMaxHorizonParameter)
        {
            a *= kMaxHorizonParameter / magnitude;
            q *= kMaxHorizonParameter / magnitude;
        }

        const double m = std::max(mass, 0.0);
        return KerrNewmanParams{.Mass = m, .SpinParameter = a * m, .Charge = q * m};
    }

    [[nodiscard]] inline double OuterHorizonRadius(const KerrNewmanParams& p)
    {
        const double m = p.Mass;
        const double a = p.SpinParameter;
        const double q = p.Charge;
        return m + std::sqrt(std::max(m * m - a * a - q * q, 0.0));
    }

    // Static limit: inside it no observer can stay at rest relative to the distant stars.
    [[nodiscard]] inline double ErgosphereRadius(const KerrNewmanParams& p, const double theta)
    {
        const double m = p.Mass;
        const double a = p.SpinParameter;
        const double q = p.Charge;
        const double cosTheta = std::cos(theta);
        return m + std::sqrt(std::max(m * m - a * a * cosTheta * cosTheta - q * q, 0.0));
    }

    struct EquatorialMetric
    {
        double Gtt, Gtphi, Gphiphi;
        double DGtt, DGtphi, DGphiphi; // d/dr
    };

    [[nodiscard]] inline EquatorialMetric EvaluateEquatorialMetric(const KerrNewmanParams& p, const double r)
    {
        const double m = p.Mass;
        const double a = p.SpinParameter;
        const double q = p.Charge;
        const double f = (2.0 * m * r - q * q) / (r * r);
        const double df = (2.0 * q * q - 2.0 * m * r) / (r * r * r);
        return EquatorialMetric{
            .Gtt = -(1.0 - f),
            .Gtphi = -a * f,
            .Gphiphi = r * r + a * a + a * a * f,
            .DGtt = df,
            .DGtphi = -a * df,
            .DGphiphi = 2.0 * r + a * a * df,
        };
    }

    // Angular velocity dφ/dt of a prograde circular equatorial geodesic at Boyer–Lindquist radius r.
    // Reduces to 1 / (r^{3/2} / √M + a) for Kerr.
    [[nodiscard]] inline double ProgradeOrbitalFrequency(const KerrNewmanParams& p, const double r)
    {
        const auto g = EvaluateEquatorialMetric(p, r);
        const double discriminant = g.DGtphi * g.DGtphi - g.DGtt * g.DGphiphi;
        if (discriminant < 0.0 || g.DGphiphi == 0.0) return std::numeric_limits<double>::quiet_NaN();
        return (-g.DGtphi + std::sqrt(discriminant)) / g.DGphiphi;
    }

    // Specific energy -u_t of the prograde circular orbit at r; +inf where no timelike orbit exists
    // (at or inside the photon orbit).
    [[nodiscard]] inline double CircularOrbitEnergy(const KerrNewmanParams& p, const double r)
    {
        const double omega = ProgradeOrbitalFrequency(p, r);
        if (!std::isfinite(omega)) return std::numeric_limits<double>::infinity();
        const auto g = EvaluateEquatorialMetric(p, r);
        const double norm = -(g.Gtt + 2.0 * g.Gtphi * omega + g.Gphiphi * omega * omega);
        if (!(norm > 0.0)) return std::numeric_limits<double>::infinity();
        return -(g.Gtt + g.Gtphi * omega) / std::sqrt(norm);
    }

    // Innermost stable circular orbit (prograde): the minimum of E(r) over the circular orbits.
    // Solved numerically so it covers charge as well, where no closed form like Bardeen–Press–Teukolsky's exists.
    [[nodiscard]] inline double ProgradeIscoRadius(const KerrNewmanParams& p)
    {
        if (p.Mass <= 0.0) return 0.0;

        const double rLow = OuterHorizonRadius(p) * (1.0 + 1e-6);
        const double rHigh = 12.0 * p.Mass;
        constexpr int kSamples = 4096;

        int best = -1;
        double bestEnergy = std::numeric_limits<double>::infinity();
        for (int i = 0; i <= kSamples; ++i)
        {
            const double r = rLow + (rHigh - rLow) * static_cast<double>(i) / kSamples;
            const double energy = CircularOrbitEnergy(p, r);
            if (energy < bestEnergy)
            {
                bestEnergy = energy;
                best = i;
            }
        }
        if (best < 0) return 6.0 * p.Mass;

        const double step = (rHigh - rLow) / kSamples;
        double lo = rLow + step * std::max(best - 1, 0);
        double hi = rLow + step * std::min(best + 1, kSamples);
        constexpr double kInvPhi = 0.6180339887498949;
        for (int i = 0; i < 100; ++i)
        {
            const double x1 = hi - kInvPhi * (hi - lo);
            const double x2 = lo + kInvPhi * (hi - lo);
            if (CircularOrbitEnergy(p, x1) < CircularOrbitEnergy(p, x2)) hi = x2;
            else lo = x1;
        }
        return 0.5 * (lo + hi);
    }

    // Specific angular momentum u_phi of the prograde circular orbit at r.
    [[nodiscard]] inline double CircularOrbitAngularMomentum(const KerrNewmanParams& p, const double r)
    {
        const double omega = ProgradeOrbitalFrequency(p, r);
        const auto g = EvaluateEquatorialMetric(p, r);
        const double norm = -(g.Gtt + 2.0 * g.Gtphi * omega + g.Gphiphi * omega * omega);
        if (!std::isfinite(omega) || !(norm > 0.0)) return std::numeric_limits<double>::quiet_NaN();
        return (g.Gtphi + g.Gphiphi * omega) / std::sqrt(norm);
    }

    // Fraction of rest-mass energy radiated by gas spiralling down to the ISCO: 1 - E_isco.
    [[nodiscard]] inline double RadiativeEfficiency(const KerrNewmanParams& p)
    {
        if (p.Mass <= 0.0) return 0.0;
        return 1.0 - CircularOrbitEnergy(p, ProgradeIscoRadius(p));
    }

    // Page & Thorne (1974) flux of a geometrically thin, optically thick disk with zero torque at the ISCO,
    // for unit accretion rate: F = -Omega_,r / (4 pi sqrt(-g) (E - Omega L)^2) * Integral_{r_isco}^{r} (E - Omega L) L_,r dr
    // with sqrt(-g) = r, the equatorial (t, r, phi) determinant of the Kerr–Newman family in Boyer–Lindquist
    // coordinates. Evaluated on `radii` (ascending, first >= ISCO); geometric units of p.
    [[nodiscard]] inline std::vector<double> PageThorneFlux(const KerrNewmanParams& p, const std::span<const double> radii)
    {
        std::vector<double> flux(radii.size(), 0.0);
        if (radii.empty() || p.Mass <= 0.0) return flux;

        const double rIsco = ProgradeIscoRadius(p);
        const auto derivative = [&](const auto& f, const double r)
        {
            const double h = 1e-5 * r;
            return (f(r + h) - f(r - h)) / (2.0 * h);
        };
        const auto omega = [&](const double r) { return ProgradeOrbitalFrequency(p, r); };
        const auto angularMomentum = [&](const double r) { return CircularOrbitAngularMomentum(p, r); };
        const auto integrand = [&](const double r)
        {
            return (CircularOrbitEnergy(p, r) - omega(r) * angularMomentum(r)) * derivative(angularMomentum, r);
        };

        // Integrate with Simpson's rule in u = ln r between consecutive radii, starting at the ISCO; the panel count
        // grows with the gap so sparse or far-out radii stay accurate.
        const auto integrandLog = [&](const double u) { const double r = std::exp(u); return integrand(r) * r; };
        double integral = 0.0;
        double previous = rIsco;
        for (std::size_t i = 0; i < radii.size(); ++i)
        {
            const double r = std::max(radii[i], rIsco);
            if (r > previous)
            {
                const double u0 = std::log(previous);
                const double u1 = std::log(r);
                const int panels = 2 * std::max(8, static_cast<int>(std::ceil(64.0 * (u1 - u0))));
                const double h = (u1 - u0) / panels;
                double sum = integrandLog(u0) + integrandLog(u1);
                for (int k = 1; k < panels; ++k) sum += integrandLog(u0 + k * h) * (k % 2 ? 4.0 : 2.0);
                integral += sum * h / 3.0;
            }
            previous = r;

            const double energyMinusOmegaL = CircularOrbitEnergy(p, r) - omega(r) * angularMomentum(r);
            const double value = -derivative(omega, r) / (4.0 * std::numbers::pi * r * energyMinusOmegaL * energyMinusOmegaL) * integral;
            flux[i] = std::isfinite(value) ? std::max(value, 0.0) : 0.0;
        }
        return flux;
    }

    constexpr int kDiskProfileSamples = 64;

    // Radial temperature profile of the disk for the shaders: kDiskProfileSamples samples of T / PeakTemperature,
    // logarithmically spaced between InnerRadius (the ISCO) and OuterRadius.
    struct DiskProfile
    {
        double PeakTemperature{0.0}; // K
        double InnerRadius{0.0};     // geometric units of the KerrNewmanParams
        double OuterRadius{0.0};
        std::array<float, kDiskProfileSamples> Temperature{};
    };

    // T = (F / sigma)^(1/4) with F = Mdot c^6 / (G^2 M^2) * F_dimensionless(r / M) and
    // Mdot = eddingtonRatio * L_Edd / (eta c^2), L_Edd = 4 pi G M m_p c / sigma_T.
    [[nodiscard]] inline DiskProfile BuildDiskProfile(const KerrNewmanParams& p, const double massSolar,
                                                      const double eddingtonRatio, const double outerRadius)
    {
        DiskProfile profile;
        if (p.Mass <= 0.0 || massSolar <= 0.0) return profile;

        using namespace GPP::Units;
        constexpr double kG = kGravitationalConstant;
        constexpr double kC = kSpeedOfLight;

        // Shape in units of M (dimensionless), scaled back to the caller's units at the end.
        const KerrNewmanParams unit{.Mass = 1.0, .SpinParameter = p.SpinParameter / p.Mass, .Charge = p.Charge / p.Mass};
        const double rIn = ProgradeIscoRadius(unit);
        const double rOut = std::max(outerRadius / p.Mass, rIn * 1.01);
        std::array<double, kDiskProfileSamples> radii{};
        for (int i = 0; i < kDiskProfileSamples; ++i)
        {
            radii[static_cast<std::size_t>(i)] = rIn * std::pow(rOut / rIn, static_cast<double>(i) / (kDiskProfileSamples - 1));
        }
        const auto flux = PageThorneFlux(unit, radii);

        const double massKg = massSolar * kSolarMass;
        const double eddingtonLuminosity = 4.0 * std::numbers::pi * kG * massKg * kProtonMass * kC / kThomsonCrossSection;
        const double efficiency = std::max(RadiativeEfficiency(unit), 1e-3);
        const double accretionRate = std::max(eddingtonRatio, 0.0) * eddingtonLuminosity / (efficiency * kC * kC);
        const double fluxScale = accretionRate * std::pow(kC, 6) / (kG * kG * massKg * massKg);

        double peak = 0.0;
        std::array<double, kDiskProfileSamples> temperature{};
        for (int i = 0; i < kDiskProfileSamples; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            temperature[index] = std::pow(fluxScale * flux[index] / kStefanBoltzmann, 0.25);
            peak = std::max(peak, temperature[index]);
        }

        profile.PeakTemperature = peak;
        profile.InnerRadius = rIn * p.Mass;
        profile.OuterRadius = rOut * p.Mass;
        for (int i = 0; i < kDiskProfileSamples; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            profile.Temperature[index] = peak > 0.0 ? static_cast<float>(temperature[index] / peak) : 0.0f;
        }
        return profile;
    }
}
