# The solar system scene: ephemeris, N-body dynamics and bodies

`templates/solar-system.yaml` holds 52 bodies at their real positions and velocities on 2026-10-22 00:00 TDB: the Sun,
the eight planets, 18 of their moons, Pluto and Charon, Ceres, Eris, Haumea, Makemake, three large asteroids, three
comets, two interstellar visitors and six spacecraft. While the simulation plays, every body moves under the pull of
all the others, with the first post-Newtonian correction of general relativity and the oblateness of the planets.
The scene is generated, not authored: edit `tools/solar_system_bodies.py` and run the generator again.

Code:
- `NBodyComponent` in `src/simulation/components.cppm` (`GM`, `Velocity`, `J2`, `J2Radius`)
- `NBodySystem`, `NBodyAcceleration`, `NBodyStep`, `NBodyAdvance`, `NBodyEnergy` and `NBodySimulationModule` in `src/simulation/nbody_module.cppm`
- `BodyRotationModule` (`src/simulation/body_rotation_module.cppm`) turns the Surfaces, negative periods turn them backwards
- `tools/fetch_ephemeris.py` (JPL Horizons, cached in `tools/data/ephemeris_2026-10-22.json`),
  `tools/solar_system_bodies.py` (physical data), `tools/gen_solar_system.py` (writes the scene)

Tests: `tests/unit/simulation/nbody_tests.cpp` (integrator order, conservation laws, Mercury's perihelion, J2 nodal
precession, the module on a scene) and `tests/unit/simulation/solar_system_scene_tests.cpp` (the scene itself).

To regenerate: `tools/fetch_ephemeris.py --epoch YYYY-MM-DD` (needs the network), then
`tools/gen_solar_system.py --epoch YYYY-MM-DD`. Maps: `tools/fetch_textures.py` (the `core` group).

---

## 1. Frame and units

- Horizons gives geometric (no light time, no aberration) barycentric states in the ecliptic and mean equinox of J2000
  (`REF_PLANE=ECLIPTIC`, `CENTER=500@0`, `TIME_TYPE=TDB`). The scene uses scene (x, y, z) = ecliptic (x, z, −y): the
  ecliptic is the X–Z plane, +Y is ecliptic north, and the map is a proper rotation. The origin is the solar-system
  barycentre, so the Sun sits about 1.1 million km from it.
- Everything is SI. Gravity is stored as `GM` in m³/s², not as a mass: the products are known to 10 digits from the
  ephemerides, while G alone is known to five, so converting through kg would throw away the accuracy.
- The scene's `TimeScale` is 3600: one second of wall time is one simulated hour. The tick is 1/60 s, so a tick is a minute
  of simulated time. Raise it for the outer planets (the simulation window's time-scale control).

## 2. Dynamics

Acceleration of body i, with μ = GM (`NBodyAcceleration`):

1. **Newton.** a_i = Σ_j μ_j (r_j − r_i) / |r_j − r_i|³ over every body j that has a GM. A body with GM = 0 (comets,
   interstellar objects, spacecraft) feels the others and does not pull.
2. **1PN.** The Einstein–Infeld–Hoffmann terms of the JPL ephemerides, as in Newhall, Standish and Williams (1983)
   eq. 1 with β = γ = 1, summed over the massive bodies (the potential at i and j, the velocity terms, the
   Newtonian accelerations of the sources). It is applied to every body, so Mercury, the Moon and a test particle all
   get it. Switch with `NBodySimulationModule::SetRelativistic`.
3. **J2.** A body with `J2` pulls with the quadrupole of an oblate spheroid about its Sphere's `SpinAxis`:
   a = −(3/2) J2 μ R² / r⁵ · [(1 − 5 z²/r²) d + 2 z p̂], with d the offset from the oblate body, z = d·p̂. The oblate body
   receives the opposite momentum change, so total momentum is conserved. The pole is fixed in time (no precession of the
   axes), and it is the IAU pole of the body.

**Integrator.** Yoshida's 6th-order symplectic composition (solution A, seven kicks and eight drifts per step), in double
precision, state kept in SI. The step is 0.05 of the shortest dynamical time sqrt(r³/(μ_i + μ_j)) over all attracting
pairs, rounded down to a power of two, so the closest satellite orbit (Phobos around Mars, 4400 s per radian, step 128 s)
is always resolved and the step does not change on every tick. A tick that would need more than 4096 steps (128 s each, so about a year per second of wall time at 60 ticks per second) is taken in 4096 longer steps and a warning is logged once; the CPU
becomes the limit well before that.
The velocity-dependent 1PN term sits in the kick with the velocity at that moment; it is 10⁻⁸ of the Newtonian pull, so
the first-order treatment of its velocity dependence is far below the physics it adds.

The bodies are not PhysX actors. They have no `RigidBody`; `GravitySimulationModule` ignores them.

## 3. What the tests establish

Numbers from `tests/unit/simulation/nbody_tests.cpp` (see the file for the exact tolerances):

| Check | Result |
|---|---|
| Convergence on an eccentric (e = 0.2) Kepler orbit | the closure error after one orbit falls 64-fold when the step is halved: sixth order |
| Sun, Earth, Jupiter and the Moon for 10 years, Newtonian | relative energy change below 1e-10, momentum change below 1e-10 |
| 1PN, Mercury (a = 0.387 AU, e = 0.2056), 200 orbits | perihelion advance within 3 % of 6πGM/(c²a(1−e²)) = 0.1035″ per orbit (43″ per century); with 1PN off it is below 1 % of that |
| J2, a 60° inclined satellite at a = 7000 km | nodal regression within 3 % of −3/2 n J2 (R/a)² cos i |
| Momentum with J2 | the oblate body takes the opposite momentum change (to 1e-12) |
| The scene for 1 day (the default suite) | Newtonian energy change below 1e-8, momentum change below 1e-9 |
| The scene for 30 days against Horizons (`[slow]`) | see below |

**Against JPL Horizons after 30 days** (2026-11-21 00:00 TDB, state vectors of the scene advanced by the module at its default
time scale; positions in the barycentric frame, metres):

| Body | Error | Body (relative to its planet) | Error |
|---|---|---|---|
| Sun | 1.7 m | Moon | 118 m |
| Mercury | 7.3 km | Titan | 339 m |
| Venus | 4.8 km | Charon | 65 km |
| Earth | 4.1 km | Io | 59 km |
| Mars | 3.1 km | Phobos | 421 km |
| Jupiter | 1.7 km | | |
| Saturn | 1.3 km | | |
| Neptune | 6.3 km | | |

The planets' errors are the unmodelled small bodies and the model differences listed under Limitations. Phobos and Io
show the next harmonics of Mars' and Jupiter's gravity fields (only J2 is in), which put their along-track phase off after
a hundred and seventeen orbits. These are the numbers of this model at this epoch, not bounds: they grow with time.

## 4. Bodies

| Group | Source of the state | GM | Radius | Spin |
|---|---|---|---|---|
| Sun, planets, major moons | JPL DE441 and satellite ephemerides, Horizons | DE440 / satellite kernel summaries | IAU/NASA fact sheets | IAU WGCCRE 2015 (Archinal et al. 2018) |
| Ceres, Vesta, Pallas, Hygiea, Eris, Haumea, Makemake | JPL SBDB orbit solutions | literature values | literature | IAU where defined, else none |
| Halley, Hale–Bopp, 67P | SBDB (closest apparition) | 0 | nucleus size | none |
| 1I/ʻOumuamua, 3I/ATLAS | SBDB hyperbolic solutions | 0 | estimates | none |
| Voyager 1 and 2, New Horizons, Pioneer 10 and 11, Parker Solar Probe | Horizons trajectory kernels | 0 | metres | none |

Per body, `tools/solar_system_bodies.py` documents the source of each number.

**Spin phase.** The IAU gives the north pole (α₀, δ₀) and the prime meridian W = W₀ + Ẇ d. The generator takes the pole to
the scene frame as `SpinAxis`, `RotationPeriod` = 360°/Ẇ (negative when Ẇ is, as for Venus, Uranus and Triton), and
`PrimeMeridian` so that the engine's reference direction (+X carried to the pole by the shortest rotation, see
`BodyOrientation`) plus the PrimeMeridian lands on the IAU prime meridian at the epoch. Earth's comes out at a right
ascension of 29.89° against 30.15° from the standard GMST formula for the same instant (the rest is the UT1 − TDB offset
and the precession model). The periodic terms of the IAU series (nutation, libration) are left out.

**Moons.** The moons of the giant planets are synchronous rotators in reality and their IAU Ẇ equals their mean motion, so
the same face looks at the planet: Io's phase comes out right at the epoch to the accuracy of W₀. Hyperion and Nereid
rotate chaotically and are not turned.

**Maps.** Real maps for Mercury, Venus (the cloud deck: the surface is not visible from space), Earth, the Moon, Mars,
Jupiter, Saturn with its rings, Uranus and Neptune, from Solar System Scope (CC BY 4.0, `texture_sources.md`). Earth also
has relief, night lights, clouds and the atmosphere. Every other body shows a flat albedo colour from the literature
(`tint` in the table). The "fictional" maps of the dwarf planets in `assets/textures/` are not used: no spacecraft has
imaged Eris, Haumea or Makemake.

**Atmospheres** (`atmosphere()` in the generator): Earth has Hillaire's values. Venus, Mars, Titan and Pluto are
scattering shells with a haze, Jupiter, Saturn, Uranus and Neptune have a Rayleigh limb (hydrogen scatters weaker per
molecule than nitrogen) with methane absorbing red in the ice giants. The coefficients are scaled from Earth's by
composition and pressure and are not fits to observations.

**Lighting.** The Sun is both a `Star` (the disc) and a point `Light` of irradiance π at 1 AU falling with the inverse
square, so Mars gets 43 % and Neptune 0.11 % of Earth's light. The render profile `Solar system` is based on `Flat`
(straight rays: there is no black hole, so no sky probes and the `Light` is the only source) and uses automatic exposure.

## 5. Limitations

- **Spheres.** A planet is a sphere of its equatorial radius; Saturn's flattening (10 %) and Jupiter's (6 %) are in the
  dynamics (J2) but not in the picture. Small moons and asteroids are spheres of their mean radius. Haumea is a
  triaxial ellipsoid in reality.
- **Scale.** Spacecraft are metres across and sit at distances of up to 170 AU, so they are sub-pixel; they are in the
  simulation (select one and use a camera script on it) but not visible from far away. The same holds for the small
  moons. The `object paths` overlay draws their trails.
- **Missing bodies.** Pluto's small moons, the Martian and Jovian rings, the Kuiper belt and the asteroid belt as a
  population, comets' comae and tails, Jupiter's other moons. Their pull on the planets is in the DE441 GM of the
  planet system only where listed here, so the long-term barycentric motion differs slightly from the real one.
  Asteroids beyond Ceres, Vesta, Pallas and Hygiea are not included in the dynamics.
- **Poles.** Fixed in time; the true poles precess (Earth's by 50″/yr).
- **Saturn's rings are almost edge-on to the Sun on this date** (the ring-plane crossing was in 2025), so they are dim and
  cast a thin shadow: that is how the scene looks, not a rendering fault.
- **Exposure.** The Sun's irradiance falls with the square of the distance: Jupiter gets 3.7 % of the light Earth does, Neptune
  0.11 %. The profile `Solar system` uses automatic exposure with key 0.09 so every planet shows; the Sun's disc then
  comes out grey. `Solar system (sunny 16)` is a physical camera (f/16, 1/100 s, ISO 100): a white disc for the Sun, correct
  inner-system views, dark outer planets.
- **Tides, radiation pressure, Yarkovsky, non-gravitational comet forces:** none. The 1PN terms are the only relativity:
  frame dragging, the J2 of the Sun's own relativistic corrections and the 2PN terms are not.
- **The ephemeris drifts from the truth** at the level of the model: DE441 itself is a numerical integration with
  ~200 perturbing asteroids and tidal terms; this one has neither. Expect planet positions to stay within a few thousand
  km of Horizons for a few years for the inner planets, more for the moons (Moon: Earth's tides and the Sun's quadrupole
  are not modelled).
- **Time scale and cost.** Seven force evaluations per step, each over 52 bodies and the 39 that pull; one tick at the default
  time scale is one step. The cost grows linearly with the time scale (the step is fixed by Phobos' orbit), and has not been
  measured on a release build.
- **Camera follow lags one tick.** The spring-arm script reads the previous tick's position, so at a high time scale the
  camera trails its target by one tick of the target's motion. The default time scale keeps this below 1 % of the arm.

## Sources

- Park, R. S., Folkner, W. M., Williams, J. G. & Boggs, D. H. (2021). *The JPL Planetary and Lunar Ephemerides DE440 and DE441.* The Astronomical Journal 161, 105. doi:10.3847/1538-3881/abd414. Source of the planetary and lunar GM and of the barycentric states (via Horizons).
- Giorgini, J. D. et al. (1996). *JPL's On-Line Solar System Data Service.* BAAS 28(3), 1158. The Horizons system (<https://ssd.jpl.nasa.gov/horizons/>, API <https://ssd-api.jpl.nasa.gov/doc/horizons.html>).
- Newhall, X. X., Standish, E. M. & Williams, J. G. (1983). *DE 102: a numerically integrated ephemeris of the Moon and planets spanning forty-four centuries.* Astronomy & Astrophysics 125, 150–167. The 1PN equation of motion, eq. 1 with β = γ = 1.
- Yoshida, H. (1990). *Construction of higher order symplectic integrators.* Physics Letters A 150, 262–268. The 6th-order composition and its coefficients.
- Archinal, B. A. et al. (2018). *Report of the IAU Working Group on Cartographic Coordinates and Rotational Elements: 2015.* Celestial Mechanics and Dynamical Astronomy 130, 22. doi:10.1007/s10569-017-9805-5. Poles, prime meridians and rotation rates.
- Jacobson, R. A. et al. (2006, 2014) and the JPL planetary satellite physical parameters page (<https://ssd.jpl.nasa.gov/sats/phys_par/>). Moon GMs and radii, planetary J2.
- Hillaire, S. (2020). *A Scalable and Production Ready Sky and Atmosphere Rendering Technique.* Computer Graphics Forum 39(4). Earth's atmosphere coefficients (`atmospheres.md`).
- Solar System Scope (<https://www.solarsystemscope.com/textures/>), CC BY 4.0. Planet and Moon maps (`texture_sources.md`).
