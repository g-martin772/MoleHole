# Geodesics of massive particles and light

These notes cover the CPU geodesic integrator, which is stage 1 of milestone M1 in
`docs/RELEASE_1_GOAL.md`. Later stages build on it: bodies that orbit on geodesics, observer frames and
orbit overlays. The notes also list the literature behind each physical effect and each test value.

Code: `src/physics/geodesic.cppm` (`:Physics.Geodesic`) and `src/rendering/metric.cppm` (characteristic
radii). Tests: `tests/unit/physics/geodesic_tests.cpp` and `tests/unit/rendering/metric_tests.cpp`. The
shader counterpart for light is `shaders/kerr.glsl`, described in `docs/notes/metric_rendering.md`.

Units are geometrized (G = c = 1), with lengths and times in units of the hole's mass M. Coordinates are
Boyer–Lindquist (t, r, θ, φ) of the Kerr–Newman metric. Schwarzschild, Kerr and Reissner–Nordström are
special cases of it (see `metric_rendering.md` §1).

---

## 1. Hamiltonian formulation

Free fall follows geodesics. These are integrated as Hamilton's equations for the covariant momentum p_μ,
taken per unit rest mass:

    H = ½ g^{μν} p_μ p_ν = K / (2Σ)
    K = Δ p_r² + p_θ² + (L / sinθ − a E sinθ)² − ((r² + a²) E − a L)² / Δ
    Σ = r² + a² cos²θ,  Δ = r² − 2Mr + a² + Q²

- **Same equations as the shader.** K/Σ is the full contraction g^{μν} p_μ p_ν for any momentum, so the
  equations of motion for light and for matter are identical. Only the value of H differs:
  - H = −½ for a massive particle, because p^μ = u^μ and u·u = −1. The affine parameter is then the
    particle's proper time τ.
  - H = 0 for light.
- **Constants of motion.** The metric is stationary and axisymmetric. Therefore E = −p_t (energy at
  infinity per unit mass) and L = p_φ (axial angular momentum) are exact constants.
- **Integrated state.** r, θ, p_r and p_θ are integrated, together with φ and coordinate time t:

      dφ/dλ = ∂H/∂L  = (T / sinθ + a P / Δ) / Σ
      dt/dλ = −∂H/∂E = (a sinθ T + (r² + a²) P / Δ) / Σ

  with T = L/sinθ − aE sinθ and P = (r² + a²)E − aL.
- **Carter's constant.** Carter's constant Q_C = p_θ² + cos²θ (a²(μ² − E²) + L²/sin²θ) is the third
  constant of motion. It is not imposed. The tests use it as an independent accuracy check.
- **Turning points.** Integrating p_r and p_θ, instead of dr/dλ = ±√R(r), avoids the sign flips and
  square-root singularities at radial and polar turning points.
- **Charge.** Test particles are neutral. The charge Q only enters through Δ, so there is no Lorentz force.

Sources:
- Carter, B. (1968). *Global structure of the Kerr family of gravitational fields.* Phys. Rev. 174, 1559. doi:10.1103/PhysRev.174.1559
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation*, ch. 25 (Schwarzschild orbits) and ch. 33 (Kerr). The section numbers are not verified.
- Chandrasekhar, S. (1983). *The Mathematical Theory of Black Holes.* Oxford, Clarendon Press.
- Schmidt, W. (2002). *Celestial mechanics in Kerr spacetime.* Class. Quantum Grav. 19, 2743. doi:10.1088/0264-9381/19/10/314, arXiv:gr-qc/0202090
- Fujita, R. & Hikida, W. (2009). *Analytical solutions of bound timelike geodesic orbits in Kerr spacetime.* Class. Quantum Grav. 26, 135002. doi:10.1088/0264-9381/26/13/135002, arXiv:0906.1420
- Levin, J. & Perez-Giz, G. (2008). *A periodic table for black hole orbits.* Phys. Rev. D 77, 103005. doi:10.1103/PhysRevD.77.103005, arXiv:0802.0459

## 2. Integrator

The integrator is adaptive Dormand–Prince 5(4) (`DormandPrinceStep`, `IntegrateGeodesic`):

- **Error control.** The local error estimate is the difference between the embedded 5th- and 4th-order
  solutions, measured as an RMS over all six variables against `Absolute + Relative·|y|`.
- **Step control.** The standard controller is used: a factor of 0.9·err^(−1/5), clamped to [0.2, 5].
- **Step memory.** The last step size is kept in `GeodesicState::Step`, so that consecutive calls (one per
  simulation tick) continue without restarting the controller.
- **Two ways to advance.**
  - `AdvanceGeodesic` advances the affine parameter. For matter that is the proper time, and it may run
    backwards.
  - `AdvanceToCoordinateTime` advances until t has grown by Δt. The last step is sized as (remaining t)/(dt/dλ)
    and refined until it lands within tolerance.
- **Reference integrator.** A fixed-step RK4 (`StepGeodesicRk4`, the same scheme as the shader) is kept as a
  reference, and a test checks that the two integrators agree.
- **Precision.** All functions are templates over the scalar type. They run in double by default and in
  `GPP::Units::Wide` (binary128 where available) for reference runs.

Sources:
- Dormand, J. R. & Prince, P. J. (1980). *A family of embedded Runge–Kutta formulae.* J. Comput. Appl. Math. 6, 19. doi:10.1016/0771-050X(80)90013-3
- Hairer, E., Nørsett, S. P. & Wanner, G. (1993). *Solving Ordinary Differential Equations I: Nonstiff Problems*, 2nd ed., Springer. The step-size control is in ch. II.4 (from memory).

## 3. Horizon and coordinate singularities

- **Horizon.** Boyer–Lindquist coordinates are singular at r₊ = M + √(M² − a² − Q²). A geodesic is reported
  as `Captured` once r < r₊ + `HorizonMargin`·M (default 10⁻³ M).
- **Proper time stays finite.** The infall itself is regular in proper time. The radial-plunge test checks
  τ against the exact cycloid all the way down to the margin.
- **Coordinate time diverges.** dt/dλ ∝ 1/Δ, so a distant observer never sees the crossing. In
  `AdvanceToCoordinateTime` the step count would grow without bound, and this is why that function stops
  with `Captured`.
- **Following a body across the horizon** would need horizon-penetrating (Kerr–Schild) coordinates. That
  is out of scope for M1.
- **Polar axis.** The axis is only reachable with L = 0, and then T/sinθ = −aE stays finite. sinθ is kept
  ≥ 10⁻⁴ in magnitude as a guard. Passing over the pole continues smoothly to θ < 0, which is the same point
  as (−θ, φ + π). Every expression depends on sinθ only through sin²θ or T/sinθ, so this is consistent.

## 4. Initial conditions

- **`MakeGeodesic`** builds a geodesic from E, L and p_θ at a point. It takes p_r from the mass-shell
  condition H = −μ²/2. This is how the Schwarzschild eccentric-orbit and plunge tests are set up.
- **`MakeGeodesicFromZamoVelocity` / `MakePhotonFromZamoDirection`** build a geodesic from a 3-velocity or
  direction measured by the local zero-angular-momentum observer (ZAMO). This is the generalisation of
  `launchPhoton` in `kerr.glsl`. With ε = γ and local momentum γv:

      L = √(A/Σ) sinθ · γ v_φ,  E = α γ + ω L,  p_r = √(Σ/Δ) γ v_r,  p_θ = √Σ γ v_θ

  The ZAMO lapse α and frame-dragging rate ω are as in `metric_rendering.md` §4.
- **`MakeCircularOrbit`** builds an equatorial circular orbit from the E(r), L(r) of `metric.cppm`, for
  either orbital direction.

Sources:
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). *Rotating black holes: locally nonrotating frames, energy extraction, and scalar synchrotron radiation.* ApJ 178, 347. doi:10.1086/151796. ZAMO frames, §III.
- Bardeen, J. M. (1970). *Kerr metric black holes.* Nature 226, 64. doi:10.1038/226064a0

## 5. Characteristic radii

`metric.cppm` gives the following radii for both orbital directions (`OrbitDirection`). Prograde orbits
co-rotate with the hole. The numbers below are for M = 1.

| Radius | Meaning | Kerr closed form (upper sign: prograde) | a = 0 |
|---|---|---|---|
| Photon orbit r_ph | Circular light orbit. No circular orbits for matter inside it. | 2M{1 + cos[⅔ arccos(∓a/M)]} | 3M |
| Marginally bound r_mb | Circular orbit with E = 1. A body falling from rest at infinity is captured if its periapsis would be inside it. | 2M ∓ a + 2√M √(M ∓ a) | 4M |
| ISCO r_ms | Innermost stable circular orbit, the minimum of E(r) | BPT eq. 2.21 | 6M |

All three are found numerically from the general Kerr–Newman E(r), so they include charge. The tests
compare them with the Kerr closed forms (BPT 1972, eqs. 2.18, 2.19 and 2.21) and with the
Reissner–Nordström photon sphere (3M + √(9M² − 8Q²))/2.

The general equatorial photon-orbit condition for Kerr–Newman is r² − 3Mr + 2Q² ± 2a√(Mr − Q²) = 0.

Sources:
- Bardeen, Press & Teukolsky (1972), see §4. Eq. 2.18 is r_ph, eq. 2.19 is r_mb, eqs. 2.20–2.21 are the ISCO. The upper sign is direct (prograde) orbits.
- Dadhich, N. & Kale, P. P. (1977). *Equatorial circular geodesics in the Kerr–Newman geometry.* J. Math. Phys. 18, 1727. doi:10.1063/1.523479
- Pugliese, D., Quevedo, H. & Ruffini, R. (2013). *Equatorial circular orbits of neutral test particles in the Kerr–Newman spacetime.* Phys. Rev. D 88, 024042. doi:10.1103/PhysRevD.88.024042, arXiv:1303.6250
- Pugliese, D., Quevedo, H. & Ruffini, R. (2011). *Circular motion of neutral test particles in Reissner–Nordström spacetime.* Phys. Rev. D 83, 024021. doi:10.1103/PhysRevD.83.024021

## 6. Spin limit

Spin and charge are clamped to √(a² + Q²) ≤ 0.998 M (`kMaxHorizonParameter`). This is Thorne's limit for
the spin that disk accretion can produce, because the radiation the hole captures from the disk
counteracts further spin-up. It also keeps every scene away from near-extremal holes, where Δ becomes tiny
outside the horizon and the integration becomes stiff. The release goal deliberately keeps Gargantua at
this limit.

- Thorne, K. S. (1974). *Disk-accretion onto a black hole. II. Evolution of the hole.* ApJ 191, 507. doi:10.1086/152991

## 7. Physical effects and their test references

| Effect | Test | Reference value |
|---|---|---|
| Circular orbits, orbital frequency Ω, time dilation dτ/dt = 1/uᵗ | `Circular orbits stay circular…` | Ω = ±1/(r^{3/2} ± a), uᵗ from the metric (BPT 1972) |
| Relativistic periapsis advance (Schwarzschild) | `Schwarzschild periapsis advance…` | Δφ per radial period = 4√(p/(p−6+2e)) K(m), m = 4e/(p−6+2e). Cutler, Kennefick & Poisson 1994, eq. 2.17. The advance is Δφ − 2π. |
| Orbit parametrisation by semi-latus rectum p and eccentricity e | same | E² = (p−2−2e)(p−2+2e)/[p(p−3−e²)], L² = p²M²/(p−3−e²). CKP 1994, eqs. 2.4–2.6. Bound orbits need p > 6 + 2e (eq. 2.8). |
| Radial infall: finite proper time, infinite coordinate time | `A radial plunge from rest…` | r = (r₀/2)(1 + cos η), τ = √(r₀³/8M)(η + sin η) (MTW ch. 25; Hartle 2003) |
| Instability inside the ISCO | `The ISCO separates…` | Bardeen, Press & Teukolsky 1972, eq. 2.21 |
| Frame dragging and inclined orbits: Carter constant | `An inclined eccentric Kerr orbit…` | Carter 1968 |
| Light deflection | `Weak-field light deflection…` | α̂ = 4(M/b) + (15π/4)(M/b)² + (128/3)(M/b)³ + … Keeton & Petters 2005, eqs. 24–25 |
| Photon sphere | `Light on the photon sphere…` | r = 3M, b = √27 M, Ω = 1/√27 M (Darwin 1959) |

Sources:
- Cutler, C., Kennefick, D. & Poisson, E. (1994). *Gravitational radiation reaction for bound motion around a Schwarzschild black hole.* Phys. Rev. D 50, 3816. doi:10.1103/PhysRevD.50.3816
- Darwin, C. (1959). *The gravity field of a particle.* Proc. R. Soc. Lond. A 249, 180. doi:10.1098/rspa.1959.0015
- Keeton, C. R. & Petters, A. O. (2005). *Formalism for testing theories of gravity using lensing by compact objects. I: Static, spherically symmetric case.* Phys. Rev. D 72, 104006. doi:10.1103/PhysRevD.72.104006, arXiv:gr-qc/0511019
- Hartle, J. B. (2003). *Gravity: An Introduction to Einstein's General Relativity.* Addison-Wesley. The chapter number is not verified.

## 8. Interstellar references for the release goal

- Thorne, K. S. (2014). *The Science of Interstellar.* W. W. Norton, New York. ISBN 978-0-393-35137-8
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001. doi:10.1088/0264-9381/32/6/065001, arXiv:1502.03808
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Visualizing Interstellar's wormhole.* Am. J. Phys. 83, 486. doi:10.1119/1.4916949, arXiv:1502.03809

## Limitations

- **Test particles only.** Bodies follow the fixed metric of one hole. They do not gravitate on each other,
  and there is no radiation reaction.
- **No horizon crossing** (see §3).
- **Neutral particles.** The charge Q changes the geometry, but charged particles feel no Lorentz force.

---

*The references were checked against Crossref and the papers themselves, except where marked "not
verified". Book chapter and section numbers marked that way should be checked in a printed copy before they
are cited in the paper.*
