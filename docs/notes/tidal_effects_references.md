# Tidal effects: torque, spaghettification, Roche limit, debris and ocean tides

These notes cover pillar "2. Tidal forces" of milestone M3 in `docs/RELEASE_1_GOAL.md`. They assume the tidal
tensor E_ij of the observer or body frame is already known, and collect what is done *with* it: the torque on a
rigid body, the stretching of an extended body, the break-up criterion, the fate of the debris and the ocean
tide on Miller's planet.

Every claim carries one of these labels:

- **Verified**: checked against the paper text, abstract or Crossref record in this session.
- **Derived**: worked out here; the steps or a numeric check are shown.
- **Not verified**: recalled from memory or reported second-hand. Do not quote it as fact.

Numbers marked Derived were evaluated with CODATA-like constants (G = 6.6743e-11, c = 299 792 458 m/s,
M☉ = 1.98847e30 kg, g = 9.80665 m/s²).

Conventions follow `observers.md`: G = c = 1 where relativistic, SI otherwise. The tidal tensor is defined by
geodesic deviation in the body's local frame:

    d²ξⁱ/dτ² = −Eⁱⱼ ξʲ       E_ij = R_{îτ̂jτ̂}
    Schwarzschild, static observer (r̂, θ̂, φ̂):  E = diag(−2M/r³, M/r³, M/r³)
    Newtonian point mass:  E_ij = (GM/r³)(δ_ij − 3 r̂_i r̂_j),  r̂ from the primary to the body

The Newtonian form reproduces the Schwarzschild diagonal on the radial axis (1 − 3 = −2). E is symmetric and
trace-free in vacuum. For a radially infalling observer the diagonal is the same (Not verified here; it is a
standard result in the textbooks of §2).

Related notes: `observers.md` (frames), `interstellar_planets.md` §2 (Miller's planet), `near_extremal.md`.

---

## 1. Gravity-gradient torque on a rigid body

A body with density ρ in the tidal potential Φ = ½ E_ij xⁱ xʲ feels the force density −ρ E_ij xʲ. With the
second moment J_jl = ∫ρ x_j x_l dV and the inertia tensor I_jl = tr(J) δ_jl − J_jl, the torque about the
centre of mass is

    τ_i = ε_ijk ∫ρ x_j (−E_kl x_l) dV = −ε_ijk E_kl J_jl = ε_ijk (I E)_jk = −ε_ijk E_jl I_lk

The trace part of J drops out because ε_ijk E_kj = 0 (E symmetric). Check against the Newtonian result: with
E_ij = (GM/r³)(δ_ij − 3 r̂_i r̂_j),

    τ = (3GM/r³) r̂ × (I r̂)

which is the textbook gravity-gradient torque with r̂ from the primary to the body (**Derived**; the sign and
index placement of τ_i = −ε_ijk E_jl I_lk are the same as the expression above). Consequences, all standard:

- The torque vanishes when r̂ is a principal axis of I. The stable alignment puts the axis of *smallest*
  moment along r̂, so a body elongated along r̂ is stable (and a tidally locked moon points its long axis
  at the primary).
- Small librations about that alignment have a frequency of about Ω √(3 (B − A)/C), with Ω the orbital rate,
  A < B < C the principal moments and the axis of A pointing at the primary (Not verified, from memory; the
  attitude-dynamics texts below treat it as the standard libration result).
- **Frames.** E and I must be expressed in the same orthonormal frame, in practice the body frame. In code:
  rotate E into the body frame (Rᵀ E R), form τ_body = −ε E I, rotate back. PhysX integrates ω from a world
  torque and an inertia tensor that it rotates itself, so pass the world-frame vector.
- **Relativistic version.** To quadrupole order the torque is the same expression with E_ij the tidal tensor
  in the body's local (Fermi) frame. This follows from the quadrupole terms of the Mathisson–Papapetrou–Dixon
  equations (Dixon 1974; Ehlers & Rudolph 1977 on quasi-rigid bodies). Not verified: I did not read either
  paper's equations. Spin–curvature (Mathisson–Papapetrou) force and frame-dragging precession of the body
  axes are separate effects and are not part of this torque.
- **Validity.** The expansion needs body size ≪ curvature radius, here ≪ r (Schwarzschild) or ≪ M near a
  horizon. For a 2 m object this is satisfied everywhere outside the quantum regime. For a planet near a
  supermassive hole it is not (§5).

Sources:
- Hughes, P. C. (1986). *Spacecraft Attitude Dynamics.* Wiley (reprinted by Dover, 2004). Gravity-gradient
  torque and libration chapters. Not read for this note: Not verified (chapter numbers not given).
- Wertz, J. R. (ed.) (1978). *Spacecraft Attitude Determination and Control.* Reidel. Gravity-gradient torque
  in the environment chapter. Not read: Not verified.
- Dixon, W. G. (1974). *Dynamics of extended bodies in general relativity. III. Equations of motion.* Phil.
  Trans. R. Soc. A 277, 59. doi:10.1098/rsta.1974.0046 (Crossref record verified; content not read).
- Ehlers, J. & Rudolph, E. (1977). *Dynamics of extended bodies in general relativity: center-of-mass
  description and quasirigidity.* Gen. Rel. Grav. 8, 197. doi:10.1007/BF00763547 (Crossref record verified;
  content not read).
- Poisson, E. & Will, C. M. (2014). *Gravity.* Cambridge University Press. doi:10.1017/CBO9781139507486.
  Tidal moments and quadrupole torque in the Newtonian chapters. Not read: Not verified.

## 2. Spaghettification

Two points of a body separated by a vector ξ accelerate apart by −E ξ. For a body of length L along r̂ falling
radially through Schwarzschild, the head-to-foot stretch is

    Δa = (2M/r³) L      (SI: Δa = 2 G M L / r³ = 2 c² r_g L / r³,  r_g = GM/c²)

The sideways squeeze is half of this, M L_⊥/r³, in the opposite sense. At the horizon r = 2 r_g the stretch is
Δa = c² L / (4 r_g²), so it falls as 1/M²: gentler for bigger holes (**Derived**). The formula stays valid
inside the horizon for a radial infaller. The proper time from the horizon to the singularity of a radial
infaller from rest is π r_g / c, with the 10⁸ M☉ value 1.55 × 10³ s (**Derived**; Not verified against a text).

Radius where the stretch across L = 2 m reaches 10 g (98.07 m/s²), solving r³ = 2 c² L r_g / (10 g)
(**Derived**):

| Hole | r_g | r at 10 g | in r_g | in r_s = 2 r_g | Stretch at the horizon |
|---|---|---|---|---|---|
| 10 M☉ | 14.8 km | 3.78 × 10⁶ m | 256 | 128 | 2.1 × 10⁷ g |
| 10⁶ M☉ | 1.48 × 10⁹ m | 1.76 × 10⁸ m | 0.12 | 0.06 (inside) | 2.1 × 10⁻³ g |
| 4.3 × 10⁶ M☉ (Sgr A*-like) | 6.35 × 10⁹ m | 2.86 × 10⁸ m | 0.045 | 0.022 (inside) | 1.1 × 10⁻⁴ g |
| 10⁸ M☉ (Gargantua) | 1.48 × 10¹¹ m | 8.15 × 10⁸ m | 0.0055 | 0.0028 (inside) | 2.1 × 10⁻⁷ g |

For the stellar-mass hole the 10 g point lies far outside the horizon, so the infaller is torn apart long before
it arrives. For the supermassive holes the 10 g point is inside the horizon and a human crosses it unharmed.
The 1/M² scaling of the horizon value is the standard textbook statement; the table is my computation.
In the simulator the practical rule is to compare the largest eigenvalue of E times the body size with a
break-up acceleration, rather than to quote a radius.

For a rotating hole the stretch of an infaller is not diag(−2, 1, 1) M/r³; the tidal tensor of a Kerr
geodesic observer is known in closed form (Marck 1983, Not verified) but is the subject of the E_ij computation, not of
these notes.

Sources:
- Hartle, J. B. (2003). *Gravity: An Introduction to Einstein's General Relativity.* Addison-Wesley.
  Geodesic deviation and tidal stretching near a black hole. Not read: Not verified (chapter not given).
- Taylor, E. F. & Wheeler, J. A. (2000). *Exploring Black Holes: Introduction to General Relativity.*
  Addison-Wesley. The "tidal acceleration at the horizon falls with the mass" discussion. Not read: Not verified.
- Thorne, K. S. (2014). *The Science of Interstellar.* W. W. Norton. Tidal-gravity discussion in the
  chapters on Gargantua and Miller's planet. Full text not available: Not verified.

## 3. Roche limit and tidal disruption

**Classical results.** A secondary of radius R and density ρ_m orbiting a primary of density ρ_M and radius R_M:

| Case | Distance to the primary's centre | Source |
|---|---|---|
| Rigid secondary | d = 1.26 R_M (ρ_M/ρ_m)^{1/3} | Not verified (textbook value) |
| Fluid secondary | d ≈ 2.44 R_M (ρ_M/ρ_m)^{1/3} | Not verified (textbook value; 2.455 follows from Chandrasekhar's number below) |
| Roche ellipsoid, synchronous | Ω² / (π G ρ) = 0.0901 at the critical orbit | Not verified (from memory of Chandrasekhar's ellipsoidal-figures tables) |

**As a criterion on E.** Write the largest tidal eigenvalue λ = max eig(E) and use M = (4π/3) ρ_M R_M³,
Ω² = GM/d³ for a circular orbit (**Derived**):

    rigid (surface tidal pull = surface self-gravity):  λ R = G m / R²  ⇒  λ_crit = (4π/3) G ρ = 4.19 G ρ
                                                         (λ = 2GM/d³ gives d = 1.26 R_M (ρ_M/ρ)^{1/3}, a check)
    fluid synchronous Roche ellipsoid:                   Ω² = 0.0901 π G ρ  ⇒  GM/d³ = 0.283 G ρ,  λ = 2GM/d³ ≈ 0.57 G ρ
                                                         (d = 2.455 R_M (ρ_M/ρ)^{1/3}, consistent with 2.44)

Both reproduce the quoted distances, so the same two constants (4.19 and about 0.57) can be used with any
E that the simulator computes, Newtonian or relativistic: break up when λ exceeds λ_crit Gρ, with ρ the body's
mean density in SI (G ρ has units s⁻²). The fluid number is for a *synchronously rotating* fluid, so it
already contains the centrifugal term. A non-rotating fluid body is somewhat less stable (Not verified). A body
held together by strength between the two limits is a simple interpolation, a project choice.

**Relativistic Roche problem.** Fishbone (1973) set up equilibrium bodies on circular equatorial orbits in
Kerr by expanding the tidal potential. Ishii, Shibata & Mino (2005) extended it in Fermi normal coordinates
to third and fourth order in R/r and applied it to the Roche limit of corotating Newtonian stars (**Verified**,
abstract). Wiggins & Lai (2000) treat polytropic ellipsoids around a Kerr hole and report that the
relativistic tide disrupts a star at a larger radius than the Newtonian tide, and that the effect is smaller
for a prograde Kerr hole than for Schwarzschild (**Verified**, abstract as quoted in a search record).
Kesden (2012) computes relativistic tidal tensors for generic Kerr orbits and finds that spin raises the
maximum hole mass that can disrupt a Sun-like star to about 7 × 10⁸ M☉ (**Verified**, abstract).

**Tidal disruption radius.** Setting the stellar surface self-gravity equal to the tidal stretch:

    r_t ≈ R_* (M/m_*)^{1/3}      r_t / r_g ≈ 47 (10⁶ M☉/M)^{2/3} for the Sun

The values are 47 r_g at 10⁶ M☉ and 2.2 r_g at 10⁸ M☉ (**Derived**, R_* = 6.957e8 m). Disruption outside the
horizon needs r_t ≳ r_g to a few r_g depending on spin, which gives the **Hills mass** M_H ≈ (R_* c²/G m_*)^{3/2}
m_* = 3.2 × 10⁸ M☉ for r_t = r_g, 1.1 × 10⁸ M☉ for r_t = 2 r_g and 4 × 10⁷ M☉ for r_t = 4 r_g (**Derived**;
the capture radius is spin dependent, hence the "about 10⁸" usually quoted). Hills' original estimate is
Not verified. The fallback rate of bound debris follows dṀ ∝ t^{-5/3} at late times (Rees 1988, from memory,
Not verified); Lodato, King & Pringle (2009) show it is shallower near the peak and approaches −5/3 only at
late times, more strongly so for centrally concentrated stars (**Verified**, abstract as reported).
Gargantua (10⁸ M☉) sits right at the Hills mass for a Sun-like star. An Earth-sized planet is about five times
denser than the Sun, so its r_t is smaller by (ρ_sun/ρ)^{1/3} (§5 for the numbers).

Sources:
- Fishbone, L. G. (1973). *The relativistic Roche problem. I. Equilibrium theory for a body in equatorial,
  circular orbit around a Kerr black hole.* ApJ 185, 43. doi:10.1086/152395 (Crossref record verified; content
  not read).
- Ishii, M., Shibata, M. & Mino, Y. (2005). *Black hole tidal problem in the Fermi normal coordinates.* Phys.
  Rev. D 71, 044017. doi:10.1103/PhysRevD.71.044017, arXiv:gr-qc/0501084. Abstract read.
- Wiggins, P. & Lai, D. (2000). *Tidal interaction between a fluid star and a Kerr black hole in circular orbit.*
  ApJ 532, 530. doi:10.1086/308565, arXiv:astro-ph/9907365. Abstract-level only.
- Kesden, M. (2012). *Tidal-disruption rate of stars by spinning supermassive black holes.* Phys. Rev. D 85,
  024037. doi:10.1103/PhysRevD.85.024037, arXiv:1109.6329. Abstract-level only.
- Hills, J. G. (1975). *Possible power source of Seyfert galaxies and QSOs.* Nature 254, 295.
  doi:10.1038/254295a0 (Crossref record verified; content not read).
- Rees, M. J. (1988). *Tidal disruption of stars by black holes of 10⁶–10⁸ solar masses in nearby galaxies.*
  Nature 333, 523. doi:10.1038/333523a0 (Crossref record verified; content not read).
- Lodato, G., King, A. R. & Pringle, J. E. (2009). *Stellar disruption by a supermassive black hole: is the
  light curve really proportional to t^{-5/3}?* MNRAS 392, 332. doi:10.1111/j.1365-2966.2008.14049.x,
  arXiv:0810.1288.
- Chandrasekhar, S. (1969). *Ellipsoidal Figures of Equilibrium.* Yale University Press (reprinted in
  *A Quest for Perspectives*, World Scientific, 2001, doi:10.1142/9781848161696_0006). Not read: Not verified.

## 4. Debris after disruption

Once a body breaks up, each fragment follows its own geodesic, so the simulator needs only an initial set of
specific energies and the shared centre-of-mass state. A star of radius R_* disrupted at r_t has the near and
far sides at r_t ∓ R_*, which are at different depths of the potential well, so the specific orbital energy of
the debris spreads by (**Derived**, order of magnitude)

    ΔE ≈ G M R_* / r_t²       with r_t = R_* (M/m_*)^{1/3}  ⇒  ΔE ≈ (G m_*/R_*)(M/m_*)^{1/3}

Evaluating for the Sun and a 10⁶ M☉ hole gives ΔE ≈ 1.9 × 10¹³ J/kg. The velocity scale is √(2ΔE) ≈ 6 × 10⁶ m/s,
i.e. 0.02 c. About half of the debris is bound (E < 0) and half unbound. This is the established picture of
Rees (1988) and Lodato et al. (2009), Not verified in the text of Rees. Stone, Sari & Loeb (2013) report that
the energy spread stays roughly independent of the penetration factor β = r_t/r_p and that relativistic
corrections to it are small (**Verified**, abstract as reported). That justifies seeding fragment velocities with
a uniform spread in ΔE across the body, independent of how deep the periapsis is.

A game-friendly recipe (project choice): split the body into N chunks along the dominant stretching axis,
assign each chunk the centre-of-mass geodesic plus a velocity offset that gives it
E_i = E_cm + ΔE (2 s_i − 1), s_i ∈ [0, 1] its position along that axis, and let the geodesic integrator do the
rest.

Sources:
- Rees (1988), Lodato, King & Pringle (2009): see §3.
- Stone, N., Sari, R. & Loeb, A. (2013). *Consequences of strong compression in tidal disruption events.* MNRAS
  435, 1809. doi:10.1093/mnras/stt1270, arXiv:1210.3374. Abstract-level only.

## 5. Ocean tides and Miller's planet

**Equilibrium tide.** For a body of radius R and surface gravity g, the ocean surface is an equipotential of
gravity plus tidal potential, so the height above the unperturbed level is

    h(x) = −Φ_tide(x) / g,   Φ_tide = ½ E_ij xⁱ xʲ  evaluated at |x| = R

For a point-mass perturber (E_ij as in the header) with ψ the angle from the sub-perturber point, this gives
h(ψ) = (m_p/M) (R⁴/d³) P₂(cos ψ) where P₂ = (3 cos²ψ − 1)/2 (**Derived**). The peak at the sub-perturber point
(and the anti-point) equals (m_p/M)(R⁴/d³); the troughs at ψ = 90° are half as deep, so peak to trough is 3/2
of that. This is the "equilibrium" value: no dynamics, no land, no resonance. Check values for the Earth
(**Derived**, Earth radius 6.371e6 m and mass 5.9722e24 kg):

| Perturber | Mass (kg) | Distance (m) | Peak height h | Peak to trough |
|---|---|---|---|---|
| Moon | 7.342e22 | 3.844e8 | 0.357 m | 0.535 m |
| Sun | 1.98847e30 | 1.496e11 | 0.164 m | 0.246 m |

The 0.36 m and 0.16 m figures match the common quoted values for the lunar and solar equilibrium tide. The
real open-ocean tide is about 0.5 m but is dominated by basin resonances, and the solid Earth also deforms.
Both are corrections by the Love numbers: the observed ocean tide relative to the sea floor is
(1 + k₂ − h₂) times the equilibrium value, with h₂ ≈ 0.6, k₂ ≈ 0.3 for the solid Earth (Not verified, from
memory). A rigid planet has h₂ = k₂ = 0; a homogeneous fluid one has h₂ = 5/2 and k₂ = 3/2 (Not verified, from memory).

**Miller's planet, order of magnitude.** On the near-extremal orbit r ≈ M the natural scale of E is
1/(GM/c³)² (a tidal tensor of order M⁻² when G = c = 1; the exact eigenvalues for the orbiting frame need the
Kerr tidal tensor and are O(1) multiples of this). With GM/c³ = 492.6 s for 10⁸ M☉ (`interstellar_planets.md` §1)
and the planet defaults of §2 there (R = 6371 km, g = 12.75 m/s², ρ = 7160 kg/m³), **Derived**:

    E ~ 1 / (492.6 s)² = 4.1 × 10⁻⁶ s⁻²
    tidal acceleration at the surface  E R = 26 m/s²  = 2.1 g_planet
    equilibrium height  h = ½ E R² / g = 6.6 × 10⁶ m   (about 1 R)

So the equilibrium formula is *outside* its range; the tidal acceleration exceeds the planet's own gravity.
Against the Roche criteria of §3: G ρ = 4.8 × 10⁻⁷ s⁻², so E / G ρ ≈ 8.6, above the rigid limit (4.19) and well above
the fluid one (0.57), by this crude scale. The film's planet is therefore only plausible if E in the orbiting
frame is much smaller than this scale (the circular-orbit frame near the ISCO of an extremal hole is
strongly boosted and its tidal eigenvalues are not simply M⁻²), or if the planet is denser, or if "deformed but
not disrupted" is meant loosely. Luminet gives the same order-of-magnitude Roche estimate (Verified, per
`interstellar_planets.md`), and Schnittman writes that a terrestrial planet is "tidally disrupted just inside the
horizon of Gargantua, making Miller's planet deformed but not disrupted" (Verified, per the same file; the
abstract page checked this session does not repeat it). The simulator should therefore compute E for the orbiting
observer numerically, clamp the ocean height to a fraction of the planet radius or of the ocean depth model,
and treat the large-amplitude regime as an artistic parameter (a project choice, as with the 1 km wave height).

**Exact value (M3 stage 1, `tidal_forces.md` §3).** The boost does *not* rescue the planet. `TidalTensor` for the
circular orbit at r − M = 4 × 10⁻⁵ M, a = 1 − 1.3 × 10⁻¹⁴ gives eigenvalues (−3.00, 1.00, 2.00) / M², in agreement with
Marck's closed form (K = (L − aE)² = 1/3). That is E_max = 1.24 × 10⁻⁵ s⁻², so E R = 79 m/s² = 6.2 g_planet and
E / G ρ ≈ 26: 3 times the crude scale above, and above both Roche criteria.

**The waves.** Thorne's explanation in the film's science book is, as recalled, that the planet is tidally
locked and the water bulge rocks or librates about the hole direction, moving each hour across the surface; this
gives the periodic wave. I could not read chapter 17 and the summary in `interstellar_planets.md` marks the
mechanism as Not verified. A usable model: a tidal bulge with a small time-dependent offset angle
δ(t) = δ₀ sin(2π t/T_lib) between the bulge and the planet's frame, with T_lib of the order of the orbital or
libration period (≈ 1 h by the film's dialogue), applied to the §5 height field. This is a project choice.

Sources:
- Thorne, K. S. (2014). *The Science of Interstellar.* Norton. Chapter on Miller's planet (chapter number 17
  as given in the task; not checked). Not available: Not verified.
- Luminet, J.-P. (2015). *The Warped Science of Interstellar.* arXiv:1503.08305. Title and abstract checked
  this session (the abstract lists tidal effects); the Gargantua and tidal-locking figures are from the earlier
  full read in `interstellar_planets.md`.
- Schnittman, J. D. (2019). *Life on Miller's Planet: The Habitable Zone Around Supermassive Black Holes.*
  arXiv:1910.00940. Abstract read this session; the tidal statements above are from the earlier full read.
- Love, A. E. H. (1909). *The yielding of the Earth to disturbing forces.* Proc. R. Soc. A 82, 73. Citation from memory, not checked in Crossref. Not read:
  Not verified (cited for the Love numbers h₂, k₂).

## 6. Tidal locking timescale

For a body of mass m, radius R, moment of inertia I = α m R², rigidity μ, quality factor Q, at distance a from
a primary of mass M, starting with spin ω, the despinning time is, from memory of Gladman et al.,

    t_lock ≈ ω a⁶ I Q / (3 G M² k₂ R⁵)

with the Love number k₂ ≈ 1.5 / (1 + 19 μ / (2 ρ g R)). The strong a⁶ / M² scaling is what matters. For an
Earth-like planet around a heavy hole the time is brief (Luminet quotes about 1 ms, Verified secondary per
`interstellar_planets.md`). The exact prefactor of this formula is Not verified: I did not read the paper
(not available as arXiv). Use it for relative scaling only; the simulator can simply start tidally locked bodies
in the locked state.

Sources:
- Gladman, B., Quinn, D. D., Nicholson, P. & Rand, R. (1996). *Synchronous locking of tidally evolving
  satellites.* Icarus 122, 166. doi:10.1006/icar.1996.0117 (Crossref record verified; formula Not verified).
  No arXiv version found.

---

*The citations were checked against Crossref and the arXiv abstract pages in this session; the textbook and
article contents were not available, so every formula attributed to a book or to a paper's body is labelled
"not verified". The numerical values labelled Derived were computed here with the constants listed at the top.
Check the equation numbers in a printed copy before citing them in the paper.*
