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

    // Thorne (1974) limit; also keeps a² + Q² < M² so the horizon exists (no naked singularity). The default for
    // every black hole.
    constexpr double kMaxHorizonParameter = 0.998;
    // Near-extremal holes, beyond the Thorne limit (Interstellar's Gargantua: a/M = 1 - 1.3e-14), may come up to
    // 1 - kMinExtremalGap. Closer to 1 the gap is no longer resolved by double precision near r = M.
    constexpr double kMinExtremalGap = 1e-15;
    constexpr double kExtremalHorizonParameter = 1.0 - kMinExtremalGap;

    struct KerrNewmanParams
    {
        double Mass{1.0};
        double SpinParameter{0.0}; // a = J / M, in units of length
        double Charge{0.0};        // geometrized charge Q
    };

    // `spin` and `charge` are the dimensionless BlackHoleComponent fields a/M and Q/M. The metric type
    // zeroes the parameters it does not model, then √(a² + Q²) is clamped to `maxMagnitude` M.
    [[nodiscard]] inline KerrNewmanParams MakeKerrNewmanParams(const std::int32_t metricType, const double mass,
                                                                const double spin, const double charge,
                                                                const double maxMagnitude = kMaxHorizonParameter)
    {
        const auto kind = static_cast<MetricKind>(metricType);
        const bool rotating = kind == MetricKind::Kerr || kind == MetricKind::KerrNewman;
        const bool charged = kind == MetricKind::ReissnerNordstrom || kind == MetricKind::KerrNewman;

        double a = rotating ? std::abs(spin) : 0.0;
        double q = charged ? std::abs(charge) : 0.0;
        const double limit = std::min(maxMagnitude, kExtremalHorizonParameter);
        const double magnitude = std::sqrt(a * a + q * q);
        if (magnitude > limit)
        {
            a *= limit / magnitude;
            q *= limit / magnitude;
        }

        const double m = std::max(mass, 0.0);
        return KerrNewmanParams{.Mass = m, .SpinParameter = a * m, .Charge = q * m};
    }

    // M² - a² - Q², written so that it keeps its relative precision for near-extremal holes, where it is tiny.
    [[nodiscard]] inline double HorizonDiscriminant(const KerrNewmanParams& p)
    {
        return (p.Mass - p.SpinParameter) * (p.Mass + p.SpinParameter) - p.Charge * p.Charge;
    }

    [[nodiscard]] inline double OuterHorizonRadius(const KerrNewmanParams& p)
    {
        return p.Mass + std::sqrt(std::max(HorizonDiscriminant(p), 0.0));
    }

    [[nodiscard]] inline double InnerHorizonRadius(const KerrNewmanParams& p)
    {
        return p.Mass - std::sqrt(std::max(HorizonDiscriminant(p), 0.0));
    }

    // Δ = r² - 2Mr + a² + Q² in the factored form (r - r+)(r - r-): the expanded form loses all precision close
    // to the horizon of a near-extremal hole, where Δ is far below the size of its terms.
    [[nodiscard]] inline double HorizonDelta(const KerrNewmanParams& p, const double r)
    {
        return (r - OuterHorizonRadius(p)) * (r - InnerHorizonRadius(p));
    }

    // Static limit: inside it no observer can stay at rest relative to the distant stars.
    [[nodiscard]] inline double ErgosphereRadius(const KerrNewmanParams& p, const double theta)
    {
        const double m = p.Mass;
        const double ac = p.SpinParameter * std::cos(theta);
        return m + std::sqrt(std::max((m - ac) * (m + ac) - p.Charge * p.Charge, 0.0));
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

    // Prograde orbits co-rotate with the hole (L > 0 for a > 0), retrograde ones counter-rotate.
    enum class OrbitDirection : std::int32_t { Prograde = 0, Retrograde = 1 };

    // Circular equatorial orbits of Kerr (Q = 0) in the closed forms of Bardeen, Press & Teukolsky (1972), eqs.
    // 2.12-2.16, with x = √(r/M) and â = a/M. The combinations that cancel near r = M are factored, using
    // x - 1 and 1 - â computed directly, so the results keep their precision for near-extremal holes, where the ISCO
    // approaches r = M (for â = 1 - 1.3e-14 it lies at 1.00004 M and u^t ≈ 6e4).
    struct KerrCircularOrbit
    {
        double Omega{0.0};       // dφ/dt
        double Denominator{0.0}; // x³ - 3x ± 2â: positive outside the photon orbit
        double Ut{0.0};          // dt/dτ; NaN where Denominator ≤ 0
        double Energy{0.0};      // -u_t
        double AngularMomentum{0.0};
    };

    [[nodiscard]] inline KerrCircularOrbit EvaluateKerrCircularOrbit(const KerrNewmanParams& p, const double r,
                                                                     const OrbitDirection direction)
    {
        const double m = p.Mass;
        const double spin = p.SpinParameter / m;
        const double gap = (m - p.SpinParameter) / m; // 1 - â
        const bool prograde = direction == OrbitDirection::Prograde;
        const double s = prograde ? 1.0 : -1.0;
        const double x = std::sqrt(r / m);
        const double xMinusOne = (r - m) / m / (x + 1.0);
        const double x3 = x * x * x;
        // x³ - 3x ± 2â, x³ - 2x ± â and x² ∓ â
        const double denominator = prograde ? xMinusOne * xMinusOne * (x + 2.0) - 2.0 * gap
                                            : (x + 1.0) * (x + 1.0) * (x - 2.0) + 2.0 * gap;
        const double energyNumerator = prograde ? xMinusOne * (x * x + x - 1.0) - gap
                                                : (x + 1.0) * (x * x - x - 1.0) + gap;
        const double squareMinusSpin = prograde ? xMinusOne * (x + 1.0) + gap : x * x + spin;
        // x⁴ ∓ 2âx + â² = (x² ∓ â)² ± 2âx (x - 1)
        const double angularNumerator = squareMinusSpin * squareMinusSpin + 2.0 * s * spin * x * xMinusOne;
        const double root = x * std::sqrt(x) * std::sqrt(denominator);
        return KerrCircularOrbit{
            .Omega = s / (m * (x3 + s * spin)),
            .Denominator = denominator,
            .Ut = (x3 + s * spin) / root,
            .Energy = energyNumerator / root,
            .AngularMomentum = s * m * angularNumerator / root,
        };
    }

    // Charged holes have no closed forms like these; they take the general path through the equatorial metric.
    [[nodiscard]] inline bool UsesKerrClosedForms(const KerrNewmanParams& p) { return p.Charge == 0.0 && p.Mass > 0.0; }

    // Angular velocity dφ/dt of a circular equatorial geodesic at Boyer–Lindquist radius r.
    // Reduces to ±1 / (r^{3/2} / √M ± a) for Kerr.
    [[nodiscard]] inline double OrbitalFrequency(const KerrNewmanParams& p, const double r, const OrbitDirection direction)
    {
        if (UsesKerrClosedForms(p)) return EvaluateKerrCircularOrbit(p, r, direction).Omega;
        const auto g = EvaluateEquatorialMetric(p, r);
        const double discriminant = g.DGtphi * g.DGtphi - g.DGtt * g.DGphiphi;
        if (discriminant < 0.0 || g.DGphiphi == 0.0) return std::numeric_limits<double>::quiet_NaN();
        const double root = direction == OrbitDirection::Prograde ? std::sqrt(discriminant) : -std::sqrt(discriminant);
        return (-g.DGtphi + root) / g.DGphiphi;
    }

    [[nodiscard]] inline double ProgradeOrbitalFrequency(const KerrNewmanParams& p, const double r)
    {
        return OrbitalFrequency(p, r, OrbitDirection::Prograde);
    }

    // (dτ/dt)² = -(g_tt + 2 g_tφ Ω + g_φφ Ω²) of the circular orbit at r: positive where the orbit is timelike,
    // zero on the photon orbit, negative inside it.
    [[nodiscard]] inline double CircularOrbitNorm(const KerrNewmanParams& p, const double r, const OrbitDirection direction)
    {
        if (UsesKerrClosedForms(p))
        {
            const auto orbit = EvaluateKerrCircularOrbit(p, r, direction);
            if (!(orbit.Denominator > 0.0)) return orbit.Denominator;
            return 1.0 / (orbit.Ut * orbit.Ut);
        }
        const double omega = OrbitalFrequency(p, r, direction);
        if (!std::isfinite(omega)) return std::numeric_limits<double>::quiet_NaN();
        const auto g = EvaluateEquatorialMetric(p, r);
        return -(g.Gtt + 2.0 * g.Gtphi * omega + g.Gphiphi * omega * omega);
    }

    // Specific energy -u_t of the circular orbit at r; +inf where no timelike orbit exists
    // (at or inside the photon orbit).
    [[nodiscard]] inline double CircularOrbitEnergy(const KerrNewmanParams& p, const double r,
                                                    const OrbitDirection direction = OrbitDirection::Prograde)
    {
        if (UsesKerrClosedForms(p))
        {
            const auto orbit = EvaluateKerrCircularOrbit(p, r, direction);
            return orbit.Denominator > 0.0 ? orbit.Energy : std::numeric_limits<double>::infinity();
        }
        const double norm = CircularOrbitNorm(p, r, direction);
        if (!(norm > 0.0)) return std::numeric_limits<double>::infinity();
        const double omega = OrbitalFrequency(p, r, direction);
        const auto g = EvaluateEquatorialMetric(p, r);
        return -(g.Gtt + g.Gtphi * omega) / std::sqrt(norm);
    }

    // Innermost stable circular orbit: the minimum of E(r) over the circular orbits. Kerr uses the closed form of
    // Bardeen, Press & Teukolsky (1972), eq. 2.21; with charge there is none, so E(r) is minimised numerically.
    [[nodiscard]] inline double IscoRadius(const KerrNewmanParams& p, const OrbitDirection direction)
    {
        if (p.Mass <= 0.0) return 0.0;
        if (UsesKerrClosedForms(p))
        {
            const double spin = p.SpinParameter / p.Mass;
            const double gap = (p.Mass - p.SpinParameter) / p.Mass;
            // 1 - â² = (1 - â)(1 + â)
            const double z1 = 1.0 + std::cbrt(gap * (1.0 + spin)) * (std::cbrt(1.0 + spin) + std::cbrt(gap));
            const double z2 = std::sqrt(3.0 * spin * spin + z1 * z1);
            const double root = std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2));
            return p.Mass * (3.0 + z2 + (direction == OrbitDirection::Prograde ? -root : root));
        }

        const double rLow = OuterHorizonRadius(p) * (1.0 + 1e-6);
        const double rHigh = 12.0 * p.Mass;
        constexpr int kSamples = 4096;

        int best = -1;
        double bestEnergy = std::numeric_limits<double>::infinity();
        for (int i = 0; i <= kSamples; ++i)
        {
            const double r = rLow + (rHigh - rLow) * static_cast<double>(i) / kSamples;
            const double energy = CircularOrbitEnergy(p, r, direction);
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
            if (CircularOrbitEnergy(p, x1, direction) < CircularOrbitEnergy(p, x2, direction)) hi = x2;
            else lo = x1;
        }
        return 0.5 * (lo + hi);
    }

    [[nodiscard]] inline double ProgradeIscoRadius(const KerrNewmanParams& p)
    {
        return IscoRadius(p, OrbitDirection::Prograde);
    }

    // Specific angular momentum u_phi of the circular orbit at r.
    [[nodiscard]] inline double CircularOrbitAngularMomentum(const KerrNewmanParams& p, const double r,
                                                             const OrbitDirection direction = OrbitDirection::Prograde)
    {
        if (UsesKerrClosedForms(p))
        {
            const auto orbit = EvaluateKerrCircularOrbit(p, r, direction);
            return orbit.Denominator > 0.0 ? orbit.AngularMomentum : std::numeric_limits<double>::quiet_NaN();
        }
        const double norm = CircularOrbitNorm(p, r, direction);
        if (!(norm > 0.0)) return std::numeric_limits<double>::quiet_NaN();
        const double omega = OrbitalFrequency(p, r, direction);
        const auto g = EvaluateEquatorialMetric(p, r);
        return (g.Gtphi + g.Gphiphi * omega) / std::sqrt(norm);
    }

    // Bisection for the boundary of a predicate that is false at lo and true at hi.
    [[nodiscard]] inline double BisectBoundary(double lo, double hi, const std::function<bool(double)>& isAbove)
    {
        for (int i = 0; i < 200 && hi - lo > 1e-15 * hi; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            if (isAbove(mid)) hi = mid;
            else lo = mid;
        }
        return 0.5 * (lo + hi);
    }

    // Circular photon orbit in the equatorial plane: where circular orbits stop being timelike. Every circular
    // orbit further out is timelike, every one further in (down to the horizon) is spacelike.
    // Kerr: 2M (1 + cos(2/3 arccos(∓a/M))) (BPT eq. 2.18); Reissner–Nordström: (3M + √(9M² − 8Q²)) / 2.
    [[nodiscard]] inline double PhotonOrbitRadius(const KerrNewmanParams& p, const OrbitDirection direction)
    {
        if (p.Mass <= 0.0) return 0.0;
        if (UsesKerrClosedForms(p))
        {
            const double spin = p.SpinParameter / p.Mass;
            const double sign = direction == OrbitDirection::Prograde ? -1.0 : 1.0;
            return 2.0 * p.Mass * (1.0 + std::cos(2.0 / 3.0 * std::acos(sign * spin)));
        }
        return BisectBoundary(OuterHorizonRadius(p), 12.0 * p.Mass,
                              [&](const double r) { return CircularOrbitNorm(p, r, direction) > 0.0; });
    }

    // Marginally bound circular orbit, E = 1: a particle falling in from rest at infinity can reach it but no
    // closer without being captured. Kerr: 2M ∓ a + 2 √M √(M ∓ a) (BPT eq. 2.19).
    [[nodiscard]] inline double MarginallyBoundRadius(const KerrNewmanParams& p, const OrbitDirection direction)
    {
        if (p.Mass <= 0.0) return 0.0;
        if (UsesKerrClosedForms(p))
        {
            const double a = direction == OrbitDirection::Prograde ? p.SpinParameter : -p.SpinParameter;
            return 2.0 * p.Mass - a + 2.0 * std::sqrt(p.Mass) * std::sqrt(p.Mass - a);
        }
        return BisectBoundary(PhotonOrbitRadius(p, direction), IscoRadius(p, direction),
                              [&](const double r) { return CircularOrbitEnergy(p, r, direction) < 1.0; });
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
        double PeakTemperature{0.0}; // K, in the rest frame of the gas
        // K: the hottest gas as a distant face-on observer sees it, max T(r) / u^t(r) (the redshift of photons with
        // zero angular momentum). Unlike the rest-frame peak it barely depends on spin, which keeps it out of the
        // deep, strongly redshifted throat of a near-extremal hole.
        double ObservedPeakTemperature{0.0};
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
        double observedPeak = 0.0;
        std::array<double, kDiskProfileSamples> temperature{};
        for (int i = 0; i < kDiskProfileSamples; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            temperature[index] = std::pow(fluxScale * flux[index] / kStefanBoltzmann, 0.25);
            peak = std::max(peak, temperature[index]);
            const double norm = CircularOrbitNorm(unit, radii[index], OrbitDirection::Prograde);
            if (norm > 0.0) observedPeak = std::max(observedPeak, temperature[index] * std::sqrt(norm));
        }

        profile.PeakTemperature = peak;
        profile.ObservedPeakTemperature = observedPeak;
        profile.InnerRadius = rIn * p.Mass;
        profile.OuterRadius = rOut * p.Mass;
        for (int i = 0; i < kDiskProfileSamples; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            profile.Temperature[index] = peak > 0.0 ? static_cast<float>(temperature[index] / peak) : 0.0f;
        }
        return profile;
    }

    // Disk exposure: the shaders scale disk brightness as (g T / reference)⁴. The reference is the observed peak
    // (DiskProfile::ObservedPeakTemperature), scaled so that a Schwarzschild disk keeps the brightness it had when the
    // reference was the rest-frame peak. With the rest-frame peak, a spinning disk was exposed for gas hidden deep in
    // its redshifted throat and looked far too dim (about 40x at a = 0.998, 200x near-extremal).
    // See docs/notes/near_extremal.md.
    [[nodiscard]] inline double SchwarzschildObservedPeakRatio()
    {
        static const double ratio = []
        {
            const auto profile = BuildDiskProfile({.Mass = 1.0}, 1.0, 0.1, 20.0);
            return profile.PeakTemperature > 0.0 ? profile.ObservedPeakTemperature / profile.PeakTemperature : 1.0;
        }();
        return ratio;
    }

    [[nodiscard]] inline double DiskExposureReference(const DiskProfile& profile)
    {
        if (!(profile.ObservedPeakTemperature > 0.0)) return profile.PeakTemperature;
        return profile.ObservedPeakTemperature / SchwarzschildObservedPeakRatio();
    }
}
