# Observers, local frames and the moving camera

These notes cover stage 2 of milestone M1 in `docs/RELEASE_1_GOAL.md`. A camera, or later a person or a
spacecraft, is an *observer*: a world line with its own clock and its own local sense of directions. Stage 2
describes observers with orthonormal frames (tetrads), works out their clock rates, and lets the integrated
renderer see the sky from a moving camera.

Code:
- `src/physics/observer.cppm` (`:Physics.Observer`)
- `FillObserverData` in `src/rendering/render_data.cppm`
- `boostPhoton` in `shaders/kerr.glsl` and the camera block in `rk4RayMarching` (`shaders/ray_tracing.glsl`)
- the render setting `camera.observer`

Tests: `tests/unit/physics/observer_tests.cpp`.

Conventions follow `geodesics.md`: G = c = 1, Boyer–Lindquist coordinates (t, r, θ, φ) of Kerr–Newman, and

    Σ = r² + a² cos²θ,  Δ = r² − 2Mr + a² + Q²,  A = (r² + a²)² − Δ a² sin²θ

Beware the notation clash with Bardeen, Press & Teukolsky (1972) and James et al. (2015). Their Σ (Σ_J) is
our √A, and their ρ² is our Σ.

---

## 1. The reference frame: the ZAMO

Each observer is described by its 3-velocity relative to the **zero-angular-momentum observer** (ZAMO, also
called the locally nonrotating frame or FIDO). This is the same frame the renderer already launches photons
from (`metric_rendering.md` §4).

The ZAMO falls neither inwards nor outwards. It is dragged around the spin axis at the frame-dragging rate
ω and exists everywhere outside the horizon. Its tetrad (`ZamoTetrad`) is:

    e_t̂ = (∂_t + ω ∂_φ) / α     e_r̂ = √(Δ/Σ) ∂_r     e_θ̂ = ∂_θ / √Σ     e_φ̂ = ∂_φ / ϖ

    α = √(ΣΔ / A)     (lapse; the ZAMO's dτ/dt)
    ω = a (2Mr − Q²) / A
    ϖ = √(A / Σ) sinθ (circumferential radius √g_φφ)

The metric in this form is −α² dt² + (Σ/Δ) dr² + Σ dθ² + ϖ² (dφ − ω dt)². James et al. 2015 give the Kerr
case as eqs. A.1–A.3.

For Kerr–Newman, ω follows from ω = −g_tφ / g_φφ with the charged Δ. The Q² term appears because
r² + a² − Δ = 2Mr − Q². I have not found a source that prints this form; it is a direct consequence of the
metric (Newman et al. 1965).

Sources:
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). *Rotating black holes: locally nonrotating frames, energy extraction, and scalar synchrotron radiation.* ApJ 178, 347. doi:10.1086/151796. The locally nonrotating frames are in §II (equation numbers not verified).
- Bardeen, J. M. (1970). *Kerr metric black holes.* Nature 226, 64. doi:10.1038/226064a0
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001. doi:10.1088/0264-9381/32/6/065001, arXiv:1502.03808. App. A, eqs. A.1–A.3.
- Newman, E. T. et al. (1965). *Metric of a rotating, charged mass.* J. Math. Phys. 6, 918. doi:10.1063/1.1704351

## 2. Moving observers: the boosted tetrad

An observer that moves with velocity **v** (|v| < 1, components along e_r̂, e_θ̂, e_φ̂) through the local
ZAMO has the following 4-velocity and spatial axes (`ObserverTetrad`):

    u   = γ (e_t̂ + vⁱ e_î)
    e_j = γ v_j e_t̂ + e_ĵ + (γ − 1) v_j vᵏ e_k̂ / v²

This is a pure Lorentz boost with no rotation, so the observer's axes stay as parallel as possible to the
ZAMO's. In code, (γ − 1)/v² is written as γ²/(γ + 1). The two are equal, but the second form avoids the
cancellation at low speeds, which matters in float on the GPU.

- **Arbitrary 4-velocity.** `GramSchmidtTetrad` builds an orthonormal frame from any timelike 4-velocity
  by orthonormalising ∂_r, ∂_θ, ∂_φ against it. It spans the same rest space, but its spatial axes may be
  rotated relative to the boosted frame.
- **Tests.** Both constructions are checked for e_a · e_b = η_ab under the full metric, and the
  Gram–Schmidt frame keeps u.

Sources:
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation*, §2.9 and ch. 13 (local Lorentz frames; section numbers not verified).
- Müller, T. & Frauendiener, J. (2011). *Studying null and time-like geodesics in the classroom.* Eur. J. Phys. 32, 747. doi:10.1088/0143-0807/32/3/011, arXiv:1105.0109. Local tetrads for visualisation.

## 3. Observer presets

| Preset (`ObserverKind`) | Motion | Velocity relative to the ZAMO | Exists |
|---|---|---|---|
| ZAMO | L = 0, co-rotates at ω | 0 | everywhere outside r₊ |
| Static | at rest relative to the distant stars, Ω = 0 | v_φ̂ = −ω ϖ / α | only outside the ergosphere (g_tt < 0 ⇔ \|v\| < 1) |
| Orbiting | angular velocity Ω(r) of the circular equatorial orbit | v_φ̂ = (Ω − ω) ϖ / α | outside the photon orbit, where the circular orbit is timelike |
| Raindrop | free fall from rest at infinity: E = 1, L = 0, Carter constant 0 | radial, from the geodesic momentum (`ZamoVelocity`) | everywhere outside r₊ |

- **Rotating observers.** Any observer circling the axis with Ω = dφ/dt has v_φ̂ = (Ω − ω) ϖ / α. This is
  eq. A.7 of James et al. 2015, where their circular-orbit camera has β = (ϖ/α)(Ω − ω).
- **Orbiting off the equator.** In the equatorial plane the orbiting observer is on a circular geodesic, and
  the tests compare it with `MakeCircularOrbit`. Off the plane it keeps the Ω of the equatorial orbit at the
  same r, so it is accelerated, not free-falling.
- **Static observers in the ergosphere.** Inside the ergosphere no observer can stay at rest relative to the
  distant stars. At the static limit the required counter-rotation reaches the speed of light.
- **Raindrops.** These are the observers of Painlevé–Gullstrand coordinates, generalised to Kerr by Doran.
  With E = 1, L = 0 and Carter constant 0, the θ potential vanishes identically, so a raindrop falls at
  constant θ. The tests integrate one down to the horizon to check this.
  - In Schwarzschild the raindrop moves inwards at the Newtonian escape speed √(2M/r) relative to the static
    observers and reaches c at the horizon. This is the "river model" picture.
- **Geodesics.** `ZamoVelocity(state)` turns any geodesic of the integrator into an observer:
  - ε = (E − ωL)/α
  - v = (√(Δ/Σ) p_r, p_θ/√Σ, L/ϖ) / ε

  It is the inverse of `MakeGeodesicFromZamoVelocity`. Stage 3 uses it to put cameras on moving bodies.

Sources:
- Painlevé, P. (1921). *La mécanique classique et la théorie de la relativité.* C. R. Acad. Sci. (Paris) 173, 677.
- Gullstrand, A. (1922). *Allgemeine Lösung des statischen Einkörperproblems in der Einsteinschen Gravitationstheorie.* Ark. Mat. Astron. Fys. 16(8), 1.
- Doran, C. (2000). *A new form of the Kerr solution.* Phys. Rev. D 61, 067503. doi:10.1103/PhysRevD.61.067503, arXiv:gr-qc/9910099. Eq. 4 covers Schwarzschild and eq. 22 Kerr, in Doran's own coordinates, not Boyer–Lindquist.
- Hamilton, A. J. S. & Lisle, J. P. (2008). *The river model of black holes.* Am. J. Phys. 76, 519. doi:10.1119/1.2830526, arXiv:gr-qc/0411060
- Bardeen, Press & Teukolsky (1972), see §1, for the circular orbits.

## 4. Clock rates: gravitational and kinematic time dilation

The observer's clock against Boyer–Lindquist time t, which is the clock of a distant static observer, runs
at (`ProperTimeRate`):

    dτ/dt = 1 / uᵗ = α / γ

- **Gravitational dilation.** α is the depth in the potential well, including frame dragging. This is the
  only factor for the ZAMO.
- **Kinematic dilation.** 1/γ is the motion relative to the local ZAMO.

The tests compare this rate with three closed forms:

- **Static observer in Schwarzschild:** dτ/dt = √(1 − 2M/r).
- **Circular orbit in Kerr:** the Bardeen–Press–Teukolsky form

      uᵗ = (r^{3/2} ± a) / (r^{3/4} √(r^{3/2} − 3 r^{1/2} ± 2a))

- **ZAMO:** the rate is α.

This is the mechanism behind the time dilation on Miller's planet in the release goal. Close to Gargantua
the Orbiting rate α/γ becomes very small. The values for the film's planet are a test case for M6.

## 5. What a moving camera sees: aberration and Doppler shift

The camera's pixel directions are directions in its own rest frame. The integrated tracer launches photons
from the ZAMO frame, so each pixel direction is converted to the ZAMO at the camera position. The two
observers are at the same event, so this is an exact special-relativistic Lorentz transformation and needs no
approximation (`BoostPhoton`, `boostPhoton`).

For a photon with unit propagation direction **k′** in the camera frame, where the camera moves with **v**
relative to the ZAMO:

    ε_ZAMO / ε_cam = γ (1 + v·k′)
    k_ZAMO = (k′ + (γ²/(γ+1) (v·k′) + γ) v) / (γ (1 + v·k′))

Along the direction of motion this reduces to cos θ = (cos θ′ + v)/(1 + v cos θ′). The tests check this
formula against three references:
- the textbook aberration formula
- eq. A.9 of James et al. 2015, the formula used for the *Interstellar* camera, with their sign convention:
  their N points towards the source
- a photon built in the boosted tetrad and measured by the ZAMO

Effects in the image (`camera.observer`, Integrated transport only):

- **Aberration.** The sky crowds towards the direction of motion and the region behind spreads out. The
  black hole shadow shrinks when the camera moves towards the hole and grows when it moves away. James et al.
  2015 show this in their §3.5 / Fig. 12, which compares a geodesic-orbit camera, a ZAMO camera and a static
  camera.
- **Doppler shift of the disk.** The photon energy the camera measures (`cameraEnergy` in `rk4RayMarching`)
  is divided by γ(1 + v·k′). So the disk's colour and its g⁴ brightness include the camera's own motion as
  well as the gas's. With the Doppler toggle off, the camera's part is switched off together with the gas's.
- **Rendered check.** In `templates/sgr-a-star.yaml` the camera is at r ≈ 32 M, θ ≈ 62°. The Orbiting
  observer moves there at v = 0.160 c relative to the ZAMO. The first-order aberration of the direction to
  the hole predicts that the shadow centre moves 50.7 px (640×360, 60° field of view). In the export, the
  centroid of the shadow moves by 51.2 px, in the same direction.
- **Fallback.** Where the chosen observer cannot exist, `FillObserverData` falls back to the ZAMO.
- **Unchanged ZAMO rendering.** With the ZAMO selected, the shader skips the transformation entirely. The
  output is byte-identical to the renderer before stage 2: an export of `templates/sgr-a-star.yaml` with the
  disk frozen (`disk.speed=0`, `disk.hot_spots=0`) and with the disk off was compared with `cmp` before and
  after the change.

Sources:
- Einstein, A. (1905). *Zur Elektrodynamik bewegter Körper.* Ann. Phys. 17, 891. §7 covers the Doppler principle and aberration.
- Rybicki, G. B. & Lightman, A. P. (1979). *Radiative Processes in Astrophysics*, ch. 4.
- Weiskopf, D., Kraus, U. & Ruder, H. (1999). *Searchlight and Doppler effects in the visualization of special relativity: a corrected derivation of the transformation of radiance.* ACM Trans. Graph. 18(3), 278. doi:10.1145/336414.336459
- Müller, T. & Boblest, S. (2014). *Visual appearance of wireframe objects in special relativity.* Eur. J. Phys. 35, 065025. doi:10.1088/0143-0807/35/6/065025, arXiv:1410.4583
- James et al. (2015), see §1: §2.1 (camera set-up), App. A eqs. A.7–A.11, §3.5 and Fig. 12.

## 6. Proper acceleration: what an accelerometer on board reads

Only free-falling observers feel weightless. Every other observer needs thrust, and its proper acceleration is
the magnitude of its 4-acceleration a^μ = u^ν ∇_ν u^μ (`ProperAcceleration`, `KillingObserverAcceleration`,
`ZamoAcceleration`):

- **Killing observers.** Static observers, and rigidly rotating ones with constant Ω, move along a Killing
  vector χ = ∂_t + Ω ∂_φ. Their acceleration is the gradient of the redshift factor:
  a_μ = ∇_μ ln |χ|, with |χ| = √(−(g_tt + 2Ω g_tφ + Ω² g_φφ)) at fixed Ω. For the static observer in
  Schwarzschild this gives the classic a = M / (r² √(1 − 2M/r)), which diverges at the horizon. The tests check
  it.
- **ZAMOs.** ω varies from place to place, so the ZAMO is not a Killing observer. Its acceleration is the
  gradient of the lapse: a = ∇ ln α. In Schwarzschild it coincides with the static observer.
- **Free fall.** Circular geodesics in the equatorial plane and raindrops have a = 0. The tests confirm this
  numerically for the Killing formula at the Keplerian Ω, where ∂_r |χ| = 0 is exactly the circular-orbit
  condition.
- **Off-plane Orbiting.** Off the equatorial plane the Orbiting observer needs thrust towards the plane.

The magnitude |a| = √(g^rr a_r² + g^θθ a_θ²) uses central differences of the scalar. It is converted to SI
with c²/r_g, and the camera window shows it in units of Earth's surface gravity, GM⊕/R⊕² from `GPP::Units`.

Sources (no textbook location verified yet; check before citing in the paper):
- Wald, R. M. (1984). *General Relativity.* University of Chicago Press, §6.3 (static observers, a = ∇ ln V; from memory).
- Thorne, K. S., Price, R. H. & Macdonald, D. A. (1986). *Black Holes: The Membrane Paradigm.* Yale University Press (FIDO acceleration a = ∇ ln α; from memory).
- Macdonald, D. & Thorne, K. S. (1982). *Black-hole electrodynamics: an absolute-space/universal-time formulation.* MNRAS 198, 345 (from memory; not found on Crossref).
- Abramowicz, M. A. & Prasanna, A. R. (1990). *Centrifugal force reversal near a Schwarzschild black hole.* MNRAS 245, 720. The behaviour of the acceleration of rotating observers; whether it states the ∇ ln |χ| form is not verified.

## 7. The camera window's Observer section

The camera window shows the following for the observer the image is rendered from
(`ComputeObserverReadout` in `src/simulation/observer_readout.cppm`, published by the viewport each frame):
- r/M
- the speed relative to the ZAMO and γ
- dτ/dt, and "1 h here = X far away"
- the proper acceleration
- for a camera riding a Geodesic Body, its proper time

It also reports a fallback to the ZAMO, and notes when the transport ignores the observer. The readout and
the renderer apply the same rules (`BodyObserverVelocity`, `FillObserverFromBody`, `FillObserverData`), so the
numbers always belong to the image.

## Limitations

- **Sky background.** The skybox is aberrated but not colour- or brightness-shifted. It is an RGB image
  with no spectrum, and the stationary renderer did not shift it either. Correct searchlight beaming of the
  sky needs a spectral sky (Weiskopf et al. 1999).
- **First hole only.** The observer is defined relative to the first black hole, the one the integrated
  tracer uses.
- **Third-person mode.** The observer is placed at the camera position, not at the offset third-person ray
  origin.
- **Integrated transport only.** The analytic-lens and straight transports keep the static camera.

---

*The references were checked against Crossref, the arXiv texts or the publishers' pages, except where marked
"not verified". Check those section and equation numbers in a printed copy before citing them in the paper.*
