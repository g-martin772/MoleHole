# Bodies in free fall: geodesic motion in the simulation

These notes cover stage 3 of milestone M1 in `docs/RELEASE_1_GOAL.md`. Scene entities with a
**Geodesic Body** component move along timelike geodesics of their black hole's exact Kerr–Newman metric.
Each body keeps its own proper time.

Code:
- `src/simulation/geodesic_module.cppm` / `.cpp` (`:Simulation.GeodesicBodies`)
- the component `GeodesicBodyComponent` in `src/simulation/components.cppm`
- the exclusions in `gravity_module.cpp`

It builds on `geodesics.md` (the integrator) and `observers.md` (launch velocities, clock rates).

Tests: `tests/unit/simulation/geodesic_module_tests.cpp`.

---

## 1. The test-particle approximation

A geodesic body is a **test particle**. It follows the geometry of one black hole but does not change it.
This is exact in the limit where the body's mass is negligible against the hole's, and it is an excellent
approximation for planets and spacecraft around a supermassive black hole: for Miller's planet around
Gargantua the mass ratio is about 10⁻¹³. The corrections, the gravitational self-force and radiation
reaction, scale with the mass ratio and only matter over very many orbits. They are outside the release.

Consequences in the app:

- **Single source.** A body follows exactly one hole, set in the component's `BlackHole` field (or the first
  hole if that is empty). Other holes are ignored. There is no exact solution for several holes, so
  multi-hole scenes stay with the Newtonian module.
- **No mutual gravity.** Geodesic bodies don't attract each other, and the Newtonian gravity module neither
  moves them nor counts them as sources.
- **Charge.** As in the integrator, the charge Q acts only through the geometry.

Sources:
- Poisson, E., Pound, A. & Vega, I. (2011). *The motion of point particles in curved spacetime.* Living Rev. Relativ. 14, 7. doi:10.12942/lrr-2011-7, arXiv:1102.0529. Geodesic motion is the leading order in the mass ratio, and the self-force is the correction.
- Barack, L. & Pound, A. (2019). *Self-force and radiation reaction in general relativity.* Rep. Prog. Phys. 82, 016904 (online 2018). doi:10.1088/1361-6633/aae552, arXiv:1805.10385
- Geroch, R. & Jang, P. S. (1975). *Motion of a body in general relativity.* J. Math. Phys. 16, 65. doi:10.1063/1.522416. A rigorous result: a sufficiently small body follows a geodesic.
- Einstein, A., Infeld, L. & Hoffmann, B. (1938). *The gravitational equations and the problem of motion.* Ann. Math. 39, 65. doi:10.2307/1968714. Historical: the equations of motion follow from the field equations.

## 2. Units and clocks

The integrator works in units of the hole's mass M. The module converts with the hole's own scales, using
the constants of `GPP::Units`:

    one length unit  r_g = G M / c²   (`GravitationalRadius`)
    one time unit    t_g = G M / c³   (`GeometricTime`)

- **Simulation time is coordinate time.** Each runner tick advances the Boyer–Lindquist coordinate time t
  by (tick seconds × scene `TimeScale`) / t_g. This is the same physical-seconds scaling the PhysX module
  uses. The coordinate time t is the clock of a distant observer at rest relative to the hole, so all bodies
  share one notion of "now" and the outside universe ticks uniformly.
- **Each body has its own proper time.** It accumulates τ as the affine parameter of its geodesic, in
  seconds. `ClockRate` = dτ/dt = α/γ is the current rate (`observers.md` §4), and `Speed` is the speed
  relative to the local ZAMO. At Sagittarius A* (4.3 × 10⁶ M☉), t_g ≈ 21 s, so a 10 M circular orbit takes
  about 2.6 hours of coordinate time. Use `TimeScale` to watch it faster.

## 3. Launching

The body starts from its current position, converted from world coordinates to Boyer–Lindquist (r, θ, φ) in
the hole's spin frame. That frame is the same one the shader uses (`spinFrame` in `kerr.glsl`), so bodies,
disk and horizon share an orientation. Its initial velocity relative to the local ZAMO is one of:

| `Launch` | Initial velocity | Motion |
|---|---|---|
| Zamo | 0 relative to the ZAMO | released from co-rotation with the frame dragging; falls in |
| Static | at rest relative to the distant stars | dropped; falls in. Impossible inside the ergosphere |
| Orbiting | the circular-orbit velocity at its r (prograde or `Retrograde`) | a circular orbit in the equatorial plane; above or below it, an orbit that starts with the equatorial Ω and then precesses |
| Raindrop | free fall from rest at infinity (E = 1, L = 0) | falls in at constant θ |
| Velocity | `LaunchVelocity` along (r̂, θ̂, φ̂) of the ZAMO, in c | anything: eccentric, inclined, escaping |

A launch that is impossible is reported as "Cannot launch here", and the body stays put. Cases:
- the body is at or inside the horizon
- Static is requested inside the ergosphere
- Orbiting is requested inside the photon orbit
- |v| ≥ c

**Re-launching.** The module re-launches a body when:
- its transform is changed from outside (editor, script, undo)
- its launch fields change
- its hole moves or changes

So moving a body in the editor always means "start again from here with these settings". The runtime state
(geodesic, status, proper time) is not saved to the scene file. On load the body launches from its saved
position. During Play, the Stop snapshot keeps the complete state, so Stop returns every body to its
pre-play position and proper time. The tests check this.

## 4. The horizon in coordinate time

An infalling body reaches the horizon in finite proper time but infinite coordinate time. The integrator
stops a body at r₊ + 10⁻³ M, and the module reports it as "Crossed the horizon" with a clock rate of 0. The
body then stays where it is.

This frozen body is what the distant observer's clock implies: in t, the body hangs at the horizon forever.
Visually it would also redden and fade, but the body is not yet rendered through the tracer. The proper time
shown is the time the body itself needed. For a body falling in Schwarzschild from rest at infinity (a
Raindrop), the proper time between passing r₀ and passing r is the following, and the test checks it:

    τ(r₀ → r) = (2/3) (r₀^{3/2} − r^{3/2}) / √(2M)

It follows from dr/dτ = −√(2M/r). A body released from rest *at* r₀ follows the cycloid instead
(`geodesics.md` §7).

Following a body *through* the horizon would need horizon-penetrating coordinates (Kerr–Schild or Doran),
see `geodesics.md` §3.

Sources:
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation*, ch. 25 (radial infall in Schwarzschild; finite proper time, infinite coordinate time). The section number is not verified.
- Schutz, B. F. *A First Course in General Relativity*, ch. 11 (Schwarzschild geometry, radial infall). The section is not verified.
- Hartle, J. B. (2003). *Gravity*, chapter on Schwarzschild black holes. The chapter number is not verified.

## 5. Interaction with PhysX and Newtonian gravity

- The module runs in `SimulationPhase::PostSim`, after PhysX, so the geodesic position always wins.
- A geodesic body with a dynamic rigid body is switched to kinematic when the simulation starts. PhysX then
  follows the written transform as a kinematic pose, and collisions and triggers still see the body.
- `GravitySimulationModule` skips geodesic bodies, both as targets and as sources of Newtonian force.
- Not tested: whether the black hole's PhysX trigger volume (the gravity module's absorption) also fires
  for kinematic geodesic bodies. Without it, a captured body freezes at the horizon as in §4.

## 5b. Riding a world line: the scene camera as a geodesic body

Giving the primary scene camera a Geodesic Body makes the camera an observer in free fall. While the
simulation plays (or during a video export) and the scene camera is possessed:
- the simulation moves the camera along its geodesic
- the renderer takes the observer velocity from that geodesic (`ZamoVelocity` of its state) instead of the
  `camera.observer` preset, with aberration and Doppler shift as in `observers.md` §5
- the camera window shows the body's proper time, clock rate and speed, and a proper acceleration of 0

If the body has crossed the horizon, or follows a different hole than the first one (the one the tracer
uses), the view falls back to the ZAMO and the readout says so. The camera keeps its own orientation (its
transform rotation); turning the view relative to the observer's frame is milestone M5's first-person
controller.

`templates/gargantua-orbits.yaml` shows this setup: near-extremal Gargantua (10⁸ M☉, a = 1 − 1.3 × 10⁻¹⁴), a
body on a 6 M orbit, a raindrop, and the camera on an orbit at about 15 M, with `TimeScale` 6000. One second of
runner time is then 6000 s of coordinate time; Gargantua's GM/c³ is about 490 s.

**The scene time scale reaches the simulation.** `Scene::Clone` in gpp dropped `TimeScale`, so every scene ran
at 1 regardless of its file. The fix is in gpp (`src/simulation/scene.cpp`), and a MoleHole test checks that a
simulation keeps its scene's time scale.

## 6. Validation

| Test | Checks |
|---|---|
| Coordinate round trip | world ↔ Boyer–Lindquist in a tilted spin frame, 10⁻⁹ relative |
| Standalone comparison | 200 ticks of an inclined orbit around a tilted, spinning hole match one call of the integrator, 10⁻⁷ relative, including proper time |
| Equatorial orbit | r stays constant to 10⁻⁸; `ClockRate` and the accumulated proper time match the circular-orbit dτ/dt (`observers.md` §4) |
| Raindrop | captured; proper time against the closed form above; frozen afterwards |
| Impossible launches | static inside the ergosphere and v ≥ c are refused, and the body doesn't move |
| Gravity exclusion | a dynamic rigid body becomes kinematic |
| Save/load | launch settings survive the YAML round trip, runtime state does not |
| Play/Stop | the threaded runner returns bodies to the pre-play position and proper time |
| Template | `gargantua-orbits.yaml` loads, keeps `TimeScale`, and launches and moves all three bodies |
| Camera on a body | the scene camera's guid reaches the renderer; its velocity becomes the observer frame, with the ZAMO fallback for a body following another hole |

## 7. Time dilation and the spin limit

How strongly a body's clock can be slowed on a stable orbit is set by the ISCO of the hole. The largest
circular-orbit dilation is dt/dτ = uᵗ at the ISCO:

| a/M | ISCO | dt/dτ at the ISCO | 1 h on the orbit equals |
|---|---|---|---|
| 0.9 | 2.32 M | 2.7 | 2.7 h |
| 0.998 (Thorne limit, the app's clamp) | 1.24 M | 10.8 | 10.8 h |
| 1 − 10⁻⁸ | 1.003 M | 674 | 28 days |
| 1 − 1.3 × 10⁻¹⁴ | 1.00004 M | 61 900 | 7.06 years |

These values come from the Bardeen–Press–Teukolsky formulas used in `observer_tests.cpp`. The last row
reproduces the *Interstellar* figure of 1 hour on Miller's planet = 7 years (a factor of about 61 000). That
is why Gargantua needs a spin of about 1 − 1.3 × 10⁻¹⁴. The figure is usually attributed to Thorne
(*The Science of Interstellar*; James et al. 2015). Neither the exact value nor its page is verified yet,
but the calculation above confirms that this spin produces the film's factor. With spin clamped to the Thorne limit, the app can show at most
about 11×. A black hole can therefore be set to the near-extremal spin range, which allows
a/M = 1 − δ down to δ = 10⁻¹⁵ and gives Miller's planet its 7 years per hour. The default stays at the Thorne
limit. See `near_extremal.md` for the physics and the numerical changes that keep such orbits accurate.

Sources:
- Thorne, K. S. (2014). *The Science of Interstellar.* W. W. Norton, New York. ISBN 978-0-393-35137-8. Gargantua's mass is ≳ 10⁸ M☉ (excerpt checked). The Miller's planet chapter is not verified (around ch. 16–17).
- James, O. et al. (2015). Class. Quantum Grav. 32, 065001. doi:10.1088/0264-9381/32/6/065001
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). ApJ 178, 347. doi:10.1086/151796
- Thorne, K. S. (1974). *Disk-accretion onto a black hole. II. Evolution of the hole.* ApJ 191, 507. doi:10.1086/152991

---

*The references were checked against Crossref and arXiv. Book locations marked "not verified" need checking in a printed copy before they are cited in the paper.*
