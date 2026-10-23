# Tidal tensor of Kerr–Newman: references and check values

These notes cover the tidal tensor of milestone M3 in `docs/RELEASE_1_GOAL.md` (pillar 2, "Tidal forces"). The tidal
tensor E_ij is computed from the exact Kerr–Newman metric for any observer. This note lists the definitions, the
closed forms the code can be checked against, and which of them were checked against a source.

Conventions follow `observers.md` and `geodesics.md`: G = c = 1, Boyer–Lindquist coordinates (t, r, θ, φ), signature
(−, +, +, +), and

    Σ = r² + a² cos²θ,  Δ = r² − 2Mr + a² + Q²,  A = (r² + a²)² − Δ a² sin²θ

The ZAMO tetrad (e_t̂, e_r̂, e_θ̂, e_φ̂) is the one of `observers.md` §1. Hats mean components in an orthonormal frame.
"Verified" means the statement was read in the paper text. "Derived" means my own algebra, checked in the limits named
there. "From memory" and "Not verified" mean what they say.

---

## 1. Definition, sign and trace

For an observer with 4-velocity u and orthonormal spatial axes e_i (i = 1, 2, 3):

    E_ij = R_{μνρσ} e_i^μ u^ν e_j^ρ u^σ        (electric part of the Riemann tensor)
    B_ij = ½ ε_{ikl} C_{kl j u}-type contraction  (magnetic part: the dual Weyl tensor, same contraction;
                                                   sign depends on the orientation, see §3)

- **Geodesic deviation.** Two nearby free-falling observers with separation ξ^i (measured in the frame of one of them)
  see D²ξ^i/dτ² = −E^i_j ξ^j. The frame must be parallel transported, or the equation gets rotation terms.
- **Sign.** A negative eigenvalue stretches, a positive one compresses. In Schwarzschild the radial eigenvalue is −2M/r³.
- **Symmetric and traceless parts.** E_ij is symmetric. Its trace is the Ricci component along u:

      tr E = R_{μν} u^μ u^ν                                   (derived, from the contraction g^{αβ} R_{αμβν} u^μ u^ν)

  In vacuum (Schwarzschild, Kerr) the trace vanishes. In general:

      tr E = 8π (T_{μν} u^μ u^ν + ½ T)
           = 4π (ρ + 3p)      for a perfect fluid with density ρ and pressure p       (derived)
           = 8π T_{μν} u^μ u^ν = 8π ρ_EM  for an electromagnetic field (T = 0)      (derived)

  So the Kerr–Newman tidal tensor has a positive trace, a net compressive effect. Xin & Mummery (2025) state that
  the trace is non-zero for Q ≠ 0 and positive ("compressive"); the formula printed there (their eq. 59, garbled in
  the text extraction I read) was not checked.
- **Weyl and Ricci parts.** With the Weyl tensor C and Ricci tensor R_{μν} (R = 0 for Maxwell fields):

      E_ij = E^W_ij + ½ (δ_ij R_uu − R_ij) + (R/6) δ_ij,   E^W_ij = C_{iuju}        (derived from the Riemann decomposition)

  The magnetic part B_ij is the dual Weyl tensor contracted the same way. It has no Ricci contribution. B_ij vanishes
  for a static observer in Schwarzschild and is non-zero for any observer in Kerr that is not in the Carter frame (§3).
- **Convention check.** Zhang et al. (2012, eq. 2.3a) write Δa^i = −E^i_j ξ^j and Wiggins & Lai (2000, eqs. 1–3) write
  d²X_i/dτ² + C_ij X_j = 0 with C_ij = R_{αβγδ} e_(0)^α e_(i)^β e_(0)^γ e_(j)^δ. Both are the convention above. Verified.

Sources:
- Zhang, F., Zimmerman, A., Nichols, D. A., Chen, Y., Lovelace, G., Matthews, K. D., Owen, R. & Thorne, K. S. (2012). *Visualizing spacetime curvature via frame-drag vortexes and tidal tendexes II. Stationary black holes.* Phys. Rev. D 86, 084049. doi:10.1103/PhysRevD.86.084049, arXiv:1208.3034. Eq. 2.3a. Verified in the text.
- Wiggins, P. & Lai, D. (2000). *Tidal interaction between a fluid star and a Kerr black hole in circular orbit.* ApJ 532, 530. doi:10.1086/308565, arXiv:astro-ph/9907365. Eqs. 1–3. Verified in the text.
- Owen, R. et al. (2011). *Frame-drag vortexes and tidal tendexes I. General theory and weak-gravity applications.* Phys. Rev. D 84, 124014. doi:10.1103/PhysRevD.84.124014, arXiv:1012.4869. The definition of E and B (listed on Crossref; the text was not read).
- Xin, W. & Mummery, A. (2025). *The relativistic tidal tensor: general solutions for stationary axisymmetric spacetimes and the Hills mass of naked singularities.* arXiv:2511.21499 (no journal reference or DOI found). Same convention, trace statement for Kerr–Newman. Preprint, not peer reviewed.
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation*, ch. 11 (geodesic deviation) and ch. 31 (Schwarzschild tidal tensor). Chapter numbers from memory.

## 2. Schwarzschild

Static observer, frame (r̂, θ̂, φ̂):

    E = diag(−2M/r³, +M/r³, +M/r³),      B = 0

Zhang et al. (2012) print this as their eq. 4.5 and cite MTW eq. 31.4b for it. Verified that the paper prints it and cites
MTW. I did not open MTW.

- **Invariance under radial boosts.** An observer falling radially (raindrop or any radial geodesic) sees the same E and B.
  Argument (derived): under a boost with rapidity η along an axis, Q = E + iB transforms as a complex symmetric tensor
  under a rotation by the imaginary angle iη about that axis (§5). Schwarzschild has Q = Ψ₂ diag(2, −1, −1) with
  Q_θθ = Q_φφ, and diag(2, −1, −1) is unchanged by any rotation about the first axis. I did not find a printed statement
  of this for the radial case; MTW §31.? is the likely place (not verified).
- **Transverse boosts are not invariant.** A circular-orbit observer sees B = 0 but a changed E (§6, K ≠ 0).
- **Reissner–Nordström check (derived).** Static observer, f = 1 − 2M/r + Q²/r²: E_rr = f''/2 = −2M/r³ + 3Q²/r⁴ and
  E_θθ = E_φφ = f'/(2r) = M/r³ − Q²/r⁴. The trace is Q²/r⁴ = 8π ρ_EM with ρ_EM = Q²/(8πr⁴).
  This matches the Weyl plus Ricci split of §4.

Sources:
- Zhang et al. (2012), see §1: eq. 4.5.
- Misner, Thorne & Wheeler (1973), eq. 31.4b as cited by Zhang et al. (not opened).

## 3. Kerr: Petrov type D, the Carter frame and Ψ₂

Kerr is type D. The only non-zero Weyl scalar in a tetrad aligned with the principal null directions (the Kinnersley
tetrad) is

    Ψ₂ = −M / (r − i a cosθ)³                                  (Kinnersley / Teukolsky convention; see the caution below)

BenTov & Swearngin (2019, eq. 2.68) print Ψ₂ = −(M − Q²/R) / (R*)³ for Kerr–Newman with R = r + i a cosθ
(eq. 2.4) and R* its conjugate. At Q = 0 this is the line above. Verified for that paper's convention. I did not read
Kinnersley (1969) or Teukolsky (1973) themselves; Ψ₂ = M ρ³ with ρ = −1/(r − i a cosθ) is from memory.

**Real and imaginary parts.** Zhang et al. (2012, eqs. A2c–A2d) give, for Kerr,

    Q_e = M r (r² − 3a² cos²θ) / Σ³,     Q_m = M a cosθ (3r² − a² cos²θ) / Σ³

and relate them to the Kinnersley Ψ₂ by Ψ₂ = −Q_e + i Q_m (verified, in the Acknowledgments-adjacent text of Appendix A).
Expanding −M/(r − i a cosθ)³ = −M (r + i a cosθ)³/Σ³ gives Re Ψ₂ = −Q_e, as in the paper, but Im Ψ₂ = −Q_m.
**The sign of the imaginary part therefore differs between the two sources**: it depends on the orientation
convention (sign of ε_ijk, direction of m). Only the sign of B depends on it. E does not.

**Carter (canonical, principal) frame.** The orthonormal tetrad aligned with the principal null directions is

    e_0 = ((r² + a²) ∂_t + a ∂_φ) / √(ΔΣ)          e_1 = √(Δ/Σ) ∂_r
    e_2 = ∂_θ / √Σ                                  e_3 = (∂_φ + a sin²θ ∂_t) / (√Σ sinθ)

(from memory; Carter 1968 and Chandrasekhar 1983 are the usual sources, not opened). Its observer has
Ω = dφ/dt = a / (r² + a²), independent of θ. In this frame E and B are both diagonal, and the Weyl scalar gives

    E_ij + i B_ij = Ψ₂ · diag(2, −1, −1)           in (r̂, θ̂, φ̂)-like axes (e_1, e_2, e_3)

Check against Zhang et al. (2012, eq. 3.2): Ψ₂ = (E_NN + i B_NN)/2 with N the unit normal, so E_NN = 2 Re Ψ₂ and
E_θθ = E_φφ = −Re Ψ₂, which gives E = Q_e diag(−2, 1, 1) (verified for E). At a = 0 this is the Schwarzschild tensor of §2.
The sign of B follows the orientation caution above. In their own eq. A2b the diagonal B has the opposite sign
to what eq. 3.2 plus Ψ₂ = −Q_e + i Q_m would give. I could not resolve this, so the sign of B is **not verified**.

**Carter observer relative to the ZAMO (derived).** e_1 and e_2 coincide with e_r̂ and e_θ̂. The pair (e_0, e_3) is the
ZAMO pair (e_t̂, e_φ̂) boosted along φ̂. With Ω − ω = a Δ Σ / ((r² + a²) A), ϖ/α = A sinθ / (Σ √Δ) and
v_φ̂ = (Ω − ω) ϖ / α (`observers.md` §3):

    v_φ̂ = a √Δ sinθ / (r² + a²),     γ = (r² + a²) / √A,     v² = ξ ≡ Δ a² sin²θ / (r² + a²)²

The Carter observer is at rest relative to the ZAMO on the axis and at the horizon, and moves prograde elsewhere.
For Kerr–Newman the same algebra holds with the charged Δ and ω. The quantity ξ is exactly the ξ of Zhang et al.
(2012, eq. A2e), which supports the result: their ZAMO-frame tidal tensor (below) comes from boosting
Q_e diag(−2, 1, 1) with v² = ξ.

**ZAMO-frame tensor (verified, Kerr only).** Zhang et al. (2012, eqs. A2a–A2f) give E for the ZAMO tetrad in (r̂, θ̂, φ̂):

    E_r̂r̂ = −Q_e (2 + ξ)/(1 − ξ)     E_θ̂θ̂ = Q_e (1 + 2ξ)/(1 − ξ)     E_φ̂φ̂ = Q_e
    E_r̂θ̂ = μ Q_m,    μ = 3 a √Δ (r² + a²) sinθ / A = 3 √ξ/(1 − ξ),    E_r̂φ̂ = E_θ̂φ̂ = 0

Their B has the same form with Q_e → Q_m, Q_m → −Q_e. The (r̂, θ̂) block is not diagonal off the axis, which is why a
ZAMO tensor test needs the off-diagonal term. I wrote E_θ̂θ̂ and E_r̂θ̂ as they read in the text extraction, with the
matrix layout partly garbled. Check them against the trace (E_r̂r̂ + E_θ̂θ̂ + E_φ̂φ̂ = 0) before trusting them.

Sources:
- Zhang et al. (2012), see §1: eqs. 3.2, A2a–A2f.
- BenTov, Y. & Swearngin, J. (2019). *Gravitational shockwaves on rotating black holes.* Gen. Relativ. Gravit. 51, 76 (volume from Crossref; article number not checked). doi:10.1007/s10714-019-2512-7, arXiv:1706.03430. Eqs. 2.4, 2.68, 2.69.
- Marck, J.-A. (1983). *Solution to the equations of parallel transport in Kerr geometry; tidal tensor.* Proc. R. Soc. Lond. A 385, 431–438. doi:10.1098/rspa.1983.0021 (checked on Crossref; the paper itself was not read, the results are quoted through Wiggins & Lai and Xin & Mummery).
- Kinnersley, W. (1969). *Type D vacuum metrics.* J. Math. Phys. 10, 1195–1203. doi:10.1063/1.1664958 (Crossref only).
- Teukolsky, S. A. (1973). *Perturbations of a rotating black hole. I.* ApJ 185, 635. doi:10.1086/152444 (Crossref only).
- Carter, B. (1968). *Global structure of the Kerr family of gravitational fields.* Phys. Rev. 174, 1559. doi:10.1103/PhysRev.174.1559
- Chandrasekhar, S. (1983). *The Mathematical Theory of Black Holes.* Oxford University Press. Chapter on the Kerr tetrads; location not verified.

## 4. Kerr–Newman: Ψ₂ and the Ricci part

Verified (BenTov & Swearngin 2019, eqs. 2.68–2.69), with R = r + i a cosθ:

    Ψ₂ = −(1/(R*)³) (M − Q²/R) = −M/(r − i a cosθ)³ + Q² / ((r + i a cosθ)(r − i a cosθ)³)
    Φ₁₁ = Q² / (2 |R|⁴) = Q² / (2Σ²)

The first form is the printed one and the second is its expansion (verified). The paper does not give the tetrad, and
the sign convention of Φ₁₁ was not checked. It uses the Newman–Penrose normalisation, where the Maxwell field sets
Φ₁₁ = 2π(T_{ln} + T_{mm*}) up to convention (from memory).

**How the electric tensor gets a Ricci part.** Ψ₂ gives only the Weyl part E^W. The Maxwell field adds the second term
of the split in §1. In the Carter frame the field of Kerr–Newman has radial E and B components only, and its energy
density is ρ_EM = Q²/(8πΣ²). This is from memory for the field and derived for what follows. The stress is then
T_ij = ρ_EM diag(−1, 1, 1), so R_uu = Q²/Σ² and R_ij = (Q²/Σ²) diag(−1, 1, 1):

    E = Q_e' diag(−2, 1, 1)-type Weyl part from Re Ψ₂(KN)  +  (Q²/Σ²) diag(1, 0, 0)  =  … + 2 Φ₁₁ diag(1, 0, 0)

It is derived, and checked against Reissner–Nordström (§2: E^W_rr = −2M/r³ + 2Q²/r⁴ plus Q²/r⁴ gives −2M/r³ + 3Q²/r⁴;
E_θθ = M/r³ − Q²/r⁴ with no Ricci part). The trace is Q²/Σ², positive. For any other observer, transform E^W and the
Ricci term separately (§5): the Ricci term is not boost invariant, because R_uu and R_ij change with u.

Xin & Mummery (2025) note that Marck's method relies on Ricci flatness and does not carry over to Kerr–Newman. They
compute the Kerr–Newman tidal tensor for geodesics from the ZAMO frame instead. Verified (their §VI text).

Sources:
- BenTov & Swearngin (2019), see §3.
- Xin & Mummery (2025), see §1: §VI.
- Newman, E. T. et al. (1965). *Metric of a rotating, charged mass.* J. Math. Phys. 6, 918. doi:10.1063/1.1704351 (the field; not checked here).

## 5. Transforming E and B between observers at the same event

Two observers at one event are related by a Lorentz transformation, so E and B transform as the electric and magnetic
parts of the Weyl tensor. The complex combination Q = E + iB (symmetric, traceless, 3 × 3) transforms under a boost with
velocity v = tanh η along unit vector n as under a **rotation by the imaginary angle iη about n**:

    Q' = R Q Rᵀ,   R = rotation about n by χ = iη  (cos χ = γ, sin χ = i γ v)               (derived; sign of η follows the orientation)

This follows from SL(2, C) acting on the Weyl spinor and is standard; the closed forms of Maartens & Bassett (1998) or
Penrose & Rindler vol. 1 give the same thing. I did not locate the equation numbers, so none are given here.

Worked check (derived, then compared with a source): start from the Carter frame, Q = Ψ₂ diag(2, −1, −1), and
boost along the third axis with v² = ξ. Then

    Q'_11 = γ² (Q_11 − v² Q_22) = Ψ₂ (2 + ξ)/(1 − ξ),     Q'_12 = (Q_11 − Q_22) cosχ sinχ = 3 i γ² v Ψ₂

so E'_11 = Re Q'_11 = −Q_e (2 + ξ)/(1 − ξ) and |E'_12| = |Re(3 i γ² v Ψ₂)| = 3γ² v |Im Ψ₂| = μ Q_m, with μ = 3√ξ/(1 − ξ). Both match Zhang et al. (2012,
eqs. A2a, A2f). Verified. This is the cleanest unit test: boost the Carter tensor, compare with the ZAMO closed form.

For the pure vector form, with E, B in the old frame and a boost v, the Weyl formulas are the same as for the electromagnetic
field tensor squared. Write them from the rotation above instead of from memory.

When the observer's spatial axes are also rotated (Gram–Schmidt instead of a pure boost, `observers.md` §2), apply the
spatial rotation to E and B afterwards. The eigenvalues of E do not depend on it.

Sources:
- Zhang et al. (2012), see §1: eqs. A2a–A2f. Verified as the comparison above.
- Maartens, R. & Bassett, B. A. (1998). *Gravito-electromagnetism.* Class. Quantum Grav. 15, 705. arXiv:gr-qc/9704059 (title and journal from memory; DOI and contents not checked).
- Penrose, R. & Rindler, W. (1984). *Spinors and Space-Time*, vol. 1. Cambridge University Press (from memory; chapter not located).

## 6. Equatorial geodesics and circular orbits in Kerr (closed form)

For a geodesic in the equatorial plane with parallel-transported frame (e_1, e_2 in the plane, e_3 normal to it), Marck
(1983) gives, as quoted by Wiggins & Lai (2000, eqs. 4–7), with K = (L − aE)² (Carter constant with Q_C = 0) and the
angle Ψ_M of the rotating in-plane axes:

    C_11 = (M/r³) [1 − 3 (r² + K) cos²Ψ_M / r²]
    C_22 = (M/r³) [1 − 3 (r² + K) sin²Ψ_M / r²]
    C_33 = (M/r³) [1 + 3K/r²]
    C_12 = C_21 = −(3M/(2r³)) ((r² + K)/r²) sin 2Ψ_M
    dΨ_M/dτ = (E (L − aE) + a) / (r² + K)

Eigenvalues (verified: Wiggins & Lai eqs. 4–8 plus Xin & Mummery eq. 55, which agree):

    λ_⊥ = (M/r³) (1 + 3K/r²)         normal to the plane (e_3), compressive
    λ_1 =  M/r³                       in the plane, constant
    λ_2 = −(M/r³) (2 + 3K/r²)         in the plane, stretching

The trace is zero. In Schwarzschild (a = 0) K = L². Static limit K = 0 gives (+1, +1, −2) M/r³.
The in-plane eigenvectors rotate with Ψ_M: they are not fixed in the orbit frame.

**Circular orbit.** For circular equatorial orbits Wiggins & Lai (2000, eqs. 9–11) give (verified):

    E = (r² − 2Mr ± a√(Mr)) / (r P),   L = ±√(Mr) (r² ∓ 2a√(Mr) + a²) / (r P),   P = √(r² − 3Mr ± 2a√(Mr))
    dΨ_M/dτ = √(M/r³)

Substituting (my own algebra, checked at a = 0 where K = M r²/(r − 3M)):

    K = (L − aE)² = r² (√(Mr) ∓ a)² / P²                                                    (derived)

Upper sign is prograde, lower sign retrograde. For Schwarzschild, λ_⊥ = (M/r³) · r/(r − 3M), which tends to M/r³ at large r.
I did not check that closed form against another source. The ISCO and plunge values need the same formulas at r_ISCO.

Further sources not read: Ishii, Shibata & Mino (2005) extend Marck's frame to the Fermi-normal-coordinate expansion of
the tidal potential to third and fourth order in R/r (abstract checked; the equations were not).

Sources:
- Wiggins & Lai (2000), see §1: eqs. 4–11. Verified.
- Xin & Mummery (2025), see §1: eq. 55, with k = (l_z − aε)². Verified.
- Marck (1983), see §3.
- Ishii, M., Shibata, M. & Mino, Y. (2005). *Black hole tidal problem in the Fermi normal coordinates.* Phys. Rev. D 71, 044017. doi:10.1103/PhysRevD.71.044017, arXiv:gr-qc/0501084. Abstract only.
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). ApJ 178, 347. doi:10.1086/151796. Source of E and L for circular orbits (see `observers.md` §4).

## 7. Conversion to SI

E has dimension 1/length² when time is measured in length. With M the length GM/c²:

    E[s⁻²] = E[geometric, 1/M²] × c² / (GM/c²)² = E[1/M²] × c⁶ / (GM)²           (derived)

Check: a static Schwarzschild observer has E_θ̂θ̂ = M/r³ (geometric) = GM/c² · c² / r³ = GM/r³ in s⁻². The Newtonian value.

The tidal acceleration across a body of length L along an eigenvector is

    a_tidal = |λ| · L          [m s⁻²],      in units of g: a_tidal / 9.80665 m s⁻²

with λ in s⁻² and L in m. In code, keep the unit in the name (`lambdaPerS2`, `lengthM`). Example (own arithmetic, recompute
with `GPP::Units`): at r = 2GM/c² around 10 M☉ the radial Schwarzschild eigenvalue is c⁶/(4 (GM)²) ≈ 1.0 × 10⁸ s⁻², so a body
2 m long feels ≈ 2 × 10⁸ m s⁻² ≈ 2 × 10⁷ g.

## 8. Validity of a local flat patch

The tidal tensor is the leading curvature term of the metric in Fermi normal coordinates (Manasse & Misner 1963):
g_âb̂ = η_âb̂ − (1/3) R_âĉb̂d̂ x^ĉ x^d̂ + O(x³). It is valid only for patches small against the curvature radius, which scales as
(r³/M)^{1/2}. `m2_rendering_references.md` §4.2 gives the estimate L ≲ √(ε r³/M) and the numerical example at r = 6M.
The same caution applies here: E_ij describes only the leading order, and Ishii et al. (2005) show that third- and
fourth-order terms matter at R/r ≳ 0.1 (abstract checked). For an accelerated observer (hovering, powered) the geodesic
deviation equation needs extra terms from the 4-acceleration, and E_ij alone is not the full tidal field. Fermi–Walker
transport replaces parallel transport there.

Sources:
- Manasse, F. K. & Misner, C. W. (1963). *Fermi normal coordinates and some basic concepts in differential geometry.* J. Math. Phys. 4, 735–745. doi:10.1063/1.1724316 (Crossref only).
- Misner, Thorne & Wheeler (1973), §13.6 (as cited in `m2_rendering_references.md` §4.2; not opened).
- Ishii et al. (2005), see §6.

---

*DOIs of Marck 1983, Kinnersley 1969, Teukolsky 1973, Manasse & Misner 1963, Ishii et al. 2005, Wiggins & Lai 2000, Owen et al. 2011
and BenTov & Swearngin 2019 were checked against Crossref. The formulas of §1 (convention), §3 (Q_e, Q_m, ZAMO tensor), §4 (Ψ₂, Φ₁₁),
§6 (eigenvalues, circular orbit E, L) were checked against the paper texts. The Carter tetrad, the velocity relative to the ZAMO,
the complex-rotation law, the Ricci split and the SI conversion are my own derivations, tested only in the limits stated. The sign of
B and Im Ψ₂ is not settled between sources. Everything marked "Not verified" or "from memory" was not checked. Check section and equation
numbers in a printed copy before citing them in the paper.*
