# Metric-aware rendering: horizon, disk and redshift

These notes explain how the integrated render path (`transport.mode = Integrated`, or
`--physically-accurate` on export) derives the event horizon, the accretion disk and the disk's colour from the metric. They also list the
literature behind each part.

Code: `shaders/kerr.glsl`, `shaders/ray_tracing.glsl` (`rk4RayMarching`), `shaders/disk.glsl`,
`src/rendering/metric.cppm`, `src/rendering/render_data.cppm` (`FillMetricData`, called from
`src/rendering/render_pipeline.cpp`). The integration uses the `real` type from `shaders/real.glsl`, so
`transport.gpu_precision = f64` runs it in double precision.

Units are geometrized (G = c = 1). Lengths are in units of the mass M. The `Spin` and `Charge` fields of a
black hole are the dimensionless values a/M and Q/M.

---

## 1. One metric family: Kerr–Newman

The four selectable metrics are all special cases of the Kerr–Newman metric in Boyer–Lindquist (BL)
coordinates (t, r, θ, φ):

| Metric | a | Q |
|---|---|---|
| Schwarzschild | 0 | 0 |
| Kerr | a | 0 |
| Reissner–Nordström | 0 | Q |
| Kerr–Newman | a | Q |

With Σ = r² + a² cos²θ and Δ = r² − 2Mr + a² + Q², every part of the renderer evaluates the same
closed-form expressions. The host zeroes `a` and/or `Q` according to the selected metric, so the shader
has no per-metric branches.

- Schwarzschild, K. (1916). *Über das Gravitationsfeld eines Massenpunktes nach der Einsteinschen Theorie.* Sitzungsber. Preuss. Akad. Wiss., 189–196.
- Reissner, H. (1916). Ann. Phys. 355, 106; Nordström, G. (1918). Proc. KNAW 20, 1238.
- Kerr, R. P. (1963). *Gravitational field of a spinning mass as an example of algebraically special metrics.* Phys. Rev. Lett. 11, 237.
- Newman, E. T. et al. (1965). *Metric of a rotating, charged mass.* J. Math. Phys. 6, 918.
- Boyer, R. H. & Lindquist, R. W. (1967). *Maximal analytic extension of the Kerr metric.* J. Math. Phys. 8, 265.
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation*, ch. 33.

## 2. Event horizon

The outer horizon is the larger root of Δ = 0:

    r₊ = M + √(M² − a² − Q²)

A ray is captured when it gets within 0.01 M of r₊. BL coordinates are singular exactly at r₊, so the
integration never crosses it. The step size shrinks in proportion to r − r₊, so a ray approaches the
horizon geometrically.

If a² + Q² > M² there is no horizon: the result is a naked singularity, which is not a black hole. The host
therefore clamps √(a² + Q²) ≤ 0.998 M. The value 0.998 is Thorne's limit for the spin that accretion can
actually produce.

- Thorne, K. S. (1974). *Disk-accretion onto a black hole. II. Evolution of the hole.* ApJ 191, 507.

**Shadow.** The shadow is not drawn explicitly. It is the set of camera rays that end on the horizon. For
Schwarzschild its edge lies at impact parameter √27 M ≈ 5.196 M. For Kerr it becomes flattened and shifted
on the prograde side (the "D" shape), because co-rotating photon orbits can get closer to the hole.

- Synge, J. L. (1966). *The escape of photons from gravitationally intense stars.* MNRAS 131, 463.
- Bardeen, J. M. (1973). *Timelike and null geodesics in the Kerr metric.* In *Black Holes (Les Houches)*, 215–239.

**Ergosphere** (debug view "Ergosphere & ISCO", tinted purple). Inside the static limit

    r_E(θ) = M + √(M² − a² cos²θ − Q²)

nothing can stay at rest relative to the distant stars, because frame dragging forces it to co-rotate.

## 3. Geodesic integration (Hamiltonian, Carter form)

Light follows null geodesics. Instead of the second-order geodesic equation with numerically
differentiated Christoffel symbols, which the old code used, the renderer integrates Hamilton's equations
for the momentum p_μ:

    H = ½ g^{μν} p_μ p_ν = K / (2Σ)
    K = Δ p_r² + p_θ² + (L / sinθ − a E sinθ)² − ((r² + a²) E − a L)² / Δ

- **Conserved quantities.** The metric is stationary and axisymmetric, so E = −p_t and L = p_φ are exact
  constants of motion. Only r, θ, p_r and p_θ (plus φ) are integrated, with RK4.
- **Exact derivatives.** All derivatives of K are analytic. The old float32 finite differences lost around
  10 % precision to cancellation.
- **Null condition.** H = 0 is set exactly at launch and afterwards only drifts by integration error. Debug
  view "Geodesic constraint drift" shows max |K| / (Σ E²) along each ray on a log scale.
- **Pole crossing.** Near the polar axis dφ/dλ ∝ L / sin²θ, which is a coordinate singularity, not a
  physical one. The step is limited so that θ and φ change by at most 0.02 rad per step. Without this limit,
  rays passing over the pole leave a visible seam.
- **Horizon.** Close to the horizon Δ → 0 and the drift grows inside the shadow. This is harmless because
  those rays are captured anyway.

K is the form in which Carter showed that the Hamilton–Jacobi equation of Kerr–Newman separates. That
separation is the reason the derivatives are short.

- Carter, B. (1968). *Global structure of the Kerr family of gravitational fields.* Phys. Rev. 174, 1559.
- Chan, C., Psaltis, D. & Özel, F. (2013). *GRay: a massively parallel GPU-based code for ray tracing in relativistic spacetimes.* ApJ 777, 13.
- Pu, H.-Y. et al. (2016). *Odyssey: a public GPU-based code for general relativistic radiative transfer in Kerr spacetime.* ApJ 820, 105.

## 4. Camera: backwards tracing from a ZAMO

Outside 50 M (25 r_s) rays travel in straight lines, and integration starts where a ray enters that sphere.
The deflection out there is only a few percent. From there, rays are traced **backwards in time**. The photon that reaches the camera moves along
`−rayDirection`, and it is integrated with a negative affine step. Simply reversing its direction would be
wrong when a ≠ 0, because time reversal of Kerr flips the sign of the spin.

The camera is a zero-angular-momentum observer (ZAMO). This observer co-rotates with the local frame
dragging and is defined everywhere outside the horizon, unlike a static observer, which cannot exist inside
the ergosphere. Its lapse α and angular velocity ω are:

    α = √(ΣΔ / A)
    ω = a (2Mr − Q²) / A
    A = (r² + a²)² − Δ a² sin²θ

A pixel direction n̂ with components (n_r, n_θ, n_φ) in the ZAMO frame gives:

    E   = α + ω L
    L   = √g_φφ · n_φ
    p_r = √(Σ/Δ) · n_r
    p_θ = √Σ · n_θ

This momentum is null by construction and already includes relativistic aberration. When integration
starts on the 50 M sphere instead of at the camera, frequencies are referred back to the camera's own ZAMO
with E_cam = (E − ω_cam L) / α_cam.

Camera and exit positions are converted between Cartesian and BL coordinates with the oblate-spheroidal
map:

    x = √(r² + a²) sinθ cosφ
    y = √(r² + a²) sinθ sinφ
    z = r cosθ

The z axis is the hole's `SpinAxis`. The whole computation happens in that rotated frame, which is why
tilting the spin axis rotates the image rigidly.

- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). *Rotating black holes: locally nonrotating frames, energy extraction, and scalar synchrotron radiation.* ApJ 178, 347.
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001.

## 5. Accretion disk geometry: the ISCO

The gas orbits on prograde circular equatorial geodesics. Their angular velocity, measured as dφ/dt, is:

    Ω = (−∂_r g_tφ + √((∂_r g_tφ)² − ∂_r g_tt ∂_r g_φφ)) / ∂_r g_φφ

For Kerr this reduces to Ω = 1 / (r^{3/2}/√M + a).

The disk's inner edge is the innermost stable circular orbit (ISCO), the minimum of the orbital energy E(r).
Inside it the gas plunges into the hole within a few orbits, so it emits little. The ISCO is:

| Spin | ISCO |
|---|---|
| a = 0 | 6 M |
| a = 0.9 M | 2.32 M |
| a = 0.998 M | 1.24 M |
| extremal Reissner–Nordström | 4 M |

`ProgradeIscoRadius` minimises E(r) numerically, so it also handles charge. For Kerr the unit tests check it
against the Bardeen–Press–Teukolsky closed form.

The outer edge is fixed at 20 M (`kDiskOuterRadiusM` in `render_data.cppm`). It is a free, artistic
parameter, because nothing in the metric sets it.

- Bardeen, Press & Teukolsky (1972), see §4. The ISCO formula is their eq. 2.21.
- Pugliese, D., Quevedo, H. & Ruffini, R. (2013). *Equatorial circular orbits of neutral test particles in the Kerr–Newman spacetime.* Phys. Rev. D 88, 024042.

## 6. Disk temperature

The disk uses the relativistic Page–Thorne flux, computed on the CPU from the same E(r), L(r) and Ω(r) as the
ISCO, with a physical temperature scale from the hole's mass and accretion rate. Details are in
`docs/notes/disk_rendering.md`.

- Shakura, N. I. & Sunyaev, R. A. (1973). *Black holes in binary systems. Observational appearance.* A&A 24, 337.
- Novikov, I. D. & Thorne, K. S. (1973). *Astrophysics of black holes.* In *Black Holes (Les Houches)*, 343–450.
- Page, D. N. & Thorne, K. S. (1974). *Disk-accretion onto a black hole. I. Time-averaged structure of accretion disk.* ApJ 191, 499.

## 7. Redshift, Doppler beaming and frame dragging

The frequency ratio between camera and gas comes from one formula, which combines gravitational redshift,
Doppler shift and frame dragging:

    g = ν_obs / ν_em = (p·u_cam) / (p·u_em) = 1 / (uᵗ (E − Ω L))
    uᵗ = 1 / √(−(g_tt + 2 g_tφ Ω + g_φφ Ω²))

The camera ZAMO normalises the photon energy to 1. The purely gravitational part is the lapse ratio
α_em / α_cam, which lets the "Gravitational Redshift" and "Doppler Beaming" toggles switch the two factors
independently.

The shift is applied in two places:

- **Colour.** The blackbody LUT is sampled at temperature g · T_em.
- **Brightness.** Because I_ν / ν³ is invariant along a ray (Liouville), bolometric intensity scales as g⁴.
  This makes the approaching side of the disk brighter and bluer, and the receding side dimmer and redder.

The analytic-lens tracer has no geodesic momentum. It takes the photon's angular momentum from its local
straight-line direction and then applies the same formula, frame dragging included (`analyticLensDiskG` in
`disk.glsl`). The flat tracer keeps its flat-space approximation (`doppler.glsl`).

- Cunningham, C. T. (1975). *The effects of redshifts and focusing on the spectrum of an accretion disk around a Kerr black hole.* ApJ 202, 788.
- Luminet, J.-P. (1979). *Image of a spherical black hole with thin accretion disk.* A&A 75, 228.
- Misner, Thorne & Wheeler (1973), §22.6, for the invariance of I_ν / ν³.
- Rybicki, G. B. & Lightman, A. P. (1979). *Radiative Processes in Astrophysics*, ch. 4 (relativistic beaming).

## 8. Volumetric absorption

Absorption through the gas needs a proper length, not an affine-parameter step. The length travelled
through the gas in its rest frame is:

    ds = (−p·u_em) dλ = dλ / g

The full radiative transfer through the disk is described in `docs/notes/disk_rendering.md`.

## Limitations

- **One hole only.** The accurate path uses only the first black hole. Multiple holes need a superposed or
  numerical metric.
- **Horizon crossing.** BL coordinates cannot follow rays through the horizon. Kerr–Schild coordinates
  would be needed for that.
- **Charge is purely gravitational.** Q only affects the gas through gravity. Charged-particle (Lorentz
  force) motion is ignored.

---

*The references were compiled from memory. Check volume and page numbers against the publications before
citing them in the paper.*
