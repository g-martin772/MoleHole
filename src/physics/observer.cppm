export module MoleHole:Physics.Observer;

import std;
import :Rendering.Metric;
import :Physics.Geodesic;

// Observers and their local orthonormal frames (tetrads) in the Kerr–Newman family, Boyer–Lindquist coordinates,
// G = c = 1. Every observer is described by its 3-velocity relative to the local zero-angular-momentum observer
// (ZAMO), the reference frame the renderer already launches photons from (shaders/kerr.glsl). Vector components
// are ordered (t, r, theta, phi); spatial ZAMO-frame components are ordered (r̂, θ̂, φ̂).
// Background and sources: docs/notes/observers.md
export namespace MoleHole
{
    // Camera observer presets; mirrors RenderSettings::Observer and the `observer.frame` render setting.
    enum class ObserverKind : std::int32_t
    {
        Zamo = 0,     // zero angular momentum, co-rotates with the frame dragging; exists everywhere outside r+
        Static = 1,   // at rest relative to the distant stars; only outside the ergosphere
        Orbiting = 2, // rotates with the angular velocity of the circular equatorial orbit at its r
        Raindrop = 3, // falls freely from rest at infinity (E = 1, L = 0, Carter's constant 0)
    };

    using Vector4 = std::array<double, 4>;
    using Vector3 = std::array<double, 3>;

    // Rows are e_(a)^μ: row 0 is the observer's 4-velocity, rows 1-3 its spatial axes.
    using Tetrad = std::array<Vector4, 4>;

    struct ZamoFrame
    {
        double Alpha{1.0}; // lapse: dτ/dt of the ZAMO
        double Omega{0.0}; // frame-dragging angular velocity dφ/dt of the ZAMO
        double Varpi{0.0}; // circumferential radius sqrt(g_φφ)
        double SqrtGrr{1.0};
        double SqrtGthth{1.0};
    };

    [[nodiscard]] inline ZamoFrame EvaluateZamoFrame(const KerrNewmanParams& p, const double r, const double theta)
    {
        const double a2 = p.SpinParameter * p.SpinParameter;
        const double s = std::sin(theta);
        const double c = std::cos(theta);
        const double sigma = r * r + a2 * c * c;
        const double delta = HorizonDelta(p, r);
        const double big = (r * r + a2) * (r * r + a2) - delta * a2 * s * s;
        return ZamoFrame{
            .Alpha = std::sqrt(std::max(sigma * delta / big, 0.0)),
            .Omega = p.SpinParameter * (2.0 * p.Mass * r - p.Charge * p.Charge) / big,
            .Varpi = std::sqrt(big / sigma) * std::abs(s),
            .SqrtGrr = std::sqrt(sigma / delta),
            .SqrtGthth = std::sqrt(sigma),
        };
    }

    // Covariant metric g_μν at (r, theta).
    [[nodiscard]] inline std::array<Vector4, 4> MetricTensor(const KerrNewmanParams& p, const double r, const double theta)
    {
        const double a = p.SpinParameter;
        const double s = std::sin(theta);
        const double c = std::cos(theta);
        const double sigma = r * r + a * a * c * c;
        const double delta = HorizonDelta(p, r);
        const double big = (r * r + a * a) * (r * r + a * a) - delta * a * a * s * s;
        const double gtphi = -a * s * s * (r * r + a * a - delta) / sigma;
        std::array<Vector4, 4> g{};
        g[0][0] = -(delta - a * a * s * s) / sigma;
        g[0][3] = g[3][0] = gtphi;
        g[1][1] = sigma / delta;
        g[2][2] = sigma;
        g[3][3] = big * s * s / sigma;
        return g;
    }

    [[nodiscard]] inline double MetricDot(const std::array<Vector4, 4>& g, const Vector4& x, const Vector4& y)
    {
        double sum = 0.0;
        for (std::size_t m = 0; m < 4; ++m)
        {
            for (std::size_t n = 0; n < 4; ++n) sum += g[m][n] * x[m] * y[n];
        }
        return sum;
    }

    [[nodiscard]] inline double Dot3(const Vector3& x, const Vector3& y) { return x[0] * y[0] + x[1] * y[1] + x[2] * y[2]; }

    [[nodiscard]] inline Tetrad ZamoTetrad(const KerrNewmanParams& p, const double r, const double theta)
    {
        const auto z = EvaluateZamoFrame(p, r, theta);
        return Tetrad{
            Vector4{1.0 / z.Alpha, 0.0, 0.0, z.Omega / z.Alpha},
            Vector4{0.0, 1.0 / z.SqrtGrr, 0.0, 0.0},
            Vector4{0.0, 0.0, 1.0 / z.SqrtGthth, 0.0},
            Vector4{0.0, 0.0, 0.0, 1.0 / z.Varpi},
        };
    }

    // Velocity relative to the ZAMO of an observer at (r, theta) circling the axis with angular velocity
    // Ω = dφ/dt: v_φ̂ = (Ω - ω) ϖ / α. nullopt where that would be faster than light.
    [[nodiscard]] inline std::optional<Vector3> RotatingObserverVelocity(const KerrNewmanParams& p, const double r,
                                                                         const double theta, const double angularVelocity)
    {
        const auto z = EvaluateZamoFrame(p, r, theta);
        const double v = (angularVelocity - z.Omega) * z.Varpi / z.Alpha;
        if (!std::isfinite(v) || !(std::abs(v) < 1.0)) return std::nullopt;
        return Vector3{0.0, 0.0, v};
    }

    // Velocity relative to the local ZAMO of a particle on a timelike geodesic: the spatial part of its momentum
    // in the ZAMO frame over its energy there, ε = (E - ω L) / α.
    [[nodiscard]] inline Vector3 ZamoVelocity(const KerrNewmanParams& p, const GeodesicState<double>& state)
    {
        const auto z = EvaluateZamoFrame(p, state.R, state.Theta);
        const double energy = (state.Energy - z.Omega * state.AngularMomentum) / z.Alpha;
        // On the axis ϖ = 0 and only L = 0 is possible
        const double azimuthal = state.AngularMomentum == 0.0 ? 0.0 : state.AngularMomentum / z.Varpi / energy;
        return {state.Pr / z.SqrtGrr / energy, state.Ptheta / z.SqrtGthth / energy, azimuthal};
    }

    // Velocity relative to the ZAMO of a preset observer at (r, theta); nullopt where that observer cannot exist
    // (static inside the ergosphere, orbiting inside the photon orbit, anything at or inside the horizon).
    // Orbiting observers off the equatorial plane keep the angular velocity of the equatorial orbit at the same r;
    // they are not on a geodesic there.
    [[nodiscard]] inline std::optional<Vector3> ObserverVelocity(const KerrNewmanParams& p, const ObserverKind kind,
                                                                 const double r, const double theta,
                                                                 const OrbitDirection direction = OrbitDirection::Prograde)
    {
        if (!(r > OuterHorizonRadius(p)) || p.Mass <= 0.0) return std::nullopt;
        switch (kind)
        {
        case ObserverKind::Zamo: return Vector3{0.0, 0.0, 0.0};
        case ObserverKind::Static: return RotatingObserverVelocity(p, r, theta, 0.0);
        case ObserverKind::Orbiting:
        {
            if (!(CircularOrbitNorm(p, r, direction) > 0.0)) return std::nullopt;
            return RotatingObserverVelocity(p, r, theta, OrbitalFrequency(p, r, direction));
        }
        case ObserverKind::Raindrop:
        {
            const auto state = MakeGeodesic<double>(p, GeodesicKind::Timelike, r, theta, 0.0, 1.0, 0.0, 0.0, -1.0);
            if (!state) return std::nullopt;
            return ZamoVelocity(p, *state);
        }
        }
        return std::nullopt;
    }

    [[nodiscard]] inline double LorentzFactor(const Vector3& v) { return 1.0 / std::sqrt(1.0 - Dot3(v, v)); }

    // The ZAMO tetrad boosted by v: the frame of an observer moving with velocity v through the ZAMO, with its
    // spatial axes aligned with the ZAMO's (pure boost, no rotation). Row 0 is its 4-velocity u = γ (e_t̂ + v^i e_î).
    [[nodiscard]] inline Tetrad ObserverTetrad(const KerrNewmanParams& p, const double r, const double theta,
                                               const Vector3& velocity)
    {
        const auto zamo = ZamoTetrad(p, r, theta);
        const double gamma = LorentzFactor(velocity);
        // (γ - 1) / v², without the cancellation at low speed
        const double boost = gamma * gamma / (gamma + 1.0);
        Tetrad result{};
        for (std::size_t m = 0; m < 4; ++m)
        {
            result[0][m] = gamma * (zamo[0][m] + velocity[0] * zamo[1][m] + velocity[1] * zamo[2][m]
                                    + velocity[2] * zamo[3][m]);
        }
        for (std::size_t j = 0; j < 3; ++j)
        {
            for (std::size_t m = 0; m < 4; ++m)
            {
                double spatial = zamo[j + 1][m];
                for (std::size_t k = 0; k < 3; ++k) spatial += boost * velocity[j] * velocity[k] * zamo[k + 1][m];
                result[j + 1][m] = gamma * velocity[j] * zamo[0][m] + spatial;
            }
        }
        return result;
    }

    // 4-velocity u^μ of an observer moving with velocity v relative to the ZAMO.
    [[nodiscard]] inline Vector4 FourVelocity(const KerrNewmanParams& p, const double r, const double theta,
                                              const Vector3& velocity)
    {
        return ObserverTetrad(p, r, theta, velocity)[0];
    }

    // Orthonormal frame of an arbitrary timelike 4-velocity u by Gram–Schmidt against the coordinate directions
    // ∂_r, ∂_θ, ∂_φ. Spans the same rest space as ObserverTetrad, but its axes may be rotated relative to it.
    [[nodiscard]] inline Tetrad GramSchmidtTetrad(const KerrNewmanParams& p, const double r, const double theta,
                                                  const Vector4& fourVelocity)
    {
        const auto g = MetricTensor(p, r, theta);
        Tetrad result{};
        const double norm = std::sqrt(-MetricDot(g, fourVelocity, fourVelocity));
        for (std::size_t m = 0; m < 4; ++m) result[0][m] = fourVelocity[m] / norm;
        for (std::size_t a = 1; a < 4; ++a)
        {
            Vector4 e{};
            e[a] = 1.0;
            // Timelike e_0 has norm -1, so projecting it out adds (e · e_0) e_0.
            const double time = MetricDot(g, e, result[0]);
            for (std::size_t m = 0; m < 4; ++m) e[m] += time * result[0][m];
            for (std::size_t b = 1; b < a; ++b)
            {
                const double overlap = MetricDot(g, e, result[b]);
                for (std::size_t m = 0; m < 4; ++m) e[m] -= overlap * result[b][m];
            }
            const double length = std::sqrt(MetricDot(g, e, e));
            for (std::size_t m = 0; m < 4; ++m) result[a][m] = e[m] / length;
        }
        return result;
    }

    // Rate dτ/dt of the observer's clock against Boyer–Lindquist time, the clock of a distant static observer:
    // 1 / u^t = α / γ. Smaller than 1 means the observer ages more slowly.
    [[nodiscard]] inline double ProperTimeRate(const KerrNewmanParams& p, const double r, const double theta,
                                               const Vector3& velocity)
    {
        return EvaluateZamoFrame(p, r, theta).Alpha / LorentzFactor(velocity);
    }

    // |∇f| = √(g^rr (∂_r f)² + g^θθ (∂_θ f)²) of a stationary, axisymmetric scalar f(r, θ), by central differences.
    [[nodiscard]] inline double GradientMagnitude(const KerrNewmanParams& p, const double r, const double theta,
                                                  const std::function<double(double, double)>& f)
    {
        const double hr = 1e-6 * std::max(r - OuterHorizonRadius(p), 1e-9 * p.Mass);
        const double ht = 1e-6;
        const double dr = (f(r + hr, theta) - f(r - hr, theta)) / (2.0 * hr);
        const double dt = (f(r, theta + ht) - f(r, theta - ht)) / (2.0 * ht);
        const double sigma = r * r + p.SpinParameter * p.SpinParameter * std::cos(theta) * std::cos(theta);
        return std::sqrt(HorizonDelta(p, r) / sigma * dr * dr + dt * dt / sigma);
    }

    // Proper acceleration (in 1/M; times c²/r_g for SI) of an observer moving along the Killing vector
    // χ = ∂_t + Ω ∂_φ with constant Ω: a_μ = ∂_μ ln |χ|. Static observers have Ω = 0, rigidly rotating ones any Ω.
    [[nodiscard]] inline double KillingObserverAcceleration(const KerrNewmanParams& p, const double r, const double theta,
                                                            const double angularVelocity)
    {
        return GradientMagnitude(p, r, theta, [&](const double rr, const double tt)
        {
            const auto g = MetricTensor(p, rr, tt);
            return 0.5 * std::log(-(g[0][0] + 2.0 * angularVelocity * g[0][3] + angularVelocity * angularVelocity * g[3][3]));
        });
    }

    // The ZAMO is not a Killing observer (ω varies from place to place); its acceleration is a = ∇ ln α.
    [[nodiscard]] inline double ZamoAcceleration(const KerrNewmanParams& p, const double r, const double theta)
    {
        return GradientMagnitude(p, r, theta, [&](const double rr, const double tt)
        {
            return std::log(EvaluateZamoFrame(p, rr, tt).Alpha);
        });
    }

    // Proper acceleration a preset observer needs to keep its motion, in 1/M: what an accelerometer on board reads.
    // Zero for the free-falling ones. nullopt where the observer cannot exist.
    [[nodiscard]] inline std::optional<double> ProperAcceleration(const KerrNewmanParams& p, const ObserverKind kind,
                                                                  const double r, const double theta,
                                                                  const OrbitDirection direction = OrbitDirection::Prograde)
    {
        if (!ObserverVelocity(p, kind, r, theta, direction)) return std::nullopt;
        switch (kind)
        {
        case ObserverKind::Zamo: return ZamoAcceleration(p, r, theta);
        case ObserverKind::Static: return KillingObserverAcceleration(p, r, theta, 0.0);
        case ObserverKind::Orbiting:
            return KillingObserverAcceleration(p, r, theta, OrbitalFrequency(p, r, direction));
        case ObserverKind::Raindrop: return 0.0;
        }
        return std::nullopt;
    }

    struct BoostedPhoton
    {
        Vector3 Direction{};    // unit propagation direction in the target frame
        double EnergyRatio{1.0}; // ε_target / ε_source
    };

    // Special-relativistic aberration and Doppler shift of a photon between two co-located frames: given its unit
    // propagation direction k' in a frame moving with velocity v relative to the target frame, returns its direction
    // and energy in the target frame: ε / ε' = γ (1 + v·k'), cos θ = (cos θ' + v) / (1 + v cos θ').
    [[nodiscard]] inline BoostedPhoton BoostPhoton(const Vector3& velocity, const Vector3& direction)
    {
        const double gamma = LorentzFactor(velocity);
        const double along = Dot3(velocity, direction);
        const double scale = gamma * gamma / (gamma + 1.0) * along + gamma;
        Vector3 momentum{};
        for (std::size_t i = 0; i < 3; ++i) momentum[i] = direction[i] + scale * velocity[i];
        const double energy = gamma * (1.0 + along);
        return {{momentum[0] / energy, momentum[1] / energy, momentum[2] / energy}, energy};
    }
}
