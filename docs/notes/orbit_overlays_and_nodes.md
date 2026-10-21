# Orbit overlays, relativity graph nodes and the M1 scene

These notes cover stages 4 and 5 of milestone M1 in `docs/RELEASE_1_GOAL.md`:
- stage 4: overlays that show where the characteristic orbits of a black hole lie and where every body is going
- stage 5: animation-graph nodes for relativistic quantities, and the acceptance scene `templates/m1-orbits.yaml`

They build on `geodesics.md`, `observers.md`, `geodesic_bodies.md` and `near_extremal.md`.

---

## 1. Characteristic radii overlay (`overlay.characteristic_radii`)

Code: `CharacteristicRadiiOverlay` in `src/rendering/orbit_overlays.cppm`, drawn by `OrbitOverlayStage` in
`src/rendering/render_pipeline.cpp`.

For every black hole, in its spin frame (the same frame as the shader and the geodesic bodies):

| Line | Shape | Radius | Colour |
|---|---|---|---|
| Event horizon | equator and two meridians of the oblate surface r = r₊ | r₊ = M + √(M² − a² − Q²) | grey |
| Ergosphere (static limit) | the surface r_E(θ) = M + √(M² − a² cos²θ − Q²) in two meridian planes | touches the horizon at the poles, 2M at the equator | purple |
| Photon orbit | equatorial ring, prograde and retrograde | BPT eq. 2.18 (Kerr) | orange |
| Marginally bound orbit | equatorial ring, both directions | BPT eq. 2.19 | green |
| ISCO | equatorial ring, both directions | BPT eq. 2.21 | blue |

- **Brightness.** Prograde rings are bright, retrograde rings half as bright.
- **Mapping to world space.** A Boyer–Lindquist radius r maps to the Cartesian ring of radius √(r² + a²) (the
  oblate spheroidal map, `geodesics.md` §4). The test converts every drawn point back and finds the right r.
- **Which metric.** The overlay uses each hole's full physical metric (`BlackHoleMetric`). That is the metric
  the geodesic bodies follow, so their orbits and the rings agree. It ignores the `spacetime.solution` render
  choice.
- **Traced in the integrated transport.** The rasterised rings sit at the correct Boyer–Lindquist radii (the
  test converts every point back), but they are not lensed. For a = 0.9 they lie at 1.44 M (horizon), 1.56 M
  (photon orbit) and 2.32 M (ISCO), while lensing magnifies the shadow to an apparent radius of about 5 M
  (√27 M ≈ 5.2 M for Schwarzschild). As plain lines they therefore appeared inside the shadow, "too close to
  the hole", where no observer could see them.
  - With the Integrated transport, the tracer now draws the first hole's rings itself (`orbitRingColor` in
    `debug_view.glsl`, radii from `FillOrbitOverlayData`). It colours a ray where it crosses the equatorial
    plane within 1.2 % of a ring radius, the same equatorial-crossing test that shades the thin disk.
  - The rings are therefore lensed exactly like the disk, including the far side bent over the shadow and
    higher-order images. The ergoregion is tinted by each ray's path length inside it.
  - Only other black holes, and the non-integrated transports, still use rasterised lines.
  - A test checks that the traced ISCO is the disk's own inner edge and that the GLSL ring colours equal the
    C++ ones.
- **Aberrated, not lensed.** The remaining rasterised lines (other holes' rings, and the parts of predicted
  orbits and object path trails outside the integration sphere, §2b) are aberrated. For a moving camera (a `camera.observer` preset or a ridden Geodesic Body,
  Integrated transport), every vertex is moved to where that camera sees it
  (`AberrateForCamera`): its true direction from the camera is taken as seen by the local ZAMO, boosted into
  the camera's frame, and the distance is kept. This is the exact inverse of the tracer's per-pixel mapping in
  `rk4RayMarching`, with the same local basis. A test feeds the result back through the shader's mapping and
  recovers the true direction to 10⁻¹². So the lines stay on the traced image while the camera orbits at a
  quarter of c. Light bending is not applied: close to the hole, where the traced image is strongly lensed,
  the lines show the geometric positions.

Sources:
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). ApJ 178, 347. doi:10.1086/151796. Eqs. 2.18 (photon orbit), 2.19 (marginally bound), 2.21 (ISCO).
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation*, ch. 33 (horizon and static limit of Kerr; section not verified).

## 2. Predicted orbits overlay (`overlay.predicted_orbits`, `overlay.prediction_span`)

Code: `PredictedOrbitsOverlay` in `src/rendering/orbit_overlays.cppm`.

Each free-falling Geodesic Body's current geodesic state is copied from the render snapshot and integrated
ahead over the chosen span of coordinate time, in M of the body's hole. The integration uses the same
Dormand–Prince integrator (`AdvanceToCoordinateTime`), in 240 equal steps of t.

- **Rendering.** The trail fades from bright (now) to dim (end of the span), and it ends where the body would
  cross the horizon.
- **Cost.** The simulation thread is never touched. The render stage integrates again only when a body's
  state (λ, r, φ) or the span changes, so a paused scene costs nothing per frame.
- **Riding camera.** A camera that rides a body does not draw that body's own trail, which would start in the
  camera's eye.
- **Accuracy.** The prediction is the exact future of a test particle in the fixed metric, and the test
  checks that the trail ends where the integrator puts the body. It ignores everything the simulation could
  still change: graph or script edits, collisions, the user moving the body.

## 2b. Lensed trails (predicted orbits and object path trails)

With the Integrated transport, the parts of the predicted orbits and object path trails inside the first
hole's integration sphere (50 M) are drawn by the tracer itself. That is where rays are curved; outside it the
tracer treats them as straight, so the rasterised, aberrated lines are already exact there. Code:
`SplitTrailsForTracer` and `SampleTrails` in `src/rendering/orbit_overlays.cppm`, `trailSample` in
`shaders/trails.glsl`, and `OverlayGeometryStage` in `render_pipeline.cpp`.

1. **Split.** Before the trace, every trail segment is cut at the sphere. The inside parts are moved into the
   hole's local frame, the same `spinFrame` as the shader. The outside parts stay rasterised.
2. **Grid.** The inside segments are bucketed into a 48³ grid over the integration cube, each with its
   footprint padded by the widest line it can be tested with. They are uploaded as three storage buffers
   (bindings 13–16, after the mesh-trace bindings).
3. **Per ray step.** Each integration step of `rk4RayMarching` is a short straight chord, the same chords
   used to hit traced spheres and meshes. The shader finds the nearest trail segment among the grid cells the
   chord touches, using the closest distance between two segments.
4. **Width and edges.** A segment counts as hit when that distance is below a half-width of 0.9 trace pixels
   expressed as an angle, times the distance from the camera. Lines therefore keep their width on screen at
   any depth. The edge is soft (smoothstep over the last 40 %).
5. **Compositing.** The hit is composited front to back like the disk: `colour += α·coverage·trail`, then
   `α ·= 1 − coverage`. Gas and spheres in front of a trail hide it, and a trail hides what lies behind it.

Because the test happens along the actual curved rays, a trail gets every image the tracer produces: the
primary image, the secondary image on the other side of the shadow, the Einstein-ring arcs when a body
passes behind the hole, and the camera's aberration. It is the same mechanism that makes the disk and traced
objects lensed.

- **Tests.** `orbit_overlays_tests.cpp` checks the split at the sphere, the local frame against the geodesic
  module's frame, and the constant on-screen width. For 4000 random chords the grid lookup returns exactly
  what a brute-force search over all segments returns. Another test parses the shader's
  `INTEGRATION_RADIUS_M` and grid size so they cannot drift from C++.
- **Capacity.** At most 16 384 traced segments and 2¹⁸ grid entries per frame; anything beyond is dropped from
  tracing.
- **Off by default.** With the overlays off, the tracer skips the test entirely and renders exactly as before.
- **Light-travel delay is not modelled.** A trail is a fixed curve, so its lensed image is unambiguous. The
  traced bodies, however, are drawn where they are now in coordinate time, not where they were when the
  light left them. Near the hole a body can therefore appear slightly off its own lensed trail. That
  correction is the light-travel-delay item of the release goal.

## 3. Relativity nodes (animation graph and Luau)

Both nodes are in the node catalogue under **Relativity** (`node_registry.cppm`). They are implemented once
in `src/simulation/orbit_tools.cppm` and called by the interpreter (`animation_graph_executor.cpp`) and by the
Luau host (`script_runtime.cpp`, emitted by `graph_transpiler.cpp`), so both runtimes give the same values,
writes and diagnostics. The parity tests run each node in the interpreter, in Luau, and in Luau with inlining.

### Clock Rate (`NodeSubType::ClockRate`, pure)

Inputs: world position and observer (0 ZAMO, 1 Static, 2 Orbiting, 3 Raindrop). Output: dτ/dt = α/γ of that
observer at that point, relative to a distant static observer (`ObserverClockRateAt`, see `observers.md` §4).
It uses the first black hole's full metric.

- **Impossible observers.** The output is 0 where the observer cannot exist: inside the horizon, Static
  inside the ergosphere, Orbiting inside the photon orbit.
- **Tests.** In Schwarzschild at r = 10 M the node gives √(1 − 2M/r) for the static observer and √(1 − 3M/r)
  for the equatorial circular orbit.

### Launch Circular Orbit (`NodeSubType::LaunchOrbit`, action)

Inputs: an entity with a Geodesic Body; Radius in M of its hole; Azimuth in rad about the spin axis;
Retrograde. The node writes three fields: the body's position on the equator at that Boyer–Lindquist radius,
`Launch = Orbiting`, and `Retrograde`. On its next tick the geodesic module sees the moved body and launches
it from there (`geodesic_bodies.md` §3).

- **Refusals.** The node reports a warning and writes nothing when there is no Geodesic Body, the hole is
  missing, or the radius is inside the photon orbit, where no circular orbit exists.
- **Unstable radii.** Between the photon orbit and the ISCO the orbit is circular but unstable, and the body
  will eventually leave it (`geodesics.md` §7).

### What reflection already provides

The Geodesic Body's computed fields (Status, Proper time, Clock rate, Speed vs ZAMO, r/M) appear on the
generated *Get/Decompose Geodesic Body* nodes and in Luau `scene.get`. So "proper time of a body" and "time
dilation of a body" (1/clock rate) need no extra nodes.

## 4. The M1 acceptance scene (`templates/m1-orbits.yaml`)

A Kerr hole of 10⁶ M☉ with a/M = 0.9 (GM/c³ ≈ 4.9 s, `TimeScale` 100). Its render profile "M1 Orbits"
enables both overlays. The test `m1_orbits_scene_tests.cpp` loads the file and simulates about 2000 M.

| Body | Launch | What it shows | Checked by the test |
|---|---|---|---|
| Prograde (10 M) | Orbiting | frame dragging | — |
| Retrograde (10 M) | Orbiting, retrograde, on the far side | frame dragging: at the same r the counter-rotating orbit is faster in coordinate time, Ω = ±1/(r^{3/2} ± a) | swept angles in the ratio (r^{3/2} + a)/(r^{3/2} − a) = 1.0586, to 10⁻⁶ |
| Eccentric (inclined) | 14 M, v = (0, 0.06, 0.22) c relative to the ZAMO | periapsis precession (the rosette), and nodal precession of the orbital plane from frame dragging (Lense–Thirring) | stays bound between 6.3 M and 14 M; every radial period sweeps more than 2π + 1 rad |
| Raindrop | free fall from rest at infinity | finite proper time to the horizon | crosses the horizon |
| Camera (orbiting) | Orbiting, inclined | the riding camera, turned towards the hole by the scene's animation graph each tick | stays in free fall |

- **Why 10 M for the circular pair.** For a = 0.9 the retrograde ISCO is at 8.72 M, so a retrograde circular
  orbit at a smaller radius would be unstable.
- **The eccentric orbit's region.** Its periapsis of about 6.3 M is in the strong-field region where the
  periapsis advance per orbit is large. For orbits with more eccentricity or a periapsis closer to the
  separatrix, the same mechanism produces zoom-whirl behaviour.

Sources:
- Bardeen, Press & Teukolsky (1972), see §1. Ω of circular orbits (eq. 2.16 from memory, not verified); ISCO eq. 2.21.
- Lense, J. & Thirring, H. (1918). *Über den Einfluß der Eigenrotation der Zentralkörper auf die Bewegung der Planeten und Monde nach der Einsteinschen Gravitationstheorie.* Physikalische Zeitschrift 19, 156.
- Mashhoon, B., Hehl, F. W. & Theiss, D. S. (1984). *On the gravitational effects of rotating masses: the Thirring–Lense papers.* Gen. Relativ. Gravit. 16, 711. doi:10.1007/BF00762913. Includes an English translation.
- Glampedakis, K. & Kennefick, D. (2002). *Zoom and whirl: eccentric equatorial orbits around spinning black holes and their evolution under gravitational radiation reaction.* Phys. Rev. D 66, 044002. doi:10.1103/PhysRevD.66.044002, arXiv:gr-qc/0203086
- Levin, J. & Perez-Giz, G. (2008). *A periodic table for black hole orbits.* Phys. Rev. D 77, 103005. doi:10.1103/PhysRevD.77.103005, arXiv:0802.0459. Zoom-whirl orbits and precessing rosettes.
- Schmidt, W. (2002). *Celestial mechanics in Kerr spacetime.* Class. Quantum Grav. 19, 2743. doi:10.1088/0264-9381/19/10/314. Fundamental frequencies of inclined, eccentric orbits.

## 5. Disk exposure: anchored to what a distant observer sees

Before this change, the disk shader scaled brightness as (gT/T_peak)⁴, with T_peak the hottest gas in its
own rest frame. For a spinning hole that gas sits close to the ISCO, deep in the potential well, and its
light reaches a distant observer strongly redshifted. The visible disk was therefore exposed for gas nobody
could see brightly: a Thorne-limit disk was already about 20× too dim relative to a Schwarzschild one, and a
near-extremal one about 100×.

The reference is now the hottest gas as a distant face-on observer sees it:

    T_obs = max_r T(r) / uᵗ(r)

1/uᵗ is the redshift factor g = 1/[uᵗ (1 − Ω λ)] of disk emission (λ = L/E of the photon) for photons with
zero angular momentum. Those are the photons that reach an observer on the spin axis. g combines the
gravitational redshift with the transverse Doppler shift of the orbiting gas.

- **Calibration.** The shader reference is T_obs divided by its Schwarzschild ratio T_obs/T_peak = 0.839
  (`DiskExposureReference`). This keeps Schwarzschild disks exactly as before; the test checks that the
  reference equals the rest-frame peak to 10⁻¹².
- **Spin independence.** T_obs barely depends on spin (0.135, 0.136, 0.129 in the same units for a = 0.9,
  0.998, 1 − 10⁻¹⁴), unlike T_peak (0.216, 0.349, 0.499). So near-extremal and Thorne-limit disks now look
  alike, as their physical temperatures at r ≥ 2 M are nearly equal (`near_extremal.md` §4). In a render
  of the same scene the mean image brightness is 54.5 (near-extremal) against 56.0 (a = 0.998); it was 1.5
  against 5.4 before.
- **Spinning disks are brighter than before.** Relative to the old exposure: ×1.36 at a = 0.5, ×3.3 at 0.9,
  ×21 at 0.998, ×110 near-extremal. Scenes tuned for the old look may need a lower `post.exposure`.
- **Colour unchanged.** `disk.display_temperature` keeps its meaning, a colour scale relative to the
  rest-frame peak.

These numbers come from an independent Page–Thorne computation, with the accretion rate scaled by 1/η at a
fixed Eddington ratio, and the tests check them against the app's own profiles.

Sources:
- Cunningham, C. T. (1975). *The effects of redshifts and focusing on the spectrum of an accretion disk around a Kerr black hole.* ApJ 202, 788. doi:10.1086/154033. The redshift factor g of disk emission; the equation number is not verified.
- Li, L.-X., Zimmerman, E. R., Narayan, R. & McClintock, J. E. (2005). *Multitemperature blackbody spectrum of a thin accretion disk around a Kerr black hole: model computations and comparison with observations.* ApJS 157, 335. doi:10.1086/428089, arXiv:astro-ph/0411583. App. C, eq. C21: E_∞/E_local = (E† − Ω L†)/(1 − Ω λ). Also covers the inclination dependence of the observed disk spectrum.
- Page, D. N. & Thorne, K. S. (1974). ApJ 191, 499. doi:10.1086/152990. The flux profile T(r).

---

*The references were checked against Crossref, arXiv and the publishers' pages, except where marked "not
verified".*
