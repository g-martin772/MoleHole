# Local frames: standing on a planet in the throat of a near-extremal hole

A planet on Miller's orbit around Gargantua cannot be drawn by the global tracer. The planet is smaller than the
resolution of the radial coordinate there, and the integrator could not reach the orbit in the first place. These
notes explain the numbers, the **local frame** that renders a nearby body in its own flat space, and the changes to
the GPU integrator that make the throat reachable.

Code:
- `ChooseLocalFrame`, `kLocalFrameRadii` and `BodyLightBlockGpu` in `src/rendering/bodies.cppm`
- `UploadBodies` in `src/rendering/render_pipeline.cpp`
- the local-frame branch of the camera `main` in `shaders/black_hole_rendering.comp`
- `horizonDelta`, `captureMargin`, `radialSpeedScale`, `Spacetime::rMinus` in `shaders/kerr.glsl`
- the step-size and pole-rate changes in `rk4RayMarching` in `shaders/ray_tracing.glsl`
- `g_bodyObserver` and `traceObserverVelocity` in `shaders/trace_common.glsl`
- `renderSphereSurface` in `shaders/sphere.glsl`

Tests: `tests/unit/rendering/bodies_tests.cpp` covers the body entries. The local-frame choice and the shader
changes have no unit test; they were checked by renders (§6).

Conventions and the near-extremal numerics: `near_extremal.md`, `observers.md`. The validity argument for a flat patch
is in `m2_rendering_references.md` §4.

---

## 1. The problem in numbers

Gargantua: 10⁸ M☉, so M = GM/c² = 1.4767 × 10¹¹ m.

| Quantity | Value |
|---|---|
| Miller's orbit | r − M = 4 × 10⁻⁵ M (`interstellar_planets.md` §2) |
| ISCO | 3.7 × 10⁻⁵ M above M (`near_extremal.md` §1) |
| Earth-sized planet, radius 6371 km | 4.3 × 10⁻⁵ M |
| radial stretch at the ISCO | √g_rr ≈ M/(r − M) ≈ 2.7 × 10⁴ |
| radial extent of the planet | Δr ≈ 4.3 × 10⁻⁵ M / 2.7 × 10⁴ ≈ 1.6 × 10⁻⁹ M |

Near extremality Δ ≈ (r − M)², so √g_rr ≈ M/(r − M) and one coordinate unit of r is a very long proper distance. The planet
spans 1.6 × 10⁻⁹ M in Boyer–Lindquist r. A float has a resolution of 1.2 × 10⁻⁷ at r ≈ 1, so the planet is far below it. The
ray integration works in r, where the whole planet is a single float value.

A second problem came before it. The old GPU capture radius was r₊ + 0.01 M, and the GPU spin is a float that rounds
to 1. Miller's orbit at 4 × 10⁻⁵ M above M is inside that radius, so every ray launched there was captured, or ran
out of steps (§3), and the planet could not be reached at all.

## 2. The local frame

When the camera is closer than **20 body radii** (`kLocalFrameRadii`) to the centre of a sphere, that body is rendered in
its **own flat local frame**:

- **The camera.** The camera offset from the body's centre is taken in double precision, in world metres, and
  converted to render units (`ChooseLocalFrame`). It is uploaded as `PatchCamera` with the sphere's index. The offset
  is treated as a *proper* offset along the world axes. No coordinate transformation is applied.
- **The body.** It is a sphere of its proper radius around the origin of the patch. A body with a Terrain is
  ray-marched (`terrain.md`); the others use the sphere intersection. The shading is `shadeBodySurface` with the usual
  lighting (`body_lighting.md`). The atmosphere, the clouds and the rings are composited from the exact patch
  geometry, in RGB (`atmospheres.md`).
- **Everything else.** A ray that does not hit the body is traced from the body's **centre**, with the body skipped
  and with the body's velocity relative to the ZAMO as the observer (`g_bodyObserver`). The sky, the disk and the other
  bodies are seen as the body sees them, aberrated and Doppler shifted as in a probe (`body_lighting.md` §2). This
  works in both f32 and f64, because the integration starts at the body, in the same coordinates in which it
  was set up.
- **When it is used.** Only for the nearest sphere, only with the Analytic lens or Integrated transport (the Straight
  transport has no spacetime), and not in third-person mode.
- **Depth.** The patch hit is written as an ordinary solid hit at the position `rayOrigin + rayDir · t`, so meshes and
  the raster passes after the trace still sort against it.

The depth clamp has its own reason. With a near plane of 10 m and a far plane of 1000 AU, a solid hit at 3.6 × 10⁸ m
projects to within float rounding of depth 1.0, the depth of the sky. The composite then dropped random pixels of the
planet. Solid hits are now clamped below `FAR_DEPTH` (`min(hitDepth, FAR_DEPTH − 1e-6)`), so they stay in front of the
sky.

Why a flat patch is acceptable: the metric differs from Minkowski over a patch of size L by about R L², with the
curvature R of order M/r³ (Fermi normal coordinates; `m2_rendering_references.md` §4.2). For a planet of radius
4.3 × 10⁻⁵ M the correction is small if R is of the order M/r³ for r near M. That estimate was not computed for the
throat itself, where the near-extremal geometry is unusual: Not verified.

## 3. Changes to the GPU integrator

Tracing the rest of the world *from* the throat needs rays that can leave it. These changes are in
`shaders/kerr.glsl` and `rk4RayMarching`.

| Change | Before | After |
|---|---|---|
| Δ | r² − 2Mr + a² + Q² | (r − r₊)(r − r₋) with r₋ = 2M − r₊ (`horizonDelta`). The expanded form cancels near the horizon of a near-extremal hole, the factored form keeps the precision of r − r₊. Same idea as `HorizonDelta` on the CPU (`near_extremal.md` §3) |
| Capture margin | r − r₊ < 0.01 M | r − r₊ < 0.01 · max(r₊ − M, 10⁻⁵ M) (`captureMargin`). The same 0.01 M for Schwarzschild; 10⁻⁷ M for the clamped Gargantua of the GPU, below the ISCO at 3.7 × 10⁻⁵ M |
| Radial step | h = step · rate · (r − r₊) | h multiplied by max(1, 0.1 / v_max) (`radialSpeedScale`), v_max = √A \|E − ωL\| / Σ |
| Angular step cap | counts \|dθ/dλ\| + \|dφ/dλ\| | counts \|dθ/dλ\| + \|L/sin²θ − aE\| / Σ |

- **Slow photons.** v_max is the largest coordinate speed \|dr/dλ\| a photon of (E, L) can have at (r, θ), the one moving
  radially in the ZAMO frame. It is about E far from the hole but tiny in the throat. A photon of unit local energy
  needs about 1/v steps of a fixed affine size to climb out. The factor leaves rays with v_max ≥ 0.1 unchanged.
- **The pole term.** The step bound was meant to resolve rays that pass close to the polar axis, where dφ/dλ ≈ L/sin²θ. The
  frame-dragging part of dφ/dλ, aP/(ΔΣ), is large in the throat: a photon leaving it winds by about 10⁴ rad of coordinate
  φ without moving anywhere. That part is now left out of the cap. For a = 0 the two forms are identical.
- **Before the changes.** Every ray launched at Miller's orbit ran out of steps.
- **Derived: the margin.** The GPU clamps the spin to extremal, which puts r₊ at about 1.0000002 M, so
  max(r₊ − M, 10⁻⁵ M) = 10⁻⁵ M and the margin is 10⁻⁷ M. That is below the f32 spacing of r near 1 (1.2 × 10⁻⁷). The capture
  test in f32 is therefore at the resolution limit, which is one reason the shadow edge differs slightly between f32
  and f64.

## 4. What the views show

Reported from renders during the implementation (`templates/interstellar-bodies.yaml`, Integrated, auto exposure):

- **From Miller's orbit** the termination view shows the shadow filling about half of the view when looking radially
  outward, and the disk most of the rest.
- **f64** shows the true blueshift, which is extreme brightness; use auto exposure. **f32** shifts the shadow edge
  slightly.
- **First compile.** The f64 kernels take about 5 minutes to compile the first time.

## 5. Effect on existing scenes

The integrator changes were compared with the commit before them on `templates/sgr-a-star.yaml`,
`gargantua-orbits.yaml`, `test-scene.yaml`, `gravitation-showcase.yaml` and `dev.yaml` (230400 pixels each, Lensed and
Traced profiles):

| Profile | Result |
|---|---|
| Lensed | effectively identical, PSNR ≥ 96 dB |
| Traced | PSNR 67 to 70 dB; at most 16 of 230400 pixels differ by more than 2 levels, all on the shadow edge |

The Lensed profile uses the analytic lens, not the integrator, which is why it is expected to be unchanged. The
Traced differences are at the shadow edge, where the capture test sits. These numbers were measured during the implementation and
were not repeated for these notes.

## Limitations

- **Parallax.** The rest of the world is traced from the body's centre, so the background has a parallax error up to
  the radius of the patch (up to 20 body radii). For a planet in the throat this is a tiny angle; for a small body
  near a large one it is not.
- **Distant views of a deep body.** Outside 20 radii a body in the throat is a plain Cartesian sphere. Its shape is
  wrong (the radial coordinate stretch is ignored) but the error is below a pixel at such distances.
- **The camera is assumed comoving.** The patch camera uses the body's velocity as the observer, not
  `camera.observer`. A camera moving relative to the body is not aberrated against it.
- **No gravity in the patch.** The patch is flat; a ray inside it is a straight line. The light bending between the
  patch and the hole is only in the background, traced from the centre.
- **The disk's noise aliases deep in the throat.** The disk noise depends on φ, which winds many times in the
  throat. This predates M2.
- **Flat estimate.** The validity of the flat patch near the throat is an estimate (§2).
- **f64 compile time.** About 5 minutes the first time.
- **Single hole.** The velocity and the observer are relative to the first black hole.
- **A GPU spin of exactly 1.** The spin is a float; the horizon is the extremal one, 1.0000002 M, not the exact
  1 + 1.6 × 10⁻⁷ M. The CPU simulation keeps the exact value (`near_extremal.md` §4).

---

## Sources

- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). *Rotating black holes: locally nonrotating frames, energy extraction, and scalar synchrotron radiation.* ApJ 178, 347. doi:10.1086/151796. The throat and the ZAMO frame; see `near_extremal.md`.
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001. doi:10.1088/0264-9381/32/6/065001, arXiv:1502.03808.
- Schnittman, J. D. (2019). *Life on Miller's Planet: The Habitable Zone Around Supermassive Black Holes.* arXiv:1910.00940. The orbit r − M = 4 × 10⁻⁵ M (read, per `interstellar_planets.md`).
- Manasse, F. K. & Misner, C. W. (1963). *Fermi normal coordinates and some basic concepts in differential geometry.* J. Math. Phys. 4, 735. doi:10.1063/1.1724316 (not read; see `m2_rendering_references.md` §4.2).
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation.* W. H. Freeman. §13.6 (not verified).

*The code statements were read in the files listed at the top. The numbers in §1 are derived from the values in
`interstellar_planets.md` and `near_extremal.md`. The render results of §4 and §5 were reported during the
implementation and are not re-run here.*
