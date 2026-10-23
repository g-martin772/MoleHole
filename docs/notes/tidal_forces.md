# Tidal forces: the tidal tensor and what is built on it

These notes cover milestone M3 in `docs/RELEASE_1_GOAL.md`. They describe the code; the physics and its sources are in
two reference notes:

- `tidal_tensor_references.md`: definition, Schwarzschild, Kerr (type D, Carter frame, Ψ₂), Kerr–Newman, boosts,
  Marck's circular-orbit eigenvalues, SI conversion
- `tidal_effects_references.md`: gravity-gradient torque, spaghettification, Roche limit and tidal disruption, debris,
  ocean tides, tidal locking

Stages:

| Stage | Content | State |
|---|---|---|
| 1 | Tidal tensor for any observer (CPU, double) | done |
| 2 | Readout (camera window, HUD), tensor glyph overlay, Tidal Stretch node | done |
| 3a | Gravity-gradient torque on rigid bodies | planned |
| 3b | Spaghettification: jointed composites in the body's local frame (needs joints in gpp) | planned |
| 4 | Roche check and break-up into fragment geodesic bodies | planned |
| 5 | Equilibrium ocean tide on terrain with an ocean | planned |

Conventions follow `observers.md`: G = c = 1, Boyer–Lindquist coordinates, units of the hole's mass M.

---

## 1. Stage 1: the tidal tensor

Code: `src/physics/tidal.cppm` (`:Physics.Tidal`). Tests: `tests/unit/physics/tidal_tests.cpp`.

The tidal tensor of an observer with 4-velocity u and spatial axes e_i is

    E_ij = R_αβγδ e_i^α u^β e_j^γ u^δ,      d²ξ^i/dτ² = −E^i_j ξ^j

so a negative eigenvalue stretches and a positive one squeezes. A static observer in Schwarzschild sees
diag(−2M/r³, M/r³, M/r³) along (r̂, θ̂, φ̂).

**Why no derivatives of the metric.** The obvious route, Christoffel symbols and their derivatives from
`MetricTensor`, is used only in the test. In the throat of a near-extremal hole g_rr ≈ (M/(r − M))² is about 10⁹ at
Miller's orbit, and finite differences lose every digit. Instead the curvature is written down exactly in Carter's
orthonormal frame and Lorentz transformed:

1. **Carter's frame** (`CarterFrameVelocity`). Its observer circles the axis with Ω = a/(r² + a²) and shares e_r̂ and
   e_θ̂ with the ZAMO. Relative to the ZAMO it moves along φ̂ with

       v_C = a sinθ √Δ / (r² + a²)

   from v = (Ω − ω) ϖ/α and A − (r² + a²)(2Mr − Q²) = ΔΣ. Δ is the factored `HorizonDelta`, so v_C keeps its precision
   at the horizon, where it goes to 0.
2. **The Riemann tensor in that frame** (`CarterFrameRiemann`):
   - Weyl part from Ψ₂ = −M/ρ̄³ + Q²/(ρ ρ̄³) with ρ̄ = r − i a cosθ. The electric and magnetic parts are diagonal,
     E − iB = Ψ₂ diag(2, −1, −1). C_0i0j = E_ij, C_0ijk = ε_jkl B_li, C_ijkl = −ε_ijp ε_klq E_pq.
   - Ricci part of the Maxwell field (R = 0): R_ab = (Q²/Σ²) diag(1, −1, 1, 1), added as
     ½ (η_ac R_bd − η_ad R_bc − η_bc R_ad + η_bd R_ac).
3. **The observer** (`TidalTensor`). Its frame relative to the ZAMO is `BoostMatrix(v)`, the matrix form of
   `ObserverTetrad`; the ZAMO relative to Carter's frame is `BoostMatrix(−v_C)`. Their product gives the observer's
   frame in Carter components, and `ContractTidal` contracts it with the Riemann tensor. The result is along the
   observer's own axes, the boosted ZAMO axes of `ObserverTetrad`.

**The sign of B.** The literature is inconsistent on it (`tidal_tensor_references.md` §3: Zhang et al. 2012 give both
signs in different equations). E alone does not care, but a boosted observer's E mixes in B, so the sign matters for
every moving observer in Kerr. It was fixed against the coordinate Riemann tensor: with E + iB the off-diagonal
E_r̂θ̂ of boosted observers came out with the wrong sign; with E − iB all cases agree.

**Other functions.**
- `TidalTensor(p, state)` uses the momentary velocity of a geodesic (`ZamoVelocity`), for geodesic bodies.
- `TidalTensorSi` multiplies by c²/r_g² = c⁶/(GM)², giving s⁻². Times a length it is the tidal acceleration across it.
- `PrincipalAxes` diagonalises the symmetric tensor (cyclic Jacobi), eigenvalues ascending: [0] is the strongest
  stretch, [2] the strongest squeeze.

## 2. Validation

| Test | Checks |
|---|---|
| Schwarzschild static | diag(−2M/r³, M/r³, M/r³), 10⁻¹⁴ |
| Radial boosts | a radially moving observer, including a raindrop near the horizon, sees the static tensor (`tidal_tensor_references.md` §2, derived) |
| Reissner–Nordström | radial −2M/r³ + 3Q²/r⁴, transverse M/r³ − Q²/r⁴ |
| Coordinate Riemann | the closed form against R_abcd from central differences of `MetricTensor` (h = 10⁻³), contracted with `ObserverTetrad`: Schwarzschild, Kerr a = 0.9 and 0.998, Kerr–Newman, on and off the equator, boosts in all directions; agreement to 10⁻⁵ M/r³ |
| Vacuum trace | tr E = 0 for any observer in Kerr |
| Circular orbits | eigenvalues −(M/r³)(2 + 3K/r²), M/r³, (M/r³)(1 + 3K/r²) with K = (L − aE)² (Marck 1983, as quoted by Wiggins & Lai 2000), squeeze axis along θ̂: Schwarzschild, Kerr a = 0.9, and Miller's orbit |
| Principal axes | M x = λ x, unit vectors |
| SI | radial 2GM/r³ for a 10 M☉ hole at 1000 km |

The coordinate-Riemann test is the independent check: it shares no code with the closed form except `MetricTensor`
and `ObserverTetrad`, both tested in `observer_tests.cpp`.

## 3. Result: Miller's planet is above the Roche limit

On Miller's orbit (a = 1 − 1.3 × 10⁻¹⁴, r − M = 4 × 10⁻⁵ M, `interstellar_planets.md`) the orbiting observer's
eigenvalues are

    (−3.00, 1.00, 2.00) / M²        K = (L − aE)² = 1/3

finite, as Ψ₂ ≈ −1/M² there, and in agreement with Marck's closed form (the test checks it to 10⁻⁶). For
Gargantua at 10⁸ M☉ (GM/c³ = 492.6 s) and the planet of `interstellar_planets.md` (R = 6371 km, g = 12.75 m/s²,
ρ = 7160 kg/m³), derived:

    E_max = 3 / (GM/c³)² = 1.24 × 10⁻⁵ s⁻²
    tidal acceleration across R:  79 m/s² = 6.2 g_planet
    E_max / (G ρ) = 26       (rigid Roche limit 4.19, fluid 0.57; tidal_effects_references.md §3)

So with these parameters the planet is torn apart, not "deformed but not disrupted" (Schnittman 2019, quoting Thorne).
Because E ∝ 1/M², the rigid criterion needs Gargantua ≳ 2.5 × 10⁸ M☉ and the fluid one ≳ 7 × 10⁸ M☉ at this density.
The film quotes Gargantua as "at least" 10⁸ M☉ (Thorne 2014; `geodesic_bodies.md` §7), so a heavier hole keeps the
time dilation (it depends only on r/M and a) and saves the planet.

**Decision: `templates/interstellar-bodies.yaml` uses 10⁹ M☉.** All positions were scaled by 10 (cameras keep their
offsets from their planets), so r/M is unchanged; orbital speeds in m/s do not depend on M, so `TimeScale` stays. At
10⁹ M☉: E_max = 1.24 × 10⁻⁷ s⁻², E R = 0.06 g_planet, E_max / (G ρ) = 0.26, below the fluid limit 0.57.
`gargantua-orbits.yaml` (M1, no planet) keeps 10⁸ M☉.

The equilibrium-tide formula h = ½ E R²/g gives 2 × 10⁷ m at 10⁸ M☉, three planet radii: outside its range
(`tidal_effects_references.md` §5). At 10⁹ M☉ it is 2 × 10⁵ m (3 % of R): inside the range of the linear formula,
but a static bulge 200 km high, not the film's 1.2 km waves. Stage 5 still needs a design target for the waves.

## 4. Stage 2: readout, overlay and node

Code:
- `src/simulation/tidal_readout.cppm` (`:Simulation.TidalReadout`): `TidalSample` (eigenvalues in s⁻², axes as world
  directions), `MakeTidalSample`, `BodyTidalSample`, `TidalStretchAcrossMs2`
- `BoyerLindquistBasisWorld` in `src/simulation/geodesic_module.cppm`, now shared with `AberrateForCamera`
- `ObserverReadout::Tidal` in `src/simulation/observer_readout.cppm`
- `AppendTidalGlyph`, `TidalAxesOverlay` in `src/rendering/orbit_overlays.cppm`, drawn by `OverlayGeometryStage`
- render settings `overlay.tidal_axes`, `overlay.tidal_axes_scale`; legend `kTidalAxesLegend`
- the camera window's Observer section (`plugins/camera_window_layer.cpp`), the HUD gizmo and body labels
  (`plugins/viewport_hud_layer.cpp`)
- node `NodeSubType::TidalStretch` with `ObserverTidalStretchAt` in `src/simulation/orbit_tools.cppm`

Tests: `tests/unit/simulation/tidal_readout_tests.cpp`, the Tidal Stretch cases in `relativity_nodes_tests.cpp` and
`script_runtime_tests.cpp` (interpreter against Luau).

**Which observer, which metric.** The readout follows the rules of the rest of the Observer section
(`observers.md` §7): a camera riding a Geodesic Body in free fall gets the body's tensor in its hole's full metric;
otherwise the `camera.observer` preset at the camera position, in the render metric, with the ZAMO after a fallback.
Glyphs are drawn for every Geodesic Body in free fall, except the one a riding camera sits on (its tensor is the HUD
gizmo). The Tidal Stretch node uses the first hole's full metric, as Clock Rate does.

**Axes in the world.** The eigenvectors are components along the observer's boosted ZAMO axes. They are drawn along
the world directions the renderer gives the ZAMO's (r̂, θ̂, φ̂) (`BoyerLindquistBasisWorld`, as `blBasis` in
`kerr.glsl`). For a static observer in Schwarzschild the stretch axis points exactly at the hole (tested).

**The glyph.** Three axes through the body, each with a half-length of `overlay.tidal_axes_scale` body radii times
|λ|/|λ|_max: red with outward arrowheads for stretch, blue with inward ones for squeeze. On a circular orbit there is
one stretching axis, nearly radial, and the strongest squeeze is perpendicular to the orbital plane (tested). A body
without a Sphere uses 1 % of M of its hole as its radius.

**Readout values.**
- Tidal stretch: −λ_min in s⁻², and the acceleration difference across 2 m in Earth gravities (a person falling feet
  first).
- Tidal squeeze: λ_max in s⁻².
- HUD body label: the stretch, and across the Sphere's radius in Earth gravities.

**Rendered check.** `templates/m1-orbits.yaml` exported with `overlay.tidal_axes=true`: every body's red axis points
at the hole, and the blue axes are perpendicular to it.

## Limitations (stage 1)

- **First-order tides only.** E is the leading term of the tidal field across a body. Ishii, Shibata & Mino (2005)
  give the third- and fourth-order terms in Fermi normal coordinates; they matter when the body is not small
  against the curvature radius, which is the case on Miller's orbit (R/M = 4.3 × 10⁻⁵ against a curvature radius of
  about M/√3, so they are tiny there).
- **Outside the horizon.** The formulas hold everywhere, but the observer velocities come from the ZAMO, which exists
  only for r > r₊.
- **Charge.** Kerr–Newman is covered by the closed form and the coordinate-Riemann test; there is no independent
  closed form for its circular orbits (Marck's method needs a Ricci-flat spacetime, `tidal_tensor_references.md` §6).

## Limitations (stage 2)

- **Not lensed.** Glyphs are rasterised at the body's coordinate position, like the characteristic radii. Near the
  hole the traced sphere appears displaced by lensing, so the glyph can sit beside its body.
- **Flat placement of the axes.** r̂ and θ̂ of the oblate embedding are orthogonal only to O(a²/r²); the glyph shows the
  directions, not an exact frame.
- **No HUD gizmo in exports.** The camera gizmo is part of the UI; headless exports show the body glyphs only.
- **One scalar node.** Tidal Stretch returns the magnitude only; the axes are not exposed to graphs or Luau yet.

---

*The test statements were checked by running `molehole_tests "[tidal]"`. The numbers in §3 were computed with
40-digit decimal arithmetic from the Kerr circular-orbit formulas and match the test's double-precision values.*
