# Near-extremal black holes: beyond the Thorne limit

By default every black hole's spin is clamped to a/M ≤ 0.998, the Thorne limit. A black hole can instead be
marked **near-extremal**: its spin is then a/M = 1 − δ with δ as small as 10⁻¹⁵. This is the regime of
*Interstellar*'s Gargantua. These notes explain why the option exists, what changes physically near a = M,
and what had to change numerically to keep the integration accurate there.

Code:
- `BlackHoleSpinRange`, `SpinGap` and `BlackHoleMetric` in `src/simulation/components.cppm`
- `kExtremalHorizonParameter`, `HorizonDiscriminant`, `HorizonDelta` and `EvaluateKerrCircularOrbit` in
  `src/rendering/metric.cppm`
- `HorizonDelta` and the capture margin in `src/physics/geodesic.cppm`

Tests: `tests/unit/physics/near_extremal_tests.cpp`.

---

## 1. Why the option exists: Miller's planet

The film's central time-dilation scene puts Miller's planet on an orbit where 1 hour equals 7 years on Earth,
a factor of about 61 000. A stable circular orbit slows a clock most at the ISCO, where dt/dτ = uᵗ:

| a/M | ISCO | dt/dτ at the ISCO | 1 h on the orbit equals |
|---|---|---|---|
| 0.998 (Thorne limit, default) | 1.237 M | 10.8 | 10.8 h |
| 1 − 10⁻⁸ | 1.003 M | 674 | 28 days |
| 1 − 1.3 × 10⁻¹⁴ | 1.000037 M | 61 900 | 7.06 years |

Only a hole within about 10⁻¹⁴ of extremal produces the film's factor. Thorne (2014, ch. 9) states that
Gargantua must spin "less than the maximum by about one part in 100 trillion". The value 1.3 × 10⁻¹⁴, the
default `SpinGap` of a near-extremal hole, reproduces 7.06 years per hour. Its exact provenance in the book is
not verified; see §5.

Such a spin is physically extreme. Disk accretion cannot spin a hole beyond about 0.998, because the hole
swallows radiation from the disk that counteracts the spin-up (Thorne 1974). The default therefore stays at
the Thorne limit, and near-extremal holes are an explicit choice.

The film itself also did not *render* Gargantua near-extremal. The visual effects team slowed the spin to
a/M = 0.6 for the images, because the flattened shadow edge and the off-centre disk of a near-extremal hole
"would be too confusing for a mass audience". The dynamics, including Miller's time dilation, assume the
near-extremal hole (James et al. 2015, §1.2, §4.1.1, Fig. 15, App. A.3.1).

## 2. The throat: what changes near a = M

With a = M(1 − δ), every characteristic radius moves to r → M in Boyer–Lindquist coordinates
(BPT 1972, eq. 2.22):

    r+   ≈ M [1 + (2δ)^{1/2}]
    r_ph ≈ M [1 + 2 (2δ/3)^{1/2}]
    r_mb ≈ M [1 + 2 δ^{1/2}]
    r_ms ≈ M [1 + (4δ)^{1/3}]

- **Reading of the r_ph expansion.** It follows directly from the exact eq. 2.18 with
  arccos(−a/M) ≈ π − (2δ)^{1/2}. The OCR of the scanned paper drops the inner factor 2, so check the page
  image before quoting it.
- **The orbits are still distinct.** They crowd together in r, but the proper radial distances between them
  stay finite or even diverge as δ → 0. The region near r = M is a long "throat" (BPT 1972 §II and Fig. 2;
  Bardeen & Horowitz 1999).
- **ISCO speed.** Gas on the ISCO moves at c/2 relative to the ZAMO as a → M, not at c (BPT 1972 §III). The
  large slowdown comes from the lapse α → 0, not from the orbital speed. The tests check both the c/2 and the
  agreement of α/γ with uᵗ.
- **Imaging.** The images of sources near the ISCO of a near-extremal hole have characteristic signatures,
  for example the "NHEK line" in the shadow (Gralla, Lupsasca & Strominger 2018).

## 3. Numerics: keeping precision as δ → 0

Near a = M the usual formulas cancel catastrophically. For δ = 1.3 × 10⁻¹⁴, M² − a² ≈ 2.6 × 10⁻¹⁴ is computed
from terms of size 1, so the expanded form keeps only two or three significant digits in double precision.
These are the changes:

| Quantity | Expanded form (cancels) | Form used |
|---|---|---|
| M² − a² − Q² | `m*m - a*a - q*q` | `(m - a)(m + a) - q*q` (`HorizonDiscriminant`) |
| Δ = r² − 2Mr + a² + Q² | expanded | (r − r₊)(r − r₋) (`HorizonDelta`, CPU and integrator) |
| Circular orbits (Kerr) | through −(g_tt + 2g_tφΩ + g_φφΩ²), whose terms are O(1) while the result is ~10⁻¹⁰ | BPT closed forms (eqs. 2.12–2.16) in x = √(r/M), with x³ − 3x + 2â = (x − 1)²(x + 2) − 2(1 − â) and similar factorings, where x − 1 = (r − M)/(M(x + 1)) and 1 − â are computed directly |
| ISCO, photon orbit, marginally bound (Kerr) | numeric minimisation or bisection | BPT eqs. 2.21, 2.18, 2.19 with 1 − â² = (1 − â)(1 + â) |
| Capture margin | r₊ + 10⁻³ M | r₊ + 10⁻³ · max(√(M² − a² − Q²), 10⁻⁶ M). This stays 10⁻³ M for Schwarzschild and shrinks to 10⁻⁹ M for Gargantua, below its ISCO at 3.7 × 10⁻⁵ M above the horizon. |

- **Charged holes.** These have no closed forms, so they keep the numeric path through the equatorial metric.
  A test checks that both paths agree for Kerr (10⁻⁹ relative) by switching on a negligible charge.
- **Representable spins.** The spacing of doubles near 1 is 1.1 × 10⁻¹⁶. 1 − 1.3 × 10⁻¹⁴ is therefore stored
  with δ accurate to about 0.4 %, and δ < 10⁻¹⁵ would be meaningless. That is the floor `kMinExtremalGap`.
- **Unchanged default rendering.** For holes inside the Thorne limit, the closed forms give the same float
  values on the GPU as the previous numeric path. An export of the committed `templates/sgr-a-star.yaml`
  (a = 0.5) is byte-identical before and after this change, with and without the disk.

## 4. Rendering a near-extremal hole

- **Float spin.** The GPU receives the spin as a float, and 1 − 1.3 × 10⁻¹⁴ rounds to 1. `FillMetricData`
  then clamps it to the extremal limit, which places the horizon at 1.0000002 M and the ISCO at about 1.00004 M.
  At the scale of an image this is indistinguishable from the exact hole.
- **The inner disk is not drawn.** The integrated tracer captures rays at r₊ + 0.01 M, so the disk between
  the ISCO and about 1.01 M is not drawn. The light from there is redshifted by up to ~6 × 10⁴ and carries no
  visible flux anyway.
- **The disk looks dimmer at the same exposure.** The disk shader scales brightness by (gT/T_peak)⁴, relative to
  the hottest gas in its rest frame (`diskSourceRadiance` in `disk.glsl`). A near-extremal disk is hottest deep in
  the throat (T_peak about 1.4× that of a Thorne-limit disk of the same mass and Eddington ratio), where its
  light is strongly redshifted.
  - At r ≥ 2 M both disks have nearly the same physical temperature (within 5 %; Page–Thorne computed
    independently, with η = 0.42 against 0.32).
  - On screen the visible near-extremal disk is still about (0.96/1.39)⁴ ≈ 0.2 as bright. This comes from the
    exposure convention, not from the physics: raise the exposure for near-extremal scenes.
- **The simulation is exact.** Bodies and observers integrate the double-precision spin, so Miller-like
  orbits at r ≈ 1.0001 M are integrated exactly. The test integrates five orbits there: r stays constant to
  10⁻⁶ of (r − M), and dτ/dt matches the closed form.

## 5. Sources

- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). *Rotating black holes: locally nonrotating frames, energy extraction, and scalar synchrotron radiation.* ApJ 178, 347. doi:10.1086/151796
  - Eqs. 2.12–2.16: circular orbits.
  - Eqs. 2.18, 2.19, 2.21: r_ph, r_mb, r_ms.
  - Eq. 2.22: near-extremal expansions.
  - §II and Fig. 2: the throat.
  - §III: ISCO speed → c/2.
- Thorne, K. S. (1974). *Disk-accretion onto a black hole. II. Evolution of the hole.* ApJ 191, 507. doi:10.1086/152991. The a/M ≈ 0.998 limit.
- Bardeen, J. M. & Horowitz, G. T. (1999). *The extreme Kerr throat geometry: a vacuum analog of AdS₂ × S².* Phys. Rev. D 60, 104030. doi:10.1103/PhysRevD.60.104030, arXiv:hep-th/9905099
- Gralla, S. E., Lupsasca, A. & Strominger, A. (2018). *Observational signature of high spin at the Event Horizon Telescope.* MNRAS 475, 3829. doi:10.1093/mnras/sty039, arXiv:1710.11112
- Thorne, K. S. (2014). *The Science of Interstellar.* W. W. Norton, New York. ISBN 978-0-393-35137-8.
  - Ch. 9: Gargantua spins "less than the maximum by about one part in 100 trillion".
  - The time-slowing discussion is cross-referenced to ch. 26.
  - Not verified: the exact value 1.3 × 10⁻¹⁴ (probably in that chapter's endnote) and the page of the
    "1 hour = 7 years" statement.
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001. doi:10.1088/0264-9381/32/6/065001, arXiv:1502.03808
  - §1.2: a/M ~ 1 is needed for the time losses; the paper's own runs use a/M = 0.999.
  - §4.1.1, Fig. 15a, App. A.3.1: the film rendered Gargantua at a/M = 0.6.
- Jacobson, T. (2011). *Where is the extremal Kerr ISCO?* arXiv:1107.5081. Discusses BPT's c/2 ISCO speed relative to the ZAMO.

*The BPT and James et al. statements were checked against the full texts. The equation numbers of BPT come
from an OCR'd scan, so compare them with the page images before citing.*
