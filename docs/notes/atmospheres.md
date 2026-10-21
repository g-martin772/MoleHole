# Atmospheres, clouds and rings

A planet seen from orbit has a thin blue limb, a reddened terminator and haze over its mountains. This note describes
the three components that add that: **Atmosphere** (single scattering after Hillaire 2020, with an isotropic
multiple-scattering estimate), **Cloud Layer** (a thin shell) and **Ring** (a flat annulus in the equatorial plane). It
says what the shader computes, how the result is composited into the geodesic tracer, and what is left out.

Code:
- `AtmosphereComponent`, `CloudLayerComponent`, `RingComponent` in `src/simulation/components.cppm`
- `FillAtmosphereGpu` and the cloud and ring parts of `FillBodyData` in `src/rendering/bodies.cppm`
- `atmosphereExtinction`, `atmosphereTransmittance`, `atmosphereSegment`, `cloudAt`, `bodyLayers`, `ringCrossing`,
  `ringSample`, `ringRadiance`, `bodyAllLayers`, `surfaceLightFilter` in `shaders/body.glsl`
- `segmentLayers`, `foldLayers`, `composeLayers` in `shaders/ray_tracing.glsl`
- the layer pass in the camera `main` of `shaders/black_hole_rendering.comp`
- render settings `bodies.atmospheres` and `bodies.atmosphere_samples` in `src/rendering/render_settings.cpp`

Tests: `tests/unit/rendering/bodies_tests.cpp` ("Atmospheres and clouds keep a Surface lit and arrive in render units"). The shader
code itself has no unit test.

References: `m2_rendering_references.md` §3 (Hillaire 2020, checked), `interstellar_planets.md` §6 (coefficients and
presets), `body_lighting.md` (where the light comes from).

---

## 1. The model

`AtmosphereComponent` follows Hillaire (2020) §4. All coefficients are per metre; `FillAtmosphereGpu` converts them to the render
unit (`LengthUnitM`) on the CPU: lengths are divided and rates multiplied by it.

| Field | Default (Earth, Hillaire 2020 Table 1) |
|---|---|
| `Height` | 100 km above the sphere's radius |
| `RayleighScattering` | (5.802, 13.558, 33.1) × 10⁻⁶ m⁻¹ at 680, 550, 440 nm |
| `RayleighScaleHeight` | 8 km |
| `MieScattering`, `MieAbsorption` | 3.996, 4.40 × 10⁻⁶ m⁻¹ (grey) |
| `MieScaleHeight` | 1.2 km |
| `MieAnisotropy` | 0.8 (Cornette–Shanks g) |
| `OzoneAbsorption` | (0.650, 1.881, 0.085) × 10⁻⁶ m⁻¹ |
| `OzoneCenter`, `OzoneWidth` | 25 km, 30 km (tent profile) |

At altitude h, with the densities d_R = exp(−h/H_R), d_M = exp(−h/H_M) and the ozone tent
d_O = max(0, 1 − |h − centre| / half-width), the extinction is

    σ_t(h) = σ_R d_R + (σ_M,s + σ_M,a) d_M + σ_O d_O

Phase functions: Rayleigh 3(1 + μ²)/(16π); Mie Cornette–Shanks, 3/(8π) (1 − g²)(1 + μ²) / ((2 + g²)(1 + g² − 2gμ)^{3/2}),
with μ the cosine of the angle between the ray and the light direction (both as in `m2_rendering_references.md` §3.1).

## 2. Integration along a view ray

For a ray segment inside the atmosphere shell (`atmosphereSegment`):

1. The segment is cut into `bodies.atmosphere_samples` equal steps (default 16, 4 to 64), sampled at their midpoints.
2. At each sample the **in-scattered light** is
   Σ over lights of (σ_R p_R(μ) + σ_M p_M(μ, g)) · T_light · E_light, plus the multiple-scattering term (§3).
   The lights are the probe's brightest source and the `Light` components (`body_lighting.md`).
3. T_light is the **transmittance towards the light**: a 6-sample march from the sample to the top of the atmosphere
   (`atmosphereTransmittance`), and **zero when the planet is in the way**, so the night side and the shadow
   cone are dark.
4. The step is integrated **analytically**: with the source S of the sample and the extinction σ over a step ds, the
   contribution is T_view · S · (1 − e^{−σ ds}) / σ, and T_view is multiplied by e^{−σ ds} (Hillaire 2015, as cited by
   the 2020 paper, to avoid energy errors in thick steps).

A ray that crosses a cloud shell or the ring plane is cut there, and each part gets its own run of steps. The number
of samples is per part, not per ray.

Cost is the sample count × (1 + 6 · lights) density evaluations per part, for every ray that touches the outer shell.
There is no precomputed lookup table.

## 3. Multiple scattering

This is **not** Hillaire's multiple-scattering LUT (§5.5 of the paper, `m2_rendering_references.md` §3.3). It is a cruder estimate:

    S_ms = (σ_R + σ_M,s) · L̄

where L̄ is the **mean radiance of the sky around the body** without its brightest source, which is the SH band 0 of the
body's probe (`skyAmbient`, `body_lighting.md` §3). It means that the light scattered more than once is isotropic and
equals the mean incoming sky light, at every height. It is multiplied by the scattering coefficient of the sample but
not by the transmittance towards a light (the view-ray transmittance applies as for any source). In the Fixed lighting mode L̄ is a constant 0.03; in LightsOnly mode it is 0.

The estimate is cheap and keeps the night side and the shadowed haze from going black. It does not conserve
energy the way Hillaire's does, and it does not know about the ground colour: there is no light bounced from the
surface into the air.

## 4. Compositing into the tracer

The tracer needs the atmosphere in front of whatever lies behind it: the ground, the sky, the disk.

- **In a body's local frame** (`local_frames.md`) the layers are evaluated once along the exact straight patch ray, in
  RGB, and composited as add + transmit · (what is behind): the surface, or the traced background.
- **On ordinary traced rays** every straight chord of the integrator (and every segment of the hybrid tracer) is tested
  against the outer shell of every body (`segmentLayers`). The chord that ends at an object is composited in RGB. The
  chords before it are **folded** into the accumulator with a scalar alpha: the transmittance is reduced to its luminance
  (`foldLayers`, weights 0.2126, 0.7152, 0.0722), while the in-scattered light is added in colour. Only the segment
  that ends the ray keeps the colour of its transmittance.

The consequence is that a star seen through the limb of a planet, along a chord that does not end in an object, is
dimmed but not reddened. The in-scattered light of the limb itself has the right colour.

The Straight transport has none of this: it draws the Surface only.

## 5. Clouds

`CloudLayerComponent`: `Coverage` (a Data map, the red channel, same mapping as the Surface maps, white = overcast),
`Altitude` (default 4 km), `Opacity` (default 0.9, of full coverage), `Color`, `RotationPeriod` (relative to the
surface; 0 = turns with it).

- **Geometry.** A thin shell of radius R + `Altitude`. A ray that crosses it gets opacity = coverage · `Opacity` at each
  crossing (one entering, one leaving). With no coverage map the layer is uniform. The coverage is sampled with the
  ray-cone LOD of `textured_bodies.md` §7.
- **Lighting.** Diffuse: radiance = `Color`/π · E, with the irradiance E from each light times its atmospheric
  transmittance and max(n·L, 0), plus the sky ambient π L̄. **Seen from below** (the ray going outward through the shell)
  the colour is multiplied by **0.35**: a thick cloud lets a third of what falls on its top through. That factor is a
  project choice, not a measured value. The shell has no phase function, so there is no silver lining.
- **Shadows.** For direct light on the surface, a ray from the surface point towards each light is intersected with the
  shell. The light is multiplied by 1 − coverage · `Opacity` at the exit point (`surfaceLightFilter`). The atmosphere also
  dims it (transmittance with the ground at 0.999 of the radius).
- **Rotation.** The shell's orientation is the surface angle plus the cloud's own `RotationAngle`
  (`BodyRotationModule`, `textured_bodies.md` §2).

## 6. Rings

`RingComponent`: `Texture`, `InnerRadius`, `OuterRadius`, `Opacity`. Defaults are the C ring's inner edge and the A ring's outer edge
of Saturn, 74 658 km and 136 780 km (`interstellar_planets.md` §5).

- **Geometry.** A flat annulus in the sphere's equatorial plane (the plane through the centre perpendicular to the
  spin axis), with zero thickness.
- **Texture.** A **radial strip**: u runs from the inner to the outer radius, sampled at v = 0.5, colour in rgb
  (Color role) and coverage in alpha, scaled by `Opacity`. If no texture is set a pale default (0.8, 0.75, 0.7, alpha 0.8) is used.
- **Lighting.** Lit on **both faces** by |n·L| (light passes through the thin layer), plus the sky ambient. The planet
  shadows the ring: if the ray from the ring point to a light hits the sphere ahead of it, that light is skipped. The
  atmosphere does not dim the light on the ring.
- **Shadow on the planet.** The ray from a surface point to a light is intersected with the ring plane. If it crosses
  inside the ring, the light is multiplied by 1 − alpha at that radius.
- **Compositing.** The ring is one more layer in the front-to-back order, between the parts of the atmosphere on either
  side of the crossing.
- **LOD.** `ringSample` takes the mip level from the strip's own texel size, (outer − inner radius) / texture
  width, against the pixel footprint at the crossing. Not verified by a render.

## 7. Presets in the Interstellar template

`templates/interstellar-bodies.yaml` uses the project-choice presets of `interstellar_planets.md` §6, as multiples of
Earth's values (Hillaire 2020 Table 1):

| Planet | Rayleigh × Earth, H | Mie scattering / absorption × Earth, H, g | Ozone | Clouds |
|---|---|---|---|---|
| Miller | 1, 6.2 km | 3 / 0.3, 1.5 km, 0.75 | none | 2 km altitude, opacity 0.85 |
| Mann | 0.6, 10 km | 2 / 0.1, 3 km, 0.85 | none | 4 km, 0.9 |
| Edmunds | 0.3, 8 km | 0.5 / 0.5, 1.2 km, 0.8 | none | 3 km, 0.8 |

Heights of the atmosphere: 80, 120 and 100 km. `templates/solar-system-bodies.yaml` gives Earth the exact Hillaire values
of the component defaults, ozone included. Clouds there are at 6 km, opacity 0.95.

The cloud properties are all project choices (`interstellar_planets.md` §6, §8). That note proposes a volumetric layer
with a Henyey–Greenstein phase function (g about 0.85); the thin shell above does not implement that.

## 8. Checked by renders

Observed during the implementation; not repeated for these notes.

- **Earth from orbit** (`templates/solar-system-bodies.yaml`): the day side is hazy blue, the terminator is reddened,
  the night side is dark and the day limb is blue.
- **15 km above the Andes** (local frame with the Terrain): a curved glowing limb and aerial haze.

## Limitations

- **Multiple scattering** is the isotropic estimate of §3, not Hillaire's LUT.
- **No ground bounce.** Hillaire's ground albedo (0.3) is not used.
- **Scalar folding** of intermediate segments (§4): colour of a background seen through an atmosphere is lost on those
  chords.
- **Clouds** are an infinitely thin shell with Lambert shading and a project-choice factor for the underside. No
  thickness, no phase function, no shadows between cloud and cloud.
- **Rings** have zero thickness, are lit by |n·L| without a phase function, and have no ring-on-ring shadow.
- **Refraction** and the curvature of light within the shell are not modelled: each chord is straight.
- **Cost.** The ray march is per ray and per body. Lower `bodies.atmosphere_samples` for large scenes.
- **Gas giants.** Saturn has no atmosphere in the template; a view from space with no ground is not modelled
  (`interstellar_planets.md` §6).

---

## Sources

- Hillaire, S. (2020). *A Scalable and Production Ready Sky and Atmosphere Rendering Technique.* Computer Graphics Forum 39(4), 13–22 (EGSR 2020). doi:10.1111/cgf.14050. §4 and Table 1 (full text read; see `interstellar_planets.md` §6); the multiple-scattering LUT of §5.5 is *not* implemented.
- Hillaire, S. (2015). *Physically Based and Unified Volumetric Rendering in Frostbite.* SIGGRAPH Courses. Cited by the 2020 paper for the analytic integration over a step; not read.
- Bruneton, E. & Neyret, F. (2008). *Precomputed Atmospheric Scattering.* Computer Graphics Forum 27(4), 1079–1086. doi:10.1111/j.1467-8659.2008.01245.x. Not read here.
- Cornette, W. M. & Shanks, J. G. (1992). *Physical reasons for the phase function of Mie scattering.* Applied Optics 31(16), 3152–3160. Not read; the form is taken from Hillaire.
- NASA Space Science Data Coordinated Archive. *Saturnian Rings Fact Sheet.* nssdc.gsfc.nasa.gov/planetary/factsheet/. The ring radii.
- Presets and unverified items: `interstellar_planets.md` §6 and §8. Reference formulas: `m2_rendering_references.md` §3.

*The code statements were read in the files listed at the top. The render results of §8 were reported during the
implementation.*
