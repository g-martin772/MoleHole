# Release 1.0 Goal: Recreate *Interstellar* Scientifically

## Target

**MoleHole 1.0 lets a user rebuild the journey of *Interstellar* as a real-time, scientifically accurate simulation.** That means leaving Earth orbit in the Endurance, passing through the wormhole, visiting the planets around Gargantua, and falling toward the black hole. Every visual and every number has to come from General Relativity, not from artistic approximation.

The scientific reference is Kip Thorne's *The Science of Interstellar* and the papers behind the film (James, von Tunzelmann, Franklin, Thorne, "Gravitational Lensing by Spinning Black Holes in Astrophysics, and in the Movie *Interstellar*", Class. Quantum Grav. 32, 065001, 2015; and "Visualizing Interstellar's Wormhole", Am. J. Phys. 83, 486, 2015).

The release is done when the acceptance scenario at the end of this document runs end to end from a single scene file, in real time, with no manual workarounds.

## Success criteria

1. **Accurate.** The output matches published reference values within stated tolerances (see [Validation](#validation)).
2. **Real time.** The interactive viewport holds at least 30 FPS at 1080p on the reference GPU in all scenes. The headless exporter may be slower for offline-quality output.
3. **Playable.** A user can move through the scene in first person, on foot or inside a spacecraft, without leaving the simulation.
4. **Reproducible.** The whole scenario ships as `templates/` scenes, `combos/` and `scripts/`, with no hard-coded special cases in the renderer.

## Feature pillars

### 1. Textured planets and moons

Celestial bodies need surface appearance. The shader work already done for the disk and the event horizon extends to solid bodies.

- Albedo, normal, roughness and emissive textures on spheres, loaded as assets.
- Textures are seen *through the lens*: surface hits found by the ray marcher get lensed, Doppler-shifted and redshifted like the disk.
- Atmospheres and cloud layers (Scattering, limb brightening) for Miller's-style ocean planets and Mann's ice planet.
- Day/night shading from the accretion disk and star as light sources, including the disk as an extended emitter.
- Level of detail for a body that is seen from far away, then approached on foot.
- Planet set for the film: **Miller's planet** (water world, deep in the potential well), **Mann's planet** (ice and clouds), **Edmunds' planet**, plus Earth and Saturn for the opening act.

### 2. Tidal forces

- Compute the tidal tensor from the active metric (geodesic deviation) for any observer, not only the Newtonian approximation.
- Display it: an overlay of principal stretch/compression axes, plus a numeric readout in g.
- Apply it: tidal stress acts on `RigidBody` and soft objects through the existing PhysX integration, which gives spaghettification near compact objects.
- Miller's planet shows the effect on its surface: the ocean tides that produce the film's giant waves, which come from tidal stretching in Gargantua's field plus the planet's own orientation.
- Roche limit check for bodies and a break-up behaviour when it is crossed.

### 3. Wormholes

- Ellis/Morris–Thorne style traversable wormhole metric as a selectable metric next to Schwarzschild and Kerr, with the parameters from the paper (throat radius, length, lensing width).
- Null geodesics that cross the throat, with two distinct universes on each side (different sky or star field per side).
- Correct lensing: the user sees the other side as a distorted sphere that responds to the viewpoint, as in the film.
- Flying through the wormhole in real time, with a smooth transition across the throat and no coordinate singularity at the throat.
- Embedding diagram view as a debug aid.

### 4. First-person real-time movement

- A first-person controller that moves as a *timelike observer* in the current spacetime. It uses a local orthonormal tetrad, not a flat-space camera.
- Walk on a planet (surface gravity, local frame), float and fly in free space, and switch between modes.
- Relativistic effects are visible during motion: aberration, Doppler and gravitational redshift, beaming, and lensing. All of them depend on the observer's actual velocity and position.
- **Spacecraft:** interior and exterior movement on the **Endurance** (the ring station), the **Ranger** and the **Lander**.
  - The Endurance rotates for artificial gravity. The controller works in the rotating frame, with the Coriolis effect included.
  - Docking, hatch transitions and cockpit view.
  - Ship models as assets, with rigid-body physics through PhysX.
- Input via mouse and keyboard, with gamepad support as a stretch goal.

### 5. General Relativity time scales

- Separate **coordinate time**, **proper time** of each observer and **ship time**. The UI shows all of them.
- Gravitational and kinematic time dilation computed from the metric. The Miller's planet scenario has to show roughly 1 hour on the surface equal to 7 years elsewhere, driven by the physics and not by a scripted factor.
- A simulation clock that can run each frame at a user-chosen ratio of proper time to wall time, with a lock to a chosen observer.
- Light-travel delay: observers see events as they are received, not as they happen.
- Time-dilation HUD and a plot of elapsed time per observer.

### 6. Orbits

- Timelike geodesic integration in Schwarzschild and Kerr (and in the wormhole metric), using the Hamiltonian RK4 approach already in the renderer.
- Bound, unbound and plunging orbits, the ISCO, the photon sphere and the marginally bound orbit, all visible as overlays.
- Relativistic effects: periapsis precession, frame dragging (Lense–Thirring), and prograde/retrograde differences.
- Orbit prediction trail for any body or ship, with a patched-conic style planner for transfers such as the slingshot around Gargantua's intermediate-mass black hole used in the film.

## Cross-cutting requirements

| Area | Requirement |
|---|---|
| Rendering | Existing ray-marched lensing, Page–Thorne disk, Doppler and blackbody carry over to the film setup, with filtered, antialiased output. |
| Simulation | Sim-thread owns the scene. All new systems register as `GPP::ISimulationModule` and use the command/undo path. |
| Components | New data (bodies, textures, observers, ships) is registered through reflection in `RegisterComponents`, so it gets YAML, inspector, graph nodes and Luau for free. |
| Scripting | Every feature is controllable from Luau and the animation graph, so a mission can be scripted (the Endurance flight, the dive, the docking). |
| Assets | Textures and models are hot-reloaded via `config.json` → `GPP.Assets`. |
| Export | The headless `--export-video` can render the full sequence at higher quality than real time. |
| Numerics | Fixed and documented integration tolerances. Singular and near-horizon regions fail gracefully (clamped step, no NaNs). |

## Validation

Each pillar needs an automated test (`tests/unit/`, Catch2) or a reproducible reference scene.

- **Lensing:** compare the Einstein ring and the photon-ring position for Schwarzschild and Kerr against analytic values.
- **Disk:** compare against the Page–Thorne temperature profile and the Gargantua look from the paper.
- **Wormhole:** compare against the figures in the Am. J. Phys. paper (view of the other side at several throat radii).
- **Time dilation:** compare against the exact Schwarzschild/Kerr formulas for static and circular-orbit observers. Include the Miller's planet 1 h ↔ 7 y case.
- **Tidal tensor:** compare against the analytic Schwarzschild tidal components (−2M/r³ radial, +M/r³ transverse).
- **Orbits:** conservation of energy and angular momentum over many orbits, ISCO radius for a range of spins, perihelion precession rate.
- **Performance:** a benchmark scene with an FPS budget, run on the reference hardware.

## Milestones

| # | Milestone | Content |
|---|---|---|
| M1 | Geodesic core | Timelike geodesics, proper time, observer tetrads, orbit overlays, tests. |
| M2 | Bodies | Textured spheres under lensing, atmospheres, lighting, Miller/Mann/Edmunds assets. |
| M3 | Tidal forces | Tidal tensor, overlay, physics coupling, Roche limit. |
| M4 | Wormhole | Metric, geodesic integration through the throat, two-sky rendering, flight through. |
| M5 | First person | Relativistic observer controller, planet walking, Endurance interior with rotating frame. |
| M6 | Time and mission | Time-scale UI, light-delay, scripted mission, export of the full sequence. |
| M7 | Release | Validation pass, performance pass, docs and user guide, sample scenes. |

## Acceptance scenario

A single scene, `templates/interstellar.yaml`, with these steps in order:

1. Start in orbit around Saturn aboard the Endurance, walk through the rotating ring station in first person, and see the wormhole ahead.
2. Fly to the wormhole and through it in real time. The two skies and the lensing are correct.
3. Arrive near Gargantua, view the lensed disk and its Doppler asymmetry.
4. Take the Ranger to Miller's planet. Show the tidal waves, and the time dilation readout that reaches about 7 years per hour on the surface.
5. Return to the Endurance. Compare the proper time of the observer who stayed with the one who visited.
6. Plot the slingshot orbit and show the periapsis precession.
7. Fall toward the event horizon of Gargantua in a free-fall observer frame.

## Out of scope for 1.0

- Quantum-gravity or fully dynamical (numerical relativity) spacetimes. The metrics are fixed analytic ones.
- The "tesseract" and the fifth-dimension sequence, which is speculative.
- Multiplayer, VR, and full-fidelity film-grade path tracing.
- Biology or detailed planetary climate simulation.
