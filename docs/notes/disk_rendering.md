# Accretion disk rendering: physics and look

These notes explain how the disk is made visually appealing without giving up the physics. The metric
(Kerr–Newman family), the ISCO, the temperature profile, the gas motion and the frequency shift are
computed exactly. The look only enters through quantities the physics leaves open, and each of those is a
separate setting.

Code: `shaders/disk.glsl` (shared disk model), `shaders/ray_tracing.glsl` (thin and volumetric sampling in
both tracers), `src/rendering/metric.cppm` (Page–Thorne profile), `shaders/exposure.comp` and
`shaders/blackhole_display.frag` (HDR and tone mapping). The metric itself, the ISCO and the redshift
formula are covered in `docs/notes/metric_rendering.md`.

Units are geometrized (G = c = 1, lengths in M) unless SI is stated.

### What is physical and what is look

| Quantity | Source |
|---|---|
| Inner edge (ISCO) | metric, `ProgradeIscoRadius` |
| Temperature T(r) | Page–Thorne flux, mass, accretion rate (`disk.accretion_rate`) |
| Gas motion Ω(r) | circular geodesics of the metric |
| Colour shift and beaming | frequency ratio g from the metric, I ∝ T_obs⁴ |
| Outer edge (20 M), height, opacity | free parameters of the disk model |
| Turbulence, hot spots | look; they move with the physical Ω(r) |
| Display temperature, exposure, tone mapping | look; applied to every pixel alike |

With `disk.hot_spots = 0` and `disk.display_temperature = 0`, all of the disk's physics and colours are
physical; the turbulence only redistributes flux around its mean.

---

## 1. Temperature: the Page–Thorne disk

A geometrically thin, optically thick disk radiates the energy released as gas spirals inward. Page and
Thorne derived the flux emitted per unit area for an arbitrary stationary, axisymmetric metric. The only
boundary condition is that the torque vanishes at the ISCO:

    F(r) = Ṁ / (4π √−g) · (−Ω_,r) / (E − Ω L)² · ∫_{r_isco}^{r} (E − Ω L) L_,r dr

- **Orbit quantities.** E, L and Ω are the energy, angular momentum and angular velocity of circular
  equatorial orbits, the same functions `metric.cppm` uses for the ISCO. For Boyer–Lindquist coordinates
  of the Kerr–Newman family, √−g of the (t, r, φ) subspace at the equator is r.
- **Limits.** F is 0 at the ISCO, so the inner edge fades in instead of being cut. For Schwarzschild the
  peak lies near 9.5 M, and far out F approaches the Newtonian Shakura–Sunyaev form
  3/(8π r³)·(1 − √(r_in/r)). Both are unit tests.
- **Temperature.** T = (F/σ)^{1/4}.

The physical scale comes from the hole's mass and the accretion rate in Eddington units:

    L_Edd = 4π G M m_p c / σ_T,   η = 1 − E(r_isco),   Ṁ = ṁ · L_Edd / (η c²)
    F_SI(r) = Ṁ c⁶ / (G² M²) · F̃(r / M)

The radiative efficiency η comes from the metric: 5.7 % for Schwarzschild and about 32 % at a = 0.998. At a
fixed Eddington ratio, T scales as M^{−1/4}:

| Hole | Peak temperature | Appearance |
|---|---|---|
| Stellar mass (10 M☉) | about 10⁷ K | X-ray hot, blue-white |
| Supermassive | 10⁴–10⁵ K | UV/optical |

The CPU eva| luates F on 64 log-spaced radii per hole and sends T/T_peak plus T_peak to the shader
(`DiskInfo`, `DiskProfile`).

- Novikov, I. D. & Thorne, K. S. (1973). *Astrophysics of black holes.* In *Black Holes (Les Houches)*, 343–450.
- Page, D. N. & Thorne, K. S. (1974). *Disk-accretion onto a black hole. I. Time-averaged structure of accretion disk.* ApJ 191, 499.
- Shakura, N. I. & Sunyaev, R. A. (1973). *Black holes in binary systems. Observational appearance.* A&A 24, 337.
- Bardeen, J. M. (1970). *Kerr metric black holes.* Nature 226, 64 (efficiency 1 − E_isco).
- Thorne, K. S. (1974). *Disk-accretion onto a black hole. II. Evolution of the hole.* ApJ 191, 507.
- Frank, J., King, A. & Raine, D. (2002). *Accretion Power in Astrophysics*, 3rd ed., Cambridge University Press, ch. 1 and 5 (Eddington limit, disk temperature).

## 2. Colour and brightness of the gas

The source term is built in three steps:

- **Observed temperature.** The emitted temperature T is shifted by the frequency ratio g to T_obs = g·T.
  Here g is exact in the integrated tracer; see the metric notes.
- **Colour.** Taken from the blackbody LUT at T_obs. The LUT is a CIE 1931 integration of the Planck
  spectrum, and only its hue is used.
- **Brightness.** I_ν/ν³ is invariant along a ray, so the bolometric intensity scales as T_obs⁴, i.e.
  g⁴·T⁴. The approaching side is brighter and bluer, the receding side dimmer and redder.

A single calibration constant, `DISK_REFERENCE_LUMINANCE` = 0.25, sets the luminance of the hottest unshifted
gas. It acts like a fixed exposure, so relative brightness stays physical.

The analytic-lens tracer has no geodesic momentum. It takes the photon's angular momentum from its local
straight-line direction and applies the same g formula. Its disk starts no closer than 3 M, because its
Schwarzschild-like lens breaks down inside the photon sphere.

- Planck, M. (1901). *Ueber das Gesetz der Energieverteilung im Normalspectrum.* Ann. Phys. 309, 553.
- CIE (1932). *Commission Internationale de l'Éclairage Proceedings, 1931.* Cambridge University Press (colour matching functions).
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation*, §22.6 (I_ν/ν³ invariance).
- Cunningham, C. T. (1975). *The effects of redshifts and focusing on the spectrum of an accretion disk around a Kerr black hole.* ApJ 202, 788.

## 3. Display temperature: an honest colour exposure

A physical stellar-mass disk at 10⁷ K saturates the blackbody colour to blue-white. The warm, orange disk of
*Interstellar* was an artistic choice; that production also switched Doppler beaming off for the film.

`disk.display_temperature` multiplies **every** temperature by the same factor T_display / T_peak, and only
for the colour lookup:

- temperature ratios, the radial gradient and the redshift g stay physical;
- brightness still uses the physical ratios;
- 0 means physical colours.

The value is recorded in each export's `.render.yaml`, so a figure can state it.

- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001.

## 4. Gas motion: Keplerian shear with flow maps

The gas orbits at Ω(r) of the active metric (for Kerr, Ω = 1/(r^{3/2} + a)). `disk.speed` sets the angular
speed of the inner edge in rad/s of wall time. Everything further out follows the physical ratio Ω(r)/Ω_isco,
so inner rings overtake outer ones and features shear into trailing spirals.

Pure advection, with texture coordinate φ − Ω(r)·t, would wind the pattern up without limit. The
flow-map technique avoids this:

- **Two layers.** Two noise layers with clocks half a period apart are each reset while their weight is
  zero, and the weights are crossfaded with complementary triangle waves.
- **Bounded shear.** The period is a quarter orbit, so the shear never exceeds about 2.4 Δr/r.
- **Constant contrast.** The blend is variance-preserving, value / √(w₁² + w₂²), so contrast does not dip
  halfway through a cycle.
- **No repeats.** Each reset uses a new noise offset, so the pattern never repeats.

- Neyret, F. (2003). *Advected textures.* Proc. ACM SIGGRAPH/Eurographics Symposium on Computer Animation, 147–153.
- Vlachos, A. (2010). *Water flow in Portal 2.* SIGGRAPH 2010 Advances in Real-Time Rendering course.

## 5. Turbulence and hot spots

Real disks are turbulent; the magnetorotational instability drives the viscosity. In the renderer,
turbulence is a log-normal modulation m (mean about 1) of the dissipated flux:

- **Effect on the gas.** The local temperature becomes T·m^{1/4}, and the column density scales with m, so
  structure appears in colour, brightness and transparency at once.
- **Noise.** Simplex fBm (`disk.noise_scale`, `disk.noise_lod`, at most 5 octaves against aliasing), sampled
  in (log r, angle, z/H) and elongated along φ.

**Hot spots** (`disk.hot_spots`) are Gaussian clumps 35 % hotter per unit overlap. They sit on circular
geodesics at Ω(r_k), so their period and their flickering redshift are physical. Clumps like these are
observed orbiting near the ISCO of Sgr A*.

- Balbus, S. A. & Hawley, J. F. (1991). *A powerful local shear instability in weakly magnetized disks. I.* ApJ 376, 214.
- Broderick, A. E. & Loeb, A. (2006). *Imaging optically-thin hotspots near the black hole horizon of Sgr A\* at radio and near-infrared wavelengths.* MNRAS 367, 905. doi:10.1111/j.1365-2966.2006.10152.x
- GRAVITY Collaboration (2018). *Detection of orbital motions near the last stable circular orbit of the massive black hole SgrA\*.* A&A 618, L10.
- Gustavson, S. (2005). *Simplex noise demystified.* Linköping University technical note.

## 6. Disk structure: thin sheet or flared volume

Both modes share one rule: the optical depth straight through the disk is `disk.opacity` times the column
factor (turbulence and edges).

**Thin** (`disk.volumetric` off):
- Each geodesic step that crosses the equatorial plane is interpolated linearly in cos θ, and the disk is
  shaded exactly at that point.
- The optical depth along the ray is τ = opacity · column / μ. Here μ = |cos| of the angle between the ray
  and the disk normal, measured in the local ZAMO frame as p^(θ)/E_local. The gas frame would differ
  slightly; this approximation is documented.
- Every lensed image of the disk (primary, secondary, photon ring) is evaluated exactly, without aliasing
  from the step size.

**Flared volume** (`disk.volumetric` on):
- The density is a Gaussian in height with scale height H(r) = `disk.height`·r/10 M, i.e. a constant aspect
  ratio H/r, as in a gas-pressure-supported disk.
- Steps are capped at H/4 inside the slab.

**Edges** (both modes):
- **Inner.** The surface density goes to zero at the zero-torque ISCO.
- **Outer.** The disk fades out between 75 % and 100 % of the outer radius.

- Frank, King & Raine (2002), ch. 5 (vertical structure, H/r).
- Luminet, J.-P. (1979). *Image of a spherical black hole with thin accretion disk.* A&A 75, 228.

## 7. Radiative transfer

Both modes solve the transfer equation dI/ds = j − αI with a thermal source S = j/α. Over a segment of
optical depth Δτ, the formal solution for a constant source is:

    I ← I + T · S · (1 − e^{−Δτ}),   T ← T · e^{−Δτ}

Here T is the transmittance of everything in front of the segment.

- **Consistency.** Emission and absorption are consistent, and the result does not depend on the step size.
  The previous model added emission per step, which did depend on it.
- **Visible effects.** An optically thin disk lets the far side and the photon ring shine through. A thick
  one becomes a glowing surface.
- **Proper length.** In the integrated tracer the proper length through the gas is ds = dλ/g_launch; see
  the metric notes.
- **Carried transmittance.** The transmittance now also carries through the analytic-lens tracer, so the
  sky and objects behind translucent gas are dimmed correctly.

- Rybicki, G. B. & Lightman, A. P. (1979). *Radiative Processes in Astrophysics*, ch. 1 (formal solution).
- Fuerst, S. V. & Wu, K. (2004). *Radiation transfer of emission lines in curved space-time.* A&A 424, 733.
- Younsi, Z., Wu, K. & Fuerst, S. V. (2012). *General relativistic radiative transfer: formulation and emission from structured tori around black holes.* A&A 545, A13.

## 8. HDR, exposure and tone mapping

The tracer writes HDR radiance. The new **Exposure** stage writes one exposure value to a 1×1 image:

- **Manual:** 2^EV.
- **Auto:** key / log-average luminance, where the average only counts samples within 1/100 of the brightest
  one, so a frame that is mostly dark sky is metered on the disk.

Bloom and lens flare compare their thresholds against the exposed value; the flare uses a soft knee and
bilinear ghosts. The composite then:
1. adds bloom and flare in radiance units;
2. multiplies by the exposure;
3. tone-maps with Reinhard or the ACES filmic fit;
4. encodes sRGB, because the colour target is UNORM.

`post.tonemap = None` keeps the raw output from before HDR. Debug views always bypass tone mapping, so their
colours stay exact.

- Reinhard, E., Stark, M., Shirley, P. & Ferwerda, J. (2002). *Photographic tone reproduction for digital images.* ACM Trans. Graph. 21(3), 267.
- Narkowicz, K. (2015). *ACES filmic tone mapping curve.* Blog post (fit of the ACES RRT + ODT).
- Academy of Motion Picture Arts and Sciences. *Academy Color Encoding System (ACES).* Specification S-2014-004.
- IEC 61966-2-1:1999. *Multimedia systems and equipment – Colour measurement and management – Part 2-1: Default RGB colour space – sRGB.*

## Limitations and next steps

- **Plunging gas.** The faint emission from gas plunging inside the ISCO is not drawn; the surface density
  is zero there.
- **Thin-disk normal.** The thin disk measures μ in the ZAMO frame instead of the gas frame.
- **Multiple holes.** The integrated tracer shades the disk of the first hole only, like the rest of that
  path.
- **Temporal noise.** There is no blue-noise jitter or temporal accumulation yet. Thin-disk crossings do
  not alias, but volumetric noise at high `disk.noise_scale` can.

---

*References compiled from memory. Check volume and page numbers before citing them in the paper.*
