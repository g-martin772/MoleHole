export module MoleHole:Physics.Geodesic;

import std;
import :Rendering.Metric;

// Timelike and null geodesics of the Kerr–Newman family in Boyer–Lindquist coordinates (G = c = 1, lengths and
// times in units of the caller's mass). CPU counterpart of shaders/kerr.glsl: the same Carter-form Hamiltonian,
// plus coordinate time, the rest-mass term and an adaptive Dormand–Prince 5(4) integrator.
// Test particles are neutral; Q only enters through the metric. Background and sources: docs/notes/geodesics.md
export namespace MoleHole
{
    enum class GeodesicKind : std::int32_t { Timelike = 0, Null = 1 };
    enum class GeodesicStatus : std::int32_t { Ok = 0, Captured = 1, StepLimit = 2, NonFinite = 3 };

    template <typename Real>
    struct KerrNewmanSpacetime
    {
        Real M{1};
        Real A{0};
        Real Q{0};
        Real HorizonRadius{2};      // r+
        Real InnerHorizonRadius{0}; // r-
    };

    // Δ = r² - 2Mr + a² + Q² as (r - r+)(r - r-), which keeps its precision close to the horizon of a near-extremal
    // hole (see HorizonDelta in src/rendering/metric.cppm).
    template <typename Real>
    [[nodiscard]] Real HorizonDelta(const KerrNewmanSpacetime<Real>& st, const Real r)
    {
        return (r - st.HorizonRadius) * (r - st.InnerHorizonRadius);
    }

    template <typename Real>
    [[nodiscard]] KerrNewmanSpacetime<Real> MakeSpacetime(const KerrNewmanParams& p)
    {
        const Real m = static_cast<Real>(p.Mass);
        const Real a = static_cast<Real>(p.SpinParameter);
        const Real q = static_cast<Real>(p.Charge);
        const Real root = std::sqrt(std::max((m - a) * (m + a) - q * q, Real(0)));
        return {.M = m, .A = a, .Q = q, .HorizonRadius = m + root, .InnerHorizonRadius = m - root};
    }

    // Momenta are covariant and per unit rest mass, so along a timelike geodesic the affine parameter is the
    // proper time and p^μ = u^μ. A null geodesic has no proper time; Lambda is then only its affine parameter.
    template <typename Real>
    struct GeodesicState
    {
        Real R{};
        Real Theta{};
        Real Phi{};
        Real T{};
        Real Pr{};
        Real Ptheta{};
        Real Energy{};          // E = -p_t, conserved
        Real AngularMomentum{}; // L = p_phi, conserved
        Real Lambda{};          // affine parameter; proper time for timelike geodesics
        Real Step{};            // magnitude of the next adaptive step in Lambda; 0 picks one
        GeodesicKind Kind{GeodesicKind::Timelike};
        GeodesicStatus Status{GeodesicStatus::Ok};
    };

    struct GeodesicTolerance
    {
        double Relative{1e-10};
        double Absolute{1e-12};
        // Captured once r < r+ + margin · max(√(M² - a² - Q²), 1e-6 M): 1e-3 M for Schwarzschild, shrinking with the
        // horizon gap so the ISCO of a near-extremal hole (1.00004 M for a = 1 - 1.3e-14) stays outside it.
        // BL coordinates are singular at r+.
        double HorizonMargin{1e-3};
        std::int64_t MaxSteps{10'000'000};
    };

    // (r, theta, p_r, p_theta, phi, t): the integrated variables. E and L are constants of motion.
    template <typename Real>
    using GeodesicVector = std::array<Real, 6>;

    template <typename Real>
    [[nodiscard]] constexpr Real RestMassSquared(const GeodesicKind kind)
    {
        return kind == GeodesicKind::Timelike ? Real(1) : Real(0);
    }

    // sin(theta) kept away from zero; only orbits with L = 0 reach the axis, and for those T / sin(theta) is finite.
    template <typename Real>
    [[nodiscard]] Real SafeSin(const Real s)
    {
        const auto limit = static_cast<Real>(1e-4);
        return std::abs(s) < limit ? (s < Real(0) ? -limit : limit) : s;
    }

    // Hamilton's equations for H = K / (2 Sigma) in Carter's separated form with p_t = -E and p_phi = L:
    //   K = Delta p_r^2 + p_theta^2 + (L / sin(theta) - a E sin(theta))^2 - ((r^2 + a^2) E - a L)^2 / Delta
    // K / Sigma is the full contraction g^{μν} p_μ p_ν, so the same equations hold for any rest mass; H = -μ²/2
    // is fixed by the initial data. dt/dλ = -∂H/∂E, dφ/dλ = ∂H/∂L.
    template <typename Real>
    [[nodiscard]] GeodesicVector<Real> GeodesicDerivative(const KerrNewmanSpacetime<Real>& st, const Real energy,
                                                          const Real angularMomentum, const GeodesicVector<Real>& y)
    {
        const Real r = y[0];
        const Real pr = y[2];
        const Real pth = y[3];
        const Real a = st.A;
        const Real a2 = a * a;
        const Real s = SafeSin(std::sin(y[1]));
        const Real ct = std::cos(y[1]);

        const Real sigma = r * r + a2 * ct * ct;
        const Real delta = HorizonDelta(st, r);
        const Real p = (r * r + a2) * energy - a * angularMomentum;
        const Real t = angularMomentum / s - a * energy * s;
        const Real k = delta * pr * pr + pth * pth + t * t - p * p / delta;

        const Real dDelta = Real(2) * r - Real(2) * st.M;
        const Real dP = Real(2) * r * energy;
        const Real kR = dDelta * pr * pr - (Real(2) * p * dP * delta - p * p * dDelta) / (delta * delta);
        const Real kTheta = Real(2) * t * (-angularMomentum * ct / (s * s) - a * energy * ct);
        const Real sigmaR = Real(2) * r;
        const Real sigmaTheta = Real(-2) * a2 * ct * s;
        const Real half = static_cast<Real>(0.5);

        return {
            delta * pr / sigma,
            pth / sigma,
            -half * (kR - k * sigmaR / sigma) / sigma,
            -half * (kTheta - k * sigmaTheta / sigma) / sigma,
            (t / s + a * p / delta) / sigma,
            (a * s * t + (r * r + a2) * p / delta) / sigma,
        };
    }

    template <typename Real>
    [[nodiscard]] GeodesicVector<Real> ToVector(const GeodesicState<Real>& state)
    {
        return {state.R, state.Theta, state.Pr, state.Ptheta, state.Phi, state.T};
    }

    template <typename Real>
    void FromVector(const GeodesicVector<Real>& y, GeodesicState<Real>& state)
    {
        state.R = y[0];
        state.Theta = y[1];
        state.Pr = y[2];
        state.Ptheta = y[3];
        state.Phi = y[4];
        state.T = y[5];
    }

    template <typename Real>
    [[nodiscard]] GeodesicVector<Real> GeodesicDerivative(const KerrNewmanParams& p, const GeodesicState<Real>& state)
    {
        return GeodesicDerivative(MakeSpacetime<Real>(p), state.Energy, state.AngularMomentum, ToVector(state));
    }

    // H = ½ g^{μν} p_μ p_ν; -½ for a timelike geodesic, 0 for a null one.
    template <typename Real>
    [[nodiscard]] Real GeodesicHamiltonian(const KerrNewmanParams& params, const GeodesicState<Real>& state)
    {
        const auto st = MakeSpacetime<Real>(params);
        const Real a2 = st.A * st.A;
        const Real r = state.R;
        const Real s = SafeSin(std::sin(state.Theta));
        const Real ct = std::cos(state.Theta);
        const Real sigma = r * r + a2 * ct * ct;
        const Real delta = HorizonDelta(st, r);
        const Real p = (r * r + a2) * state.Energy - st.A * state.AngularMomentum;
        const Real t = state.AngularMomentum / s - st.A * state.Energy * s;
        const Real k = delta * state.Pr * state.Pr + state.Ptheta * state.Ptheta + t * t - p * p / delta;
        return k / (Real(2) * sigma);
    }

    // Integration error in the mass-shell condition H = -μ²/2; exactly zero for a perfect integrator.
    template <typename Real>
    [[nodiscard]] Real MassShellResidual(const KerrNewmanParams& params, const GeodesicState<Real>& state)
    {
        return GeodesicHamiltonian(params, state) + static_cast<Real>(0.5) * RestMassSquared<Real>(state.Kind);
    }

    // Carter's constant Q = p_theta² + cos²(theta) (a² (μ² - E²) + L² / sin²(theta)), the third constant of motion
    // that makes Kerr–Newman geodesics integrable. Zero for equatorial orbits.
    template <typename Real>
    [[nodiscard]] Real CarterConstant(const KerrNewmanParams& params, const GeodesicState<Real>& state)
    {
        const auto st = MakeSpacetime<Real>(params);
        const Real s = SafeSin(std::sin(state.Theta));
        const Real ct = std::cos(state.Theta);
        const Real e = state.Energy;
        const Real l = state.AngularMomentum;
        return state.Ptheta * state.Ptheta
               + ct * ct * (st.A * st.A * (RestMassSquared<Real>(state.Kind) - e * e) + l * l / (s * s));
    }

    // Geodesic through (r, theta, phi) with the given E, L and p_theta. p_r follows from the mass-shell condition,
    // with the sign of radialSign; nullopt where that motion is forbidden (p_r² < 0).
    template <typename Real>
    [[nodiscard]] std::optional<GeodesicState<Real>> MakeGeodesic(const KerrNewmanParams& params, const GeodesicKind kind,
                                                                  const double r, const double theta, const double phi,
                                                                  const double energy, const double angularMomentum,
                                                                  const double pTheta, const double radialSign)
    {
        const auto st = MakeSpacetime<Real>(params);
        const Real rr = static_cast<Real>(r);
        const Real e = static_cast<Real>(energy);
        const Real l = static_cast<Real>(angularMomentum);
        const Real pth = static_cast<Real>(pTheta);
        const Real a2 = st.A * st.A;
        const Real s = SafeSin(std::sin(static_cast<Real>(theta)));
        const Real ct = std::cos(static_cast<Real>(theta));
        const Real sigma = rr * rr + a2 * ct * ct;
        const Real delta = HorizonDelta(st, rr);
        if (!(delta > Real(0))) return std::nullopt;

        const Real p = (rr * rr + a2) * e - st.A * l;
        const Real t = l / s - st.A * e * s;
        const Real potential = p * p / delta;
        Real pr2 = (potential - RestMassSquared<Real>(kind) * sigma - pth * pth - t * t) / delta;
        // At a radial turning point p_r² is round-off, and its square root would turn that into a real kick
        const Real slack = Real(64) * std::numeric_limits<Real>::epsilon() * (potential + sigma + pth * pth + t * t) / delta;
        if (!(pr2 > -slack)) return std::nullopt;
        if (pr2 < slack) pr2 = Real(0);

        GeodesicState<Real> state;
        state.R = rr;
        state.Theta = static_cast<Real>(theta);
        state.Phi = static_cast<Real>(phi);
        state.Pr = (radialSign < 0.0 ? Real(-1) : Real(1)) * std::sqrt(pr2);
        state.Ptheta = pth;
        state.Energy = e;
        state.AngularMomentum = l;
        state.Kind = kind;
        return state;
    }

    // Particle or photon at (r, theta, phi) seen by the local zero-angular-momentum observer (ZAMO) with energy
    // `localEnergy` and momentum `localMomentum` along (e_r, e_theta, e_phi). Generalises launchPhoton in
    // shaders/kerr.glsl. The ZAMO tetrad e_t = (∂_t + ω ∂_φ) / α gives E = α ε + ω L.
    template <typename Real>
    [[nodiscard]] GeodesicState<Real> MakeGeodesicFromZamoMomentum(const KerrNewmanParams& params, const GeodesicKind kind,
                                                                   const double r, const double theta, const double phi,
                                                                   const Real localEnergy,
                                                                   const std::array<Real, 3>& localMomentum)
    {
        const auto st = MakeSpacetime<Real>(params);
        const Real rr = static_cast<Real>(r);
        const Real a2 = st.A * st.A;
        const Real s = SafeSin(std::sin(static_cast<Real>(theta)));
        const Real ct = std::cos(static_cast<Real>(theta));
        const Real sigma = rr * rr + a2 * ct * ct;
        const Real delta = HorizonDelta(st, rr);
        const Real big = (rr * rr + a2) * (rr * rr + a2) - delta * a2 * s * s;
        const Real alpha = std::sqrt(std::max(sigma * delta / big, Real(0)));
        const Real omega = st.A * (Real(2) * st.M * rr - st.Q * st.Q) / big;

        GeodesicState<Real> state;
        state.R = rr;
        state.Theta = static_cast<Real>(theta);
        state.Phi = static_cast<Real>(phi);
        state.AngularMomentum = std::sqrt(big / sigma) * std::abs(s) * localMomentum[2];
        state.Energy = alpha * localEnergy + omega * state.AngularMomentum;
        state.Pr = std::sqrt(sigma / delta) * localMomentum[0];
        state.Ptheta = std::sqrt(sigma) * localMomentum[1];
        state.Kind = kind;
        return state;
    }

    // Massive particle moving with 3-velocity v (units of c, |v| < 1) relative to the local ZAMO.
    template <typename Real>
    [[nodiscard]] std::optional<GeodesicState<Real>> MakeGeodesicFromZamoVelocity(const KerrNewmanParams& params,
                                                                                   const double r, const double theta,
                                                                                   const double phi,
                                                                                   const std::array<double, 3>& velocity)
    {
        const std::array<Real, 3> v{static_cast<Real>(velocity[0]), static_cast<Real>(velocity[1]),
                                    static_cast<Real>(velocity[2])};
        const Real speed2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (!(speed2 < Real(1)) || r <= OuterHorizonRadius(params)) return std::nullopt;
        const Real gamma = Real(1) / std::sqrt(Real(1) - speed2);
        return MakeGeodesicFromZamoMomentum<Real>(params, GeodesicKind::Timelike, r, theta, phi, gamma,
                                                  {gamma * v[0], gamma * v[1], gamma * v[2]});
    }

    // Photon with unit energy in the local ZAMO frame travelling along `direction` (normalised here).
    template <typename Real>
    [[nodiscard]] std::optional<GeodesicState<Real>> MakePhotonFromZamoDirection(const KerrNewmanParams& params,
                                                                                 const double r, const double theta,
                                                                                 const double phi,
                                                                                 const std::array<double, 3>& direction)
    {
        const std::array<Real, 3> n{static_cast<Real>(direction[0]), static_cast<Real>(direction[1]),
                                    static_cast<Real>(direction[2])};
        const Real length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (!(length > Real(0)) || r <= OuterHorizonRadius(params)) return std::nullopt;
        return MakeGeodesicFromZamoMomentum<Real>(params, GeodesicKind::Null, r, theta, phi, Real(1),
                                                  {n[0] / length, n[1] / length, n[2] / length});
    }

    // Circular equatorial orbit at r; nullopt at or inside the photon orbit, where it would not be timelike.
    // Orbits inside the ISCO exist but are unstable.
    template <typename Real>
    [[nodiscard]] std::optional<GeodesicState<Real>> MakeCircularOrbit(const KerrNewmanParams& params, const double r,
                                                                       const OrbitDirection direction,
                                                                       const double phi = 0.0)
    {
        const double energy = CircularOrbitEnergy(params, r, direction);
        const double angularMomentum = CircularOrbitAngularMomentum(params, r, direction);
        if (!std::isfinite(energy) || !std::isfinite(angularMomentum)) return std::nullopt;

        GeodesicState<Real> state;
        state.R = static_cast<Real>(r);
        state.Theta = std::numbers::pi_v<Real> / Real(2);
        state.Phi = static_cast<Real>(phi);
        state.Energy = static_cast<Real>(energy);
        state.AngularMomentum = static_cast<Real>(angularMomentum);
        state.Kind = GeodesicKind::Timelike;
        return state;
    }

    // One Dormand–Prince 5(4) step of size h from y, whose derivative k1 the caller has evaluated. Writes the
    // fifth-order solution to `next` and returns the RMS error estimate relative to the tolerance (≤ 1 accepts,
    // NaN or inf rejects).
    template <typename Real>
    [[nodiscard]] Real DormandPrinceStep(const KerrNewmanSpacetime<Real>& st, const Real energy, const Real angularMomentum,
                                         const GeodesicVector<Real>& y, const GeodesicVector<Real>& k1, const Real h,
                                         const GeodesicTolerance& tolerance, GeodesicVector<Real>& next)
    {
        const auto c = [](const double value) { return static_cast<Real>(value); };
        const auto stage = [&](const std::initializer_list<std::pair<Real, const GeodesicVector<Real>*>> terms)
        {
            GeodesicVector<Real> point = y;
            for (const auto& [weight, k] : terms)
            {
                for (std::size_t i = 0; i < point.size(); ++i) point[i] += h * weight * (*k)[i];
            }
            return GeodesicDerivative(st, energy, angularMomentum, point);
        };

        const auto k2 = stage({{c(1.0 / 5.0), &k1}});
        const auto k3 = stage({{c(3.0 / 40.0), &k1}, {c(9.0 / 40.0), &k2}});
        const auto k4 = stage({{c(44.0 / 45.0), &k1}, {c(-56.0 / 15.0), &k2}, {c(32.0 / 9.0), &k3}});
        const auto k5 = stage({{c(19372.0 / 6561.0), &k1}, {c(-25360.0 / 2187.0), &k2}, {c(64448.0 / 6561.0), &k3},
                               {c(-212.0 / 729.0), &k4}});
        const auto k6 = stage({{c(9017.0 / 3168.0), &k1}, {c(-355.0 / 33.0), &k2}, {c(46732.0 / 5247.0), &k3},
                               {c(49.0 / 176.0), &k4}, {c(-5103.0 / 18656.0), &k5}});

        const std::array<Real, 6> b{c(35.0 / 384.0), Real(0), c(500.0 / 1113.0), c(125.0 / 192.0),
                                    c(-2187.0 / 6784.0), c(11.0 / 84.0)};
        for (std::size_t i = 0; i < next.size(); ++i)
        {
            next[i] = y[i] + h * (b[0] * k1[i] + b[2] * k3[i] + b[3] * k4[i] + b[4] * k5[i] + b[5] * k6[i]);
        }
        const auto k7 = GeodesicDerivative(st, energy, angularMomentum, next);

        // Fifth- minus fourth-order weights
        const std::array<Real, 7> e{c(71.0 / 57600.0), Real(0), c(-71.0 / 16695.0), c(71.0 / 1920.0),
                                    c(-17253.0 / 339200.0), c(22.0 / 525.0), c(-1.0 / 40.0)};
        Real sum{0};
        for (std::size_t i = 0; i < next.size(); ++i)
        {
            const Real error = h * (e[0] * k1[i] + e[2] * k3[i] + e[3] * k4[i] + e[4] * k5[i] + e[5] * k6[i]
                                    + e[6] * k7[i]);
            const Real scale = c(tolerance.Absolute) + c(tolerance.Relative) * std::max(std::abs(y[i]), std::abs(next[i]));
            sum += (error / scale) * (error / scale);
        }
        return std::sqrt(sum / Real(6));
    }

    // Adaptive integration driver. `limit(y, dy, lambda)` returns the signed largest step allowed from y, or nullopt
    // once the target is reached. The step memory lives in state.Step, so successive calls continue smoothly.
    template <typename Real, typename StepLimit>
    void IntegrateGeodesic(const KerrNewmanParams& params, GeodesicState<Real>& state, const GeodesicTolerance& tolerance,
                           StepLimit&& limit)
    {
        if (state.Status != GeodesicStatus::Ok) return;
        const auto st = MakeSpacetime<Real>(params);
        const Real halfGap = std::max(static_cast<Real>(0.5) * (st.HorizonRadius - st.InnerHorizonRadius),
                                      static_cast<Real>(1e-6) * st.M);
        const Real capture = st.HorizonRadius + static_cast<Real>(tolerance.HorizonMargin) * halfGap;
        const Real epsilon = std::numeric_limits<Real>::epsilon();
        auto y = ToVector(state);
        if (!(state.Step > Real(0))) state.Step = static_cast<Real>(1e-2) * std::max(std::abs(state.R), st.M);

        for (std::int64_t attempts = 0;; ++attempts)
        {
            if (!std::ranges::all_of(y, [](const Real v) { return std::isfinite(v); }))
            {
                state.Status = GeodesicStatus::NonFinite;
                break;
            }
            if (y[0] < capture)
            {
                state.Status = GeodesicStatus::Captured;
                break;
            }
            const auto k1 = GeodesicDerivative(st, state.Energy, state.AngularMomentum, y);
            const std::optional<Real> bound = limit(y, k1, state.Lambda);
            if (!bound) break;
            if (attempts >= tolerance.MaxSteps)
            {
                state.Status = GeodesicStatus::StepLimit;
                break;
            }

            const bool clamped = std::abs(*bound) < state.Step;
            const Real h = clamped ? *bound : std::copysign(state.Step, *bound);
            GeodesicVector<Real> next;
            const Real error = DormandPrinceStep(st, state.Energy, state.AngularMomentum, y, k1, h, tolerance, next);
            if (!(error <= Real(1)))
            {
                const Real shrink = std::isfinite(error)
                                        ? std::clamp(static_cast<Real>(0.9) * std::pow(error, static_cast<Real>(-0.2)),
                                                     static_cast<Real>(0.2), static_cast<Real>(0.9))
                                        : static_cast<Real>(0.2);
                state.Step = std::abs(h) * shrink;
                if (state.Step < epsilon * (Real(1) + std::abs(state.Lambda)))
                {
                    state.Status = GeodesicStatus::StepLimit;
                    break;
                }
                continue;
            }

            y = next;
            state.Lambda += h;
            if (!clamped)
            {
                const Real grow = error > Real(0) ? static_cast<Real>(0.9) * std::pow(error, static_cast<Real>(-0.2))
                                                  : Real(5);
                state.Step = std::abs(h) * std::clamp(grow, static_cast<Real>(0.2), Real(5));
            }
        }
        FromVector(y, state);
    }

    // Advances the affine parameter (proper time for a timelike geodesic) by deltaLambda, which may be negative.
    template <typename Real>
    void AdvanceGeodesic(const KerrNewmanParams& params, GeodesicState<Real>& state, const Real deltaLambda,
                         const GeodesicTolerance& tolerance = {})
    {
        const Real target = state.Lambda + deltaLambda;
        const Real done = Real(4) * std::numeric_limits<Real>::epsilon() * std::max(Real(1), std::abs(target));
        IntegrateGeodesic(params, state, tolerance,
                          [&](const GeodesicVector<Real>&, const GeodesicVector<Real>&, const Real lambda) -> std::optional<Real>
                          {
                              const Real remaining = target - lambda;
                              if (std::abs(remaining) <= done) return std::nullopt;
                              return remaining;
                          });
    }

    // Advances until Boyer–Lindquist coordinate time t (the clock of a distant static observer) has grown by
    // deltaT. Stops early with Captured: t diverges at the horizon, so an infalling body never reaches it in t.
    template <typename Real>
    void AdvanceToCoordinateTime(const KerrNewmanParams& params, GeodesicState<Real>& state, const Real deltaT,
                                 const GeodesicTolerance& tolerance = {})
    {
        const Real target = state.T + deltaT;
        const Real done = static_cast<Real>(tolerance.Absolute)
                          + static_cast<Real>(tolerance.Relative) * std::max(std::abs(deltaT), Real(1));
        IntegrateGeodesic(params, state, tolerance,
                          [&](const GeodesicVector<Real>& y, const GeodesicVector<Real>& dy, const Real) -> std::optional<Real>
                          {
                              const Real remaining = target - y[5];
                              if (std::abs(remaining) <= done) return std::nullopt;
                              const Real step = remaining / dy[5];
                              if (!std::isfinite(step)) return std::nullopt;
                              return step;
                          });
    }

    // Classical fixed-step RK4, as in shaders/kerr.glsl; kept as a reference for the adaptive integrator.
    template <typename Real>
    void StepGeodesicRk4(const KerrNewmanParams& params, GeodesicState<Real>& state, const Real h)
    {
        const auto st = MakeSpacetime<Real>(params);
        const auto y = ToVector(state);
        const auto offset = [&](const GeodesicVector<Real>& k, const Real scale)
        {
            GeodesicVector<Real> point = y;
            for (std::size_t i = 0; i < point.size(); ++i) point[i] += scale * k[i];
            return GeodesicDerivative(st, state.Energy, state.AngularMomentum, point);
        };
        const Real half = static_cast<Real>(0.5) * h;
        const auto k1 = GeodesicDerivative(st, state.Energy, state.AngularMomentum, y);
        const auto k2 = offset(k1, half);
        const auto k3 = offset(k2, half);
        const auto k4 = offset(k3, h);
        GeodesicVector<Real> next = y;
        for (std::size_t i = 0; i < next.size(); ++i)
        {
            next[i] += h / Real(6) * (k1[i] + Real(2) * k2[i] + Real(2) * k3[i] + k4[i]);
        }
        FromVector(next, state);
        state.Lambda += h;
    }
}
