# Body lighting: sky probes, spherical harmonics and Light components

A planet near a black hole is lit by what is around it: the accretion disk, the stars, the starfield, other planets,
all bent by the hole's field. A planet in the solar system is lit by the Sun. These notes describe how a Surface
(`textured_bodies.md`) is lit in both cases: each body traces a small sky image from its centre with the same
tracer as the camera, reduces it to one directional light plus nine spherical-harmonic (SH) coefficients, and the
shader adds the scene's `Light` components.

Code:
- `LightComponent` in `src/simulation/components.cppm`
- `BodyLightingGpu`, `LightGpu`, `LightBlockGpu`, `FillLightData`, `ProbeDirection`, `ShBasis`, `ProjectRadianceSh`,
  `EvaluateIrradianceSh` in `src/rendering/bodies.cppm`
- `TraceStage::RecordProbes` and `UploadBodies` in `src/rendering/render_pipeline.cpp`
- `GPP_BODY_PROBE` main in `shaders/black_hole_rendering.comp`; `PROBE_PASS`, `g_skipSphere`, `g_probeVelocity`,
  `traceObserverVelocity` in `shaders/trace_common.glsl`
- `shaders/body_probe_reduce.comp`
- `gatherLights`, `skyAmbient`, `shIrradiance`, `shadeBodyLight`, `surfaceLightFilter`, `shadeBodySurface` in `shaders/body.glsl`
- `BodyObserverVelocity` in `src/simulation/observer_readout.cppm`
- render setting `bodies.lighting` in `src/rendering/render_settings.cpp`

Tests: `tests/unit/rendering/bodies_tests.cpp` (probe directions, SH of a uniform sky, SH of a half-lit sky, light upload).

Conventions: `observers.md` (ZAMO, boosted observers), `m2_rendering_references.md` §2 (SH formulas, checked against
Ramamoorthi & Hanrahan 2001).

---

## 1. Lighting modes

The setting `bodies.lighting` has two values; the transport adds a third.

| Mode | Chosen when | Light on a Surface |
|---|---|---|
| Fixed | `bodies.lighting` = Fixed | one white light from (1, 1, 1), irradiance π at normal incidence, plus an ambient 0.03 · albedo. `Light` components are ignored |
| Probe | `bodies.lighting` = Probe (default) and the transport is Analytic lens or Integrated | the probe's brightest source as a directional light, the rest of the probe as SH ambient, plus the `Light` components |
| LightsOnly | `bodies.lighting` = Probe and the transport is Straight | the `Light` components only; no probes are traced, no ambient |

Fixed is for looking at a surface. LightsOnly is picked by `UploadBodies`; it has no setting of its own. With no light at
all a Surface is black.

## 2. The sky probe

In Probe mode every sphere with a Surface traces a **64 × 32 equirectangular image of the light arriving at its
centre** (`kProbeWidth` × `kProbeHeight`), once per frame. The probes of all bodies are bands of one
`R32G32B32A32_SFLOAT` image of 64 × (32 · 16) texels (`kMaxSpheres` = 16).

- **Directions.** The probe is world-aligned, with the map convention of `textured_bodies.md` §2: v = 0 at +Y, u = 0.5
  towards +X, east towards −Z (`ProbeDirection`, `probeDirection`). It does not rotate with the body.
- **Same tracer.** The `GPP_BODY_PROBE` entry point calls `rk4RayMarching` (Integrated) or `hybridRayTrace` (Analytic
  lens) from the body's centre with the texel's direction. One ray per texel, taken at the texel centre. A probe
  kernel is compiled for f32 and, when the camera kernel is f64, for f64 as well.
- **What is left out.** The body's own sphere and its own atmosphere are skipped (`g_skipSphere`). Debug views, orbit
  and radii overlays, trails and the third-person crosshair are switched off (`PROBE_PASS`).
- **What is seen.** The disk, the starfield, other spheres, meshes, and the atmospheres and clouds of other bodies. A
  sphere without a Surface has no probe (`flags.x == 0`).
- **The observer is the body.** For a Geodesic Body in free fall around the first hole, the probe uses the body's
  velocity relative to the ZAMO (`BodyObserverVelocity`, the same function that gives a camera riding the body its
  velocity). The photon directions are boosted exactly as for a moving camera (`observers.md` §5), so the light is
  aberrated and the disk is Doppler shifted as the body moves. A body that is not a Geodesic Body in free fall is a
  ZAMO for its probe.
- **The starfield.** As for a moving camera, the skybox is aberrated but not colour- or brightness-shifted
  (`observers.md` Limitations).
- **Other bodies in a probe.** A body seen by a probe is shaded with the lighting of the previous frame
  (`u_bodyLighting` is read before this frame's reduction has written it). A planet lit by a moon that is itself lit
  by a star therefore lags by a frame. Light from a body reaches its neighbours only through this loop.

## 3. Reduction: one source and nine coefficients

`body_probe_reduce.comp` runs one workgroup per body after the probes (the render graph passes `BodyProbes` and
`BodyProbeReduce`, then `Trace`). For the 2048 texels of a probe:

1. The **strongest texel** is the one with the largest luminance × solid angle (Rec. 709 weights 0.2126, 0.7152, 0.0722;
   the solid angle of a texel is (2π/64)(π/32) cos(latitude)).
2. Every texel within **15°** of it (cos > 0.9659) is summed into one **directional light**: its direction is that of the
   strongest texel, its irradiance at normal incidence is Σ L · dΩ over those texels.
3. All other texels are projected onto the nine real SH basis functions, L_lm = Σ L · Y_lm · dΩ (per colour channel).

`shIrradiance` then evaluates the diffuse irradiance for a normal n with the quadratic form of Ramamoorthi &
Hanrahan (2001), eq. 13, constants c1 to c5, and the shader clamps it at 0. Their 9-term fit has an error of a few
per cent (`m2_rendering_references.md` §2). Two tests check the CPU mirror: a uniform sky of radiance L gives
irradiance π L, and a sky lit from the upper hemisphere follows E = π(1 + n_y)/2 to 3 %.

Why separate the brightest source: with SH alone a small, very bright source (a star, the Sun) is smeared over the
hemisphere and gives no terminator. As a directional light it casts a sharp one, and the specular lobe sees it.

The shading in Probe mode (`shadeBodySurface`):

    colour = albedo/π · E_SH(N)                              (diffuse sky; no Fresnel, no specular)
           + Σ over lights ( Lambert + GGX ) · E_i · filter_i · max(N·L_i, 0)
           + emissive

The lights are the probe's source and the `Light` components (at most 8, `kMaxLights`). `skyAmbient` exposes the SH band 0
(the mean radiance of the sky without the source) to the atmosphere and cloud code (`atmospheres.md`).

## 4. Light components

`LightComponent`: `Kind` (Directional or Point), `Color`, `Intensity`, `Direction`, `ReferenceDistance`.

- **For lights that are not in the traced scene**: the Sun for Earth or Saturn.
- **Units.** `Intensity` is the irradiance in display units, default π, so that a white Lambertian surface facing the
  light shows 1.0 for its diffuse term (`textured_bodies.md` §8). The uploaded colour is `Color · Intensity`.
- **Directional.** `Direction` points *towards* the light and is normalised on upload.
- **Point.** The position is the entity's Transform. The irradiance falls as (ReferenceDistance / distance)², with
  `ReferenceDistance` = 1 AU by default. The distance is measured from the body's surface point, taken as the centre
  plus N · radius.
- **Straight lines.** A `Light` lights a Surface along a straight line. There is no lensing, no redshift and no
  shadow from other bodies (no eclipses). The light that crosses an atmosphere, a cloud shell or a ring is attenuated
  there (`surfaceLightFilter`, `atmospheres.md`).
- **Mode.** `Light` components are used in Probe and LightsOnly mode. Fixed ignores them.

## 5. Horizon and exports

- **The body shadows itself.** A light is skipped when the *geometric* normal faces away from it, whatever the normal
  map or terrain relief says (`dot(geometricN, L) <= 0`). A normal map cannot light the night side.
  The SH ambient term has no such test.
- **Exports wait.** The trace stage reports "not ready" until the probes have run twice and all textures are loaded
  (`m_ProbeFrames >= 2`, `PendingCount() == 0`). The second probe pass sees bodies lit by the first.
- **Cost.** 2048 full geodesic traces per Surface sphere per frame, in addition to the camera. The setting has
  `Cost = 1`; Fixed and the Straight transport avoid it.

## 6. Checked by renders

Observed during the implementation with the viewport and with `--export-image`; not repeated for these notes.

- **Earth with a directional Sun** (`templates/solar-system-bodies.yaml`) shows a terminator and an ocean glint.
- **A planet lit only by a nearby star sphere** is lit from the star's side.
- **A planet near a black hole and no `Light`** is lit by the disk through its probe.

## Limitations

- **Calibration.** The radiance scales of the sources are not matched. A star sphere radiates its blackbody LUT colour,
  which is about 1, so it lights a planet weakly. The disk and the skybox are on different scales too. In
  `templates/interstellar-bodies.yaml`, planets at 40 to 80 M from Gargantua look dim against the starfield
  under auto exposure.
- **One ray per texel, 5.6° texels.** A source smaller than a texel can fall between the rays. The brightest source
  then flickers or disappears from the probe. Not tested; this follows from the sampling.
- **One source.** A second bright source (a binary) goes into the SH ambient and is blurred.
- **Specular and shadows.** The SH ambient has no specular term. Other bodies cast no shadows on a Surface; a body that
  blocks a source in the probe darkens the probe's texels, so it lowers the light from that direction, but only
  at the resolution of the probe grid (64 × 32).
- **First hole only.** The probe observer is defined relative to the first black hole. A body following another hole
  is a ZAMO for its probe.
- **Frame lag.** §2: one frame for each hop of light between bodies.
- **Fixed ambient.** Fixed mode uses 0.03 · albedo; Probe mode gets its ambient from the probe.

---

## Sources

- Ramamoorthi, R. & Hanrahan, P. (2001). *An Efficient Representation for Irradiance Environment Maps.* Proc. SIGGRAPH 2001, 497–500. doi:10.1145/383259.383317. Eq. 13 and the constants c1 to c5: checked, see `m2_rendering_references.md` §2.
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001. doi:10.1088/0264-9381/32/6/065001, arXiv:1502.03808. The camera boost used for the probe observer: eqs. A.7 to A.11, see `observers.md`.
- Rec. ITU-R BT.709. The luminance weights 0.2126, 0.7152, 0.0722. Not read for this note.
- The observers and the aberration: `observers.md` §§1 to 5 and its sources.

*The code statements were read in the files listed at the top. The render results in §6 were reported during the
implementation, not re-run when these notes were written.*
