# Interstellar's planets: physical parameters for Milestone M2

Milestone M2 adds the film's planets as rendered bodies with atmospheres, clouds and simple terrain. These notes
collect the numbers that exist for them, say where each number comes from, and propose defaults where nothing
is published. Every value carries one of these labels:

- **Verified**: seen by the author of this note in the text of the cited source.
- **Film dialogue**: stated in the film. Not checked against a script in this session; the figure is passed on
  by a verified secondary text where one is named.
- **Derived**: computed here; the derivation is shown.
- **Project choice**: the sources give no value; this is the default MoleHole uses.
- **Not verified**: recalled or reported by a pointer source (wiki, review) only. Do not quote it as fact.

Most of the film's planetary data lives in Thorne's book, whose full text was not available. Everything
attributed to it below is therefore either a secondary report (labelled so) or Not verified. The honest summary
is that **only Gargantua's mass scale, the time-dilation figure, the 130 % gravity and the Earth/Saturn
reference data could be checked**. Mann's and Edmunds' planets have almost no checkable numbers, so their
values are project choices.

Related notes: `near_extremal.md` (spin and the throat), `geodesic_bodies.md` (orbits of bodies),
`disk_rendering.md`.

---

## 1. Gargantua

| Quantity | Value | Label and source |
|---|---|---|
| Mass | 10⁸ M☉ ("100 million solar masses") | Verified, as a secondary text: Luminet (2015), §"Gargantua", gives M = 10⁸ M☉. Thorne (2014) gives "≳ 10⁸ M☉" (checked in an excerpt per `geodesic_bodies.md`; page not recorded). |
| GM/c² | 1.4767 × 10¹¹ m (0.987 AU) | Derived: 10⁸ × 1476.6 m. |
| GM/c³ | 492.6 s | Derived. |
| Horizon radius (a → M) | r₊ → M = 1.477 × 10¹¹ m | Derived (Kerr, a = M). |
| Spin for the movie's dynamics | a/M = 1 − δ, δ ≈ 1.3 × 10⁻¹⁴ | Thorne (2014) ch. 9: "less than the maximum by about one part in 100 trillion" (as quoted in `near_extremal.md`). Schnittman (2019), §3.3: "the spin must be at least a/M ≳ 1 − 10⁻¹⁴" (Verified). The 1.3 × 10⁻¹⁴ itself: Not verified in a primary text; see `near_extremal.md` §5. |
| Spin used for the film's images | a/M = 0.6 | Verified: James et al. (2015), §4.1.1 and Fig. 15a ("for visual purposes Nolan and Franklin slowed the spin to a/M = 0.6"). |
| Spin of the paper's own lensing runs | a/M = 0.999 | Verified: James et al. (2015), §1.2, §3.2, Figs. 4 to 13. |
| Natural spin limit | a/M ≈ 0.998 | Verified: James et al. (2015) ref. [31]; Thorne (1974). |
| Disk (movie model) | Thin, marginally optically thick, equatorial, not accreting, "cooled to a position-independent temperature T = 4500 K", emitting a black-body spectrum | Verified: James et al. (2015), §4 (description of the artists' disk, Fig. 14). It is an artist's construction, not a solution of the thin-disk equations. |
| Disk radii in that paper | Paint-swatch disk 9.26 M to 18.70 M (Fig. 13 inset) | Verified. This is a pedagogical disk, not the film's disk. |
| Disk Doppler speeds | about 0.55 c, gravitational redshift about 20 % | Verified: James et al. (2015), §4.1.2, for the a/M = 0.6 disk. |

Rendering note: the film omitted the Doppler shifts and the brightness shift. MoleHole's physical disk
(Page–Thorne, `disk_rendering.md`) is the project's default; a "movie-like" preset would be a uniform
T = 4500 K blackbody between the ISCO and an outer radius, with the Doppler terms optionally off. That preset
is a **Project choice**; the outer radius is not stated in the checked sources.

Scale check: the orbital coordinate-time period of a circular equatorial prograde orbit is
T = 2π (r^{3/2} + a) M, so at r ≈ M, a ≈ M, T ≈ 4π M = 6190 s = 1.72 h (**Derived**).

## 2. Miller's planet

| Quantity | Value | Label and source |
|---|---|---|
| Orbit | Circular, equatorial, prograde, just outside the ISCO of the near-extremal hole, at r − M ≈ 4 × 10⁻⁵ M | Schnittman (2019), §3.3: Miller's planet orbits "at radius r/M = 4 × 10⁻⁵" (read as a distance outside the horizon) and "no more than 4 × 10⁻⁵ gravitational radii outside the horizon" for the dilation of about 60 000 (Verified). Matches the ISCO of 1.000037 M in `near_extremal.md`. |
| Orbital speed | about c/2 relative to a local ZAMO | Derived/Verified in `near_extremal.md` §2 (BPT 1972 §III). Luminet (2015) says only "close to the speed of light" (looser). |
| Coordinate orbital period | about 1.72 h (6190 s) | Derived, see §1. |
| Time dilation | 1 hour on the planet = 7 years elsewhere, factor of about 6 × 10⁴ | Film dialogue; Verified in Schnittman (2019), §3.3, and Luminet (2015), "A huge time dilation" ("a time dilation factor of 60,000"). Thorne (2014) chapters: Not verified (page). |
| Surface gravity | 1.3 g⊕ = 12.75 m/s² | Film dialogue; Verified in Luminet (2015), §"Gargantua" ("we are told that the acceleration due to gravity on the planet is 130 % that of Earth's, which means g = 9.81 × 1.30 = 12.75 m/s²"). The script itself was not checked. |
| Radius | not stated | Project choice: 6371 km (1.0 R⊕). With g = 12.75 m/s² this gives M = g R²/G = 7.75 × 10²⁴ kg = 1.30 M⊕ and a mean density of 7160 kg/m³ (1.30 times Earth's), which is iron-rich but not absurd (**Derived**, GM from g R²/G, ρ = 3g/(4πGR)). |
| Rotation | Tidally locked to Gargantua, with the water bulge fixed in the planet frame; Thorne explains the hourly waves by the planet "rocking" | Luminet (2015), "Gargantua": a tidal-locking time of about 1 ms for M = 10⁸ M☉ and g ≈ 13 m/s² (Verified, secondary). Schnittman (2019), §2: planets inside the tidal-locking radius are tidally locked, and a terrestrial planet is "tidally disrupted just inside the horizon of Gargantua, making Miller's planet deformed but not disrupted (Thorne 2014)" (Verified). The rocking mechanism is attributed to Thorne by a wiki pointer only: Not verified. |
| Tidal deformation | Large; Roche limit R_R ~ (M/ρ)^{1/3} ~ 10¹³ cm, the same as GM/c² for ρ ~ 1 g/cm³ | Verified: Luminet (2015), "Gargantua" (order of magnitude only). |
| Ocean depth | "Knee-deep" (about 0.3 to 0.5 m) | Film visuals (the crew wades). No number in any source; Project choice 0.4 m. |
| Wave height | Skyscraper-size, about 1.2 km (4000 ft) | Pointer sources only (wiki and FAQ): Not verified. Luminet (2015) says "kilometer high waves" about the film (Verified as his reading of the film). Project choice: mean wave height 1 km at crest, wavelength much greater than the depth (Luminet: shallow-water regime). |
| Wave period | "every hour or so" local time | Verified as reported by Luminet (2015). |
| Atmosphere and clouds | Breathable, Earth-like; mostly clear, thin high cloud, no land. The film's sky is overcast grey | Film visuals. No composition is given in the checked sources: Not verified. |
| Surface light | Lit only by the hole's disk, the surrounding stars and (in the story) a dim sky; Schnittman notes the planet is embedded in an intense relativistically beamed radiation field | Verified as discussion: Schnittman (2019), §3. Not a rendering requirement. |

Two cautions for the simulation:

1. **Locked rotation is not simple here.** In the planet's own proper time the coordinate period of 1.72 h is
   about 6190 s / 61 900 = 0.1 s. A planet that is "tidally locked" on this orbit rotates in the local
   inertial frame at the rate the frame-dragging and orbital motion impose, and that rate cannot be treated
   as a Newtonian spin. The rendering default is therefore a body whose rotation is fixed in the frame of the
   hole's tidal bulge (a Project choice), and the "1 hour = 7 years" read-out comes from the observer clock
   rather than from the spin.
2. **Wave height is a design target, not a derived value.** Neither Luminet nor Schnittman solves the tidal
   response, and Luminet states that "there are too many uncertainties" to do so.

## 3. Mann's planet

Almost nothing is Verified here. The film shows an ice world with frozen clouds, a bright cold sky,
dense, white cloud decks and a rough frozen surface; the crew cannot breathe the atmosphere.

| Quantity | Value | Label and source |
|---|---|---|
| Orbit | Far from Gargantua, on an elliptical orbit (the film uses a slingshot from Miller's region to reach it) | Not verified. The Science of Interstellar has a "Mann's planet" chapter (table of contents seen in a catalogue listing); its content could not be read. |
| Surface gravity | recalled as about 0.8 g | Not verified. |
| Surface | Ice, with "frozen clouds" (vertical ice formations) | Film visuals; no number. |
| Atmosphere | Toxic; ammonia is mentioned in the film | Not verified. |
| Temperature | very cold | Not verified; no figure. |
| Radius | not stated | Project choice: 0.8 R⊕ = 5097 km, which gives g close to 0.8 g⊕ at about Earth's mean density (g ∝ ρR; Derived). |
| Rotation | not stated | Project choice: 24 h. |

## 4. Edmunds' planet

| Quantity | Value | Label and source |
|---|---|---|
| Orbit | Not stated in anything checked | Not verified. |
| Habitability | The candidate colony world; seen only briefly at the end of the film | Not verified. |
| Surface, atmosphere, gravity | Rocky, arid, with a thin atmosphere; no stated numbers | Not verified. |
| Radius, gravity, rotation | Not stated | Project choice: 1.0 R⊕, 1.0 g⊕ (9.81 m/s²), 24 h. |

## 5. Earth and Saturn

The film's opening act and the Saturn flyby need these. All values are from the NASA Planetary Fact Sheets
(Verified, read from nssdc.gsfc.nasa.gov on the date of this note).

| Quantity | Earth | Saturn |
|---|---|---|
| Mass | 5.9722 × 10²⁴ kg | 568.32 × 10²⁴ kg |
| Equatorial radius | 6378.137 km | 60 268 km (1 bar level) |
| Polar radius | 6356.752 km | 54 364 km |
| Volumetric mean radius | 6371.0 km | 58 232 km |
| Flattening (ellipticity) | 0.00335 | 0.09796 |
| Surface gravity (mean) | 9.820 m/s² | 11.19 m/s² (1 bar) |
| Sidereal rotation period | 23.9345 h | 10.656 h (NASA footnotes this as a magnetic-field/radio value; the cloud tops rotate at different rates) |
| Obliquity to orbit | 23.44° | 26.73° |

Saturn's rings (NASA Saturnian Rings Fact Sheet; ratio to the equatorial radius 60 268 km):

| Ring | Inner edge (km) | Ratio | Outer edge (km) | Ratio |
|---|---|---|---|---|
| D | 66 900 | 1.110 | 74 510 | 1.236 |
| C | 74 658 | 1.239 | 91 975 | 1.526 |
| B | 91 975 | 1.526 | 117 507 | 1.950 |
| Cassini division | 117 507 | 1.950 | 122 340 | 2.030 |
| A | 122 340 | 2.030 | 136 780 | 2.270 |

Optical depth (from the same sheet): C 0.05 to 0.35, B 0.4 to 2.5, Cassini division 0 to 0.1, A 0.4 to 1.0.
Albedo: C 0.12 to 0.30, B 0.4 to 0.6, A 0.4 to 0.6. Ring thickness is metres to tens of metres.

## 6. Atmosphere defaults

The atmospheric model is the single-scattering Rayleigh + Mie + ozone model of Bruneton & Neyret (2008),
in the real-time form of Hillaire (2020). Hillaire's Table 1 was read in the paper's text; it cites
Bruneton's 2017 implementation ([Bru17a]) for the coefficients.

| Parameter | Value | Label and source |
|---|---|---|
| Ground radius, Earth | 6360 km | Verified: Hillaire (2020), §4. |
| Top radius, Earth | 6460 km (a 100 km layer) | Verified: Hillaire (2020), §4. |
| Ground albedo | 0.3 | Verified: Hillaire (2020), §4. |
| Rayleigh scattering σ_s^r | (5.802, 13.558, 33.1) × 10⁻⁶ m⁻¹ | Verified: Hillaire (2020), Table 1. |
| Rayleigh wavelengths | 680, 550, 440 nm | Bruneton & Neyret (2008) convention; not restated in Hillaire's text. Check: σ ∝ λ⁻⁴ gives 13.55 and 33.09 from 5.802 at 680 nm (**Derived**), matching the table. |
| Rayleigh scale height | 8 km (density e^{−h/8 km}) | Verified: Hillaire (2020), §4. |
| Rayleigh phase | 3 (1 + cos²θ) / (16π) | Verified: Hillaire (2020), §4. |
| Mie scattering σ_s^m | 3.996 × 10⁻⁶ m⁻¹ (grey) | Verified: Hillaire (2020), Table 1. |
| Mie absorption σ_a^m | 4.40 × 10⁻⁶ m⁻¹ (grey) | Verified: Hillaire (2020), Table 1. Extinction is 8.396 × 10⁻⁶ m⁻¹ (Derived, the sum). |
| Mie scale height | 1.2 km | Verified: Hillaire (2020), §4. |
| Mie anisotropy g | 0.8 (Cornette–Shanks phase function) | Verified: Hillaire (2020), §4 ("By default, g = 0.8"). Bruneton & Neyret (2008) used a Cornette-type Henyey–Greenstein with g about 0.76 to 0.8; the value there: Not verified. |
| Ozone absorption σ_a^o | (0.650, 1.881, 0.085) × 10⁻⁶ m⁻¹ | Verified: Hillaire (2020), Table 1. |
| Ozone layer | Tent function of width 30 km centred at 25 km: d(h) = max(0, 1 − |h − 25 km| / 15 km) | Verified: Hillaire (2020), §4. |
| Bruneton & Neyret (2008) Mie coefficient | about 2 × 10⁻⁵ m⁻¹ scattering, extinction = scattering / 0.9 | Not verified here (recalled). Hillaire's table is used instead. |

Notes:

- The Rayleigh coefficients scale with the number density of the gas, so a thicker atmosphere is a linear
  multiplier on σ^r (Hillaire's own test of sunset uses 5× higher Rayleigh coefficients, Fig. 13).
- Scale heights scale as H = kT/(μ m_H g). For the Earth-like preset, H = 8 km at 9.8 m/s². The same gas at
  12.75 m/s² (Miller) gives 8 × 9.82 / 12.75 = 6.2 km, and at 0.8 g (Mann) 10 km (**Derived**, same T and μ).
- Presets below are **Project choices** built from the verified Earth values:

| Preset | Rayleigh (× Earth) | Rayleigh H | Mie σ_s / σ_a (× Earth) | Mie H | g | Ozone |
|---|---|---|---|---|---|---|
| Earth | 1 | 8 km | 1 / 1 | 1.2 km | 0.8 | yes |
| Miller (humid, hazy) | 1 | 6.2 km | 3 / 0.3 | 1.5 km | 0.75 | none |
| Mann (cold, ice haze) | 0.6 | 10 km | 2 / 0.1 | 3 km | 0.85 | none |
| Edmunds (thin, dry) | 0.3 | 8 km | 0.5 / 0.5 | 1.2 km | 0.8 | none |
| Saturn (view from space, no ground) | not modelled | | | | | |

Clouds: no cloud thickness or altitude is given by any source. Project choice for the first
version is a single layer, 1 to 2 km thick, base at 2 km for Miller and 4 km for Mann, with a Henyey–Greenstein
phase g about 0.85 and an extinction of about 0.05 m⁻¹ (liquid water clouds are about 0.01 to 0.1 m⁻¹; **not
verified**, textbook figure).

## 7. Summary of proposed defaults

| Body | Radius | Surface gravity | Rotation period | Atmosphere preset | Orbit |
|---|---|---|---|---|---|
| Gargantua | r₊ = M = 1.477 × 10¹¹ m (10⁸ M☉) | n/a | a/M = 1 − 1.3 × 10⁻¹⁴ | none | n/a |
| Miller's planet | 6371 km (Project choice) | 12.75 m/s² (1.3 g⊕, film) | tidally locked (fixed bulge); see §2 caution | Miller | circular, equatorial, prograde, r − M = 4 × 10⁻⁵ M, T ≈ 1.72 h coordinate |
| Mann's planet | 5097 km (Project choice) | about 0.8 g⊕ (Not verified) | 24 h (Project choice) | Mann | far elliptical (Not verified); no default numbers |
| Edmunds' planet | 6371 km (Project choice) | 9.81 m/s² (Project choice) | 24 h (Project choice) | Edmunds | not stated; Project choice |
| Earth | 6378.137 km equatorial, flattening 0.00335 | 9.82 m/s² | 23.9345 h, tilt 23.44° | Earth | n/a |
| Saturn | 60 268 km equatorial, flattening 0.09796 | 11.19 m/s² | 10.656 h, tilt 26.73° | none (ring set in §5) | n/a |

## 8. Still unverified

- Thorne (2014) page and chapter numbers for all of the following: Miller's planet figures, the rocking
  explanation of the waves, wave height, Mann's and Edmunds' orbits, gravity, atmosphere and temperature.
- The film script lines for 1.3 g (taken from Luminet's quotation) and for Mann's and Edmunds' gravity.
- The exact δ = 1.3 × 10⁻¹⁴ (see `near_extremal.md`).
- The outer radius and exact temperature profile of the film's disk.
- Bruneton & Neyret (2008) Mie coefficient and g.
- All cloud properties, Miller's ocean depth, and every radius except those of Earth and Saturn.

## 9. Sources

- Thorne, K. S. (2014). *The Science of Interstellar.* W. W. Norton, New York. ISBN 978-0-393-35137-8. Chapters on Gargantua's spin (ch. 9), Miller's planet and Mann's planet; full text not read. Pointers only: reviews and the Interstellar fan wiki.
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001. arXiv:1502.03808. Full text read: §1.2, §3.2, §4 (disk, T = 4500 K, Figs. 13 to 15), §4.1.1 and §4.1.2.
- Luminet, J.-P. (2015). *The Warped Science of Interstellar.* arXiv:1503.08305. Full text read: M = 10⁸ M☉, 130 % gravity, Roche limit, tidal locking, 60 000 time-dilation factor.
- Schnittman, J. D. (2019). *Life on Miller's Planet: The Habitable Zone Around Supermassive Black Holes.* arXiv:1910.00940. Full text read: orbital radius 4 × 10⁻⁵ M, time dilation, tidal locking and disruption.
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). ApJ 178, 347. doi:10.1086/151796 (see `near_extremal.md`).
- Bruneton, E. & Neyret, F. (2008). *Precomputed Atmospheric Scattering.* Computer Graphics Forum 27(4), 1079 to 1086. doi:10.1111/j.1467-8659.2008.01245.x. Not read in this session.
- Bruneton, E. (2017). *A Qualitative and Quantitative Evaluation of 8 Clear Sky Models* (IEEE TVCG) and the reference implementation at ebruneton.github.io/precomputed_atmospheric_scattering. Cited by Hillaire as [Bru17a, Bru17b]; the page checked held only texture constants.
- Hillaire, S. (2020). *A Scalable and Production Ready Sky and Atmosphere Rendering Technique.* Computer Graphics Forum 39(4), 13 to 22 (EGSR 2020). doi:10.1111/cgf.14050. Full text read: §4 and Table 1.
- NASA Space Science Data Coordinated Archive. *Earth Fact Sheet*, *Saturn Fact Sheet*, *Saturnian Rings Fact Sheet.* nssdc.gsfc.nasa.gov/planetary/factsheet/
