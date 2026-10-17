export module MoleHole:Rendering.Metric;

import std;

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
}
