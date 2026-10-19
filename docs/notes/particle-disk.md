# Particle disk rendering

When the heaviest hole with an `AccretionDisk` has `Model = Particles`, the tracers draw that disk from the particles of
`ParticleStage` instead of the analytic disk (`docs/notes/disk_rendering.md`). With `Model = Analytic`, or no component, the image
is byte-identical to before (checked with the shaders of the previous commit, Flat, Lensed and Traced, thin and volumetric).
Roadmap: `docs/physics-roadmap/06-accretion-and-particles.md` section 3, `04-gr-physics-core.md` section 6.

Code: `src/rendering/particle_splat.*` (grid layout, CPU reference, `ParticleSplatter`, `ParticleSplatStage`),
`shaders/particle_splat.comp`, `particle_grid.glsl` (grid block, sampling), `particle_disk.glsl` (Lensed and Traced transfer, g),
`particle_flat.glsl` (Straight), `disk_radiance.glsl` (colour and brightness calibration shared with the analytic disk),
`disk_transfer.glsl` (formal solution).

Units: positions in r_g of the hole (the hole's own mass, whatever the render length unit), velocities in c, density as
D = rho r_g [kg/m^2], temperature in K, opacity kappa in m^2/kg.

## 1. Splat

Particles (position relative to the hole, world axes, mass in kg, velocity, temperature) are deposited into two nested Cartesian
grids centred on the hole, N^3 voxels each (`disk.particle_grid`, default 128):

| Level | Half width | Voxel at N = 128, OuterRadius = 100 |
|---|---|---|
| 0 fine | 0.32 R_out | 0.5 r_g |
| 1 coarse | 1.6 R_out | 2.5 r_g |

The kernel is the quadratic B-spline (TSC): support 3 voxels per axis, weights sum to 1, so mass is conserved away from the grid
edge. Per node five sums are accumulated: m, m v (3), m T / T_ref. The resolve pass applies a binomial [1 2 1] / 4 filter per
axis (shot noise of a sparse particle set; weights still sum to 1) and writes two float textures per level, extensive quantities
per r_g^2 (`RGBA32F`: D, D T / T_ref, D v_x, D v_y; `R32F`: D v_z). Sampling interpolates the extensive quantities and divides by
D afterwards, so temperature and velocity are density weighted and an empty neighbour does not pull them to zero. The fine level is
blended into the coarse one over its last 5 voxels, the coarse level fades out over its last 8 %.

Accumulation: float atomics (`VK_EXT_shader_atomic_float`) in interactive views; signed fixed point in `uint` atomics
without that extension and always in exports, where the sum must not depend on the order of the atomics (float sums differ in the
last bits from run to run). Unit = 2^-12 of the mean particle mass (a voxel holds up to 2^19 mean particles, temperature 2^17).
Cost at N = 128, 1e5 particles: 3-5 ms for both levels (Debug build, RTX 3060).

Test V-P8 (`tests/unit/rendering/particle_splat_tests.cpp`): grid mass equals particle mass (1e-5), per-voxel agreement with the CPU
reference (fixed point 1e-3 of the peak, float 1e-4), identical bits over repeated runs.

## 2. Transfer

Grey gas, Kirchhoff: absorption alpha = kappa rho, emission j = alpha B(T). Per step of proper length ds in the gas frame
dtau = kappa D ds / r_g, I += T S (1 - e^-dtau), T *= e^-dtau (`disk_transfer.glsl`; the series form below dtau = 1e-3, because
1 - e^-x is 0 in float below 6e-8 and would drop thin gas). Exact for a constant source per step, so the result does not depend on
the step size (test: uniform slab, 1 to 4096 steps, against 1 - e^-tau). Thick gas gives the blackbody of the local temperature, thin
gas scales with rho.

Sampling:
- **Integrated:** each RK4 step samples the grid at the midpoint of its chord; the step is capped to 0.4 voxel; the
  integration radius grows from 50 M to the coarse half width (so gas out to 1.6 R_out is lensed exactly).
- **Analytic lens:** the influence zone of the hole grows likewise; steps grow with the distance but stay below half a voxel.
- **Straight:** straight ray through the coarse box, half a voxel per step.

`g` from the photon momentum and the fluid velocity: the grid velocity is read as the coordinate velocity dx^i/dt, u^t from the
metric, -p.u = u^t (E - p_r v_r - p_theta v_theta - L Omega), ds = (-p.u) d lambda, g = E_camera / (-p.u) with the toggles of
`radiometry.redshift` and `radiometry.doppler` like `integratedDiskG`. The analytic lens has no geodesic momentum: it launches
the photon from the ZAMO at the sample with the reverse of the straight-line ray (as `analyticLensDiskG`). The Straight kernel uses
1 / (gamma (1 + d.v)) times sqrt(1 - 2M/r). For a Keplerian velocity the formula reduces to `diskRedshift`.

## 3. Calibration

Colour and brightness use `diskBlackbodyRadiance`, the code of the analytic disk: colour from the blackbody LUT at g T, brightness
`DISK_REFERENCE_LUMINANCE` (0.25) (g T / T_ref)^4. T_ref is the maximum of the Novikov-Thorne temperature of the particle disk
at the component's own accretion rate (`DiskPeakTemperature`, T^4 = c (1 - sqrt(r_in/R)) / R^3, peak at R = 49/36 r_in), which the
particles relax to. So at steady state gas at the peak of the profile shows 0.25 like the analytic disk, and the radial colour
gradient is the same. Differences that remain are physical: the particle disk has the optical depth its mass gives (typically 1 to
100, the analytic disk always `disk.opacity` = 2), it spreads beyond the nominal outer radius, it has no turbulence texture, and
its accretion rate is the component's, not `disk.accretion_rate`. `disk.display_temperature` rescales by T_ref.
`disk.particle_opacity_scale` multiplies kappa.

## 3b. Disk plane grid, adaptive smoothing (W3)

Root causes of the blobby look of the first renderer: (1) the splat stood on 3D voxels of 0.5 to 2.5 r_g while the viscous layer is
H = h R = 0.4 to 3 r_g thick, so a thin disk was a few voxels deep and every particle a cubic kernel; (2) 1e5 particles over a
disk of 1e3 r_g^2 is a few per cell, shot noise; (3) the templates held 1e-6 M_sun, tau of 1e6 per r_g; (4) a 1e4 K stream next to
a 1e6 K reference is black in a T^4 brightness; (5) the "white blob" is the Doppler-boosted approaching inner disk (g^4), not an
artefact.

- **Plane grid.** Gas that is near circular (radial speed < 0.2 v_K, azimuthal within 0.5 v_K of circular, 4 to 6 H) is weighted
  into a 2D grid in the plane of the disk (`disk.particle_disk_grid` = 1024 cells over +-1.6 R_out, TSC + [1 2 1] filter, same
  fixed point atomics, `ParticleDiskWeight`), column density per area; the tracer multiplies it by the Gaussian profile of
  H = h R. Everything else (streams, plunging gas, the thick tails) stays in the 3D grids. The share is smooth, mass is conserved
  (test). Mixed gas uses the emission weighted temperature (mean of T^4).
- **Adaptive kernel.** `disk.plane_smoothing` / `disk.particle_smoothing` = particles per cell: the density read at mip 2 says how
  many particles a cell holds, the sample is taken at the finest mip that holds the target (box-filter mip chains of the 3D and
  plane grids, `particle_mips*.comp`, trilinear). Dense gas keeps full resolution, sparse gas is smoothed instead of blocky.
- **Jitter** (`disk.particle_jitter`): the sample inside each march step is offset by interleaved gradient noise per pixel.
- **Band** (`radiometry.band`): Bolometric = T^4 (the 0.25 calibration), Visible = Planck at 550 nm relative to the peak, so
  gas far below the peak (streams, outer disk) stays visible. `radiometry.sky_gain` / `disk_gain` (EV) put the display
  referred sky and disk back into the picture when exposure is lowered for physical stars (post.exposure -12 for the Sun-like
  radiance on meshes).
- **Dynamics for a binary** (`AccretionDisk`): `SeedMass/SeedInner/SeedOuter/SeedParticles` warm start (32 rings, steady-state
  profile), `TruncationRadius` (tidal torque outside it, not on near circular gas...), `ShockDamping` (extra radial damping of
  non-circular gas: a stream circularises on its angular momentum), `ViewRadius` (grid half width). Export macro step is 8 GM/c^3
  again (64 let the inner disk drift visibly).
- **Disk light of a particle disk** uses the component's own accretion rate and the PW efficiency 1/16 (it used the analytic
  `disk.accretion_rate` before).
- Cost (RTX 3060, Debug): splat 5 ms at 128^3 + 1024^2 plane, 25 ms at 256^3; Trace 100 to 150 ms at 1080p (Lensed, 0.4 to 1e6
  particles), unchanged by the plane grid. `templates/xray-binary.yaml`: 3.5 to 8 s per 1920x1080 export.

## 4. Exports

`--sim-start T` advances the particle system synchronously from t = 0 to T (before, a reset started the particles at T). Exports
use a macro step of 64 GM/c^3 instead of 8 (the block-step scheduler subdivides for fast particles anyway) and record up to 240
macro steps per command buffer. 100 s of a 10 M_sun disk take 27 s, 200 s about 45 s (RTX 3060, Debug). The state at T lags the
request by less than one macro step (64 GM/c^3, 3 ms at 10 M_sun). Two exports with the same arguments are byte identical
(images in Flat, Lensed and Traced; a 6 frame video, frame by frame and as a file); a video export advances every frame.

## Approximations and limits

- Only the heaviest hole with a particle disk (and only if it is the tracer's hole in the first 8); spin is ignored by the particle
  dynamics (Paczynski-Wiita), but the metric of the tracer is the selected one.
- The PW coordinate velocity is used as dx/dt of the Boyer-Lindquist metric; PW is not a geodesic flow.
- Super-thick matter (the templates `accretion-ring`/`accretion-stream` hold 1e-6 M_sun and more: tau of 1e6 per r_g in a voxel
  that holds 1e-3 of one particle) is opaque wherever the kernel reaches. Scale the mass rate and `Opacity` (kappa 0.0034 m2/kg
  and 3e15 kg/s give tau_half 5 to 40 for a 10 M_sun hole, see `templates/xray-binary.yaml`) instead of using such masses.
- The grid is rebuilt every frame (no temporal filtering); outside the coarse box there is no gas; no light-travel time (fast light).
- The raster mesh overlay does not see the disk's opacity (only traced meshes are occluded, as with the analytic disk).
- Cost: the Lensed kernel at 1920x1080 takes about 50 ms with the particle disk against 9 ms analytic; Traced 50 ms against 17 ms;
  Straight 17 ms against 0.3 ms. A smaller `disk.particle_grid` takes larger steps.
