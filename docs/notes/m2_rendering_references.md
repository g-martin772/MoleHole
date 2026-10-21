# M2 rendering references

Reference note for the rendering work of milestone M2: texture level of detail without derivatives, diffuse sky
lighting with spherical harmonics, a physically based atmosphere, observer frames for surface elements, heightfield
marching on spheres, and spectral shifting of RGB textures. Each section gives the formulas the implementation needs,
says where they were checked, and marks what was not.

Conventions follow `geodesics.md` and `observers.md`: G = c = 1, Boyer–Lindquist (t, r, θ, φ), signature (−+++).
In the graphics sections θ is a polar angle from +z unless stated otherwise.

Verification key: "checked" means read in the original paper text or in the author's reference code in this session.
"Not verified" means written from memory or secondary descriptions; check against a printed copy before citing.

---

## 1. Ray cones for texture LOD

A ray cone carries a footprint width w and a spread angle α along a ray. It gives a texture LOD without screen-space
derivatives, which a compute-shader ray marcher does not have.

**Pixel spread angle** (RTG 2019 eq. 30, checked):

    α_pix = arctan( 2 tan(ψ/2) / H )

ψ is the vertical field of view, H the image height in pixels. One value is used for all pixels (it is exact for the
centre pixel only). Non-square pixels or a wide field of view need the per-pixel angle,
|d̂(x+1,y) − d̂(x,y)|, which the 2021 paper uses for its local right and up vectors.

**Width growth** (RTG eqs. 25–29, checked). At the first hit, with d₀ the camera-to-hit vector:

    w₀ = 2 ‖d₀‖ tan(α/2) ≈ α ‖d₀‖

After a reflection with surface spread angle β and distance t₁ to the next hit:

    w₁ ≈ w₀ + (α + β) t₁       and the new spread is α₁ = α + β

For a ray with no reflection (the camera ray leaving the observer into the sky), w(t) = w₀ + α t with β = 0.

**LOD** (RTG eq. 26, 2021 eq. 1, checked):

    λ = Δ + log2|W| − log2|n̂·d̂|

with W the cone width at the hit (the 2021 paper writes |W|). The three terms are:
- Δ is the base mip level of the triangle at z = 1. For a triangle, Δ = ½ log2(t_a / p_a), where t_a = w·h·|texture-space
  area×2| and p_a = ‖(P₁−P₀)×(P₂−P₀)‖ (2021 eq. 2).
- log2|W| is the distance term.
- −log2|n̂·d̂| is the cos term. The footprint on the surface is stretched by 1/|cos| at grazing angles, so the LOD
  rises as the angle between −d̂ and n̂ approaches π/2. For a sky at infinity there is no surface normal, and the term drops.

Which trigonometric form RTG eq. 25 prints (the text extraction showed a cross product, the 2021 paper prints the dot
product) is Not verified. Use the 2021 eq. 1.

**Many textures.** Keep the resolution out of Δ (2021 §6):

    λ_t = ½ log2(tt / p_a) + log2|W| − log2|n̂·d̂|,     λ_i = λ_t + ½ log2(w_i h_i)

For a texture mapped by a closed form (the sphere-map case below) there is no triangle, and you compute
the footprint in texel units directly.

**Curvature** (2021 eq. 5 and 7, checked in outline). The spread change from surface curvature k is
β_c = −2k |w| / (n̂·d̂), with k estimated per edge as k₀₁ = (n̂₁−n̂₀)·(P₁−P₀) / ((P₁−P₀)·(P₁−P₀)) (Reed 2014).
The roughness term β_r is irrelevant for a pure mirror/sky lookup.

### 1.1 Equirectangular sphere maps

These are derived here from the ray-cone footprint and are not printed in the papers.

For a sky or sphere texture W×H with W = 2H, u = φ/2π, v = θ/π. A texel spans 2π/W in longitude and π/H = 2π/W in
colatitude, so it is square only on the equator. Its angular size along φ is (2π/W) sinθ.

For a sky at infinity there is no cos term and no distance term. The footprint is the cone's angle (the spread α
at that ray), and

    λ_v = log2( α W / 2π )                    (along θ; this is the finest level that does not alias in v)
    λ_u = log2( α W / (2π sinθ) )            (along φ)
    λ   = log2( α W / (2π) ) − log2(sinθ)   if the longer footprint axis is used (conservative)
    λ   = log2( α W / (2π) )                if the shorter axis is used (sharp, aliases in u near the poles)

If you want the isotropic compromise, take λ = log2(α W/(2π)) − ½ log2(sinθ) (the geometric mean). Near the poles
1/sinθ diverges. Clamp sinθ to a small ε (for example one texel row, sinθ_min = π/(2H)) and clamp λ to the
coarsest level. A footprint that is 1/sinθ times longer in u than in v is anisotropic, and a trilinear tap in one mip
level blurs it wrongly. The honest fix is several taps along u, or accepting the clamp.

For a lensed ray the cone must be the footprint of the *bundle* arriving at the sky, not the camera pixel
angle. Gravitational lensing magnifies and shrinks it, and α is not constant along the geodesic. Options are the
footprint from a neighbouring ray (one extra geodesic per pixel, as in Interstellar's ray bundles, James et al.
2015 §3), or geodesic deviation. This is my own observation, not from the ray-cone papers.

**Seam (φ = ±π, u = 0/1).** A ray cone needs no UV derivative, so the discontinuity that makes hardware
`textureGrad`/automatic LOD pick the wrong mip at the seam (neighbouring pixels differ by ~1 in u) does not occur.
You still need:
- address mode REPEAT in u, so bilinear taps and mip filtering wrap;
- address mode CLAMP_TO_EDGE in v;
- mip generation that also wraps in u.

**Poles.** Texel solid angle ∝ sinθ, so a 2×2 box-filtered mip level averages texels of unequal solid angle,
which is slightly wrong near the poles. It is a minor effect compared with the u anisotropy above. Row-wise
weighting by sinθ_j during mip generation removes it. Not verified in the literature; I did not find a
reference.

---

## 2. Spherical-harmonic irradiance

Checked against the original (Ramamoorthi & Hanrahan 2001, SIGGRAPH, 4-page paper, equations numbered as printed).

**Coordinates.** (x, y, z) = (sinθ cosφ, sinθ sinφ, cosθ).

**The nine real basis functions** (eq. 3):

    Y00           = 0.282095                 = ½ √(1/π)
    Y1−1, Y10, Y11 = 0.488603 · (y, z, x)    = ½ √(3/π) · (y, z, x)
    Y2−2, Y2−1, Y21 = 1.092548 · (xy, yz, xz) = ½ √(15/π) · (xy, yz, xz)
    Y20           = 0.315392 · (3z² − 1)     = ¼ √(5/π) · (3z² − 1)
    Y22           = 0.546274 · (x² − y²)     = ¼ √(15/π) · (x² − y²)

The closed forms on the right are the standard real SH normalisation. The numerical values are checked. The
closed forms were not checked against the paper (they follow from the values).

**Projection** (eq. 10):

    L_lm = ∫_{θ=0}^{π} ∫_{φ=0}^{2π} L(θ,φ) Y_lm(θ,φ) sinθ dθ dφ

For an equirectangular image W×H with pixel (i, j), j = 0 at the top (the +z pole):

    θ_j = (j + ½) π/H,   φ_i = (i + ½) 2π/W,   dΩ_j = (2π/W)(π/H) sinθ_j
    L_lm ≈ Σ_{i,j} L_ij Y_lm(θ_j, φ_i) dΩ_j

The sinθ weight is essential; without it the poles dominate. An exact row weight is (2π/W)(cosθ_j^top − cosθ_j^bottom)
= (2π/W) · 2 sin(π/2H) sinθ_j, which differs from the midpoint rule at O(1/H²). Check: Σ dΩ ≈ 4π, and with L ≡ 1 the
result must be L00 = √(4π) = 3.5449, all others 0. Each colour channel is separate. The paper's z axis is the
polar axis. A map whose up axis is y or whose longitude origin differs needs the same axis permutation as the
sampling code (see the project's sky convention in `shaders/`).

**Convolution with the clamped cosine** (eqs. 5, 6, 8, 9):

    E_lm = Â_l L_lm,   Â_l = √(4π/(2l+1)) A_l

    Â_0 = π = 3.141593      Â_1 = 2π/3 = 2.094395      Â_2 = π/4 = 0.785398

(Â_3 = 0, Â_4 = −0.1309, Â_5 = 0, Â_6 = 0.049087 are not needed; Â_l vanishes for odd l > 1.)

    E(n) = Σ_{l≤2, m} Â_l L_lm Y_lm(n)       (irradiance, with the π in Â_0 included)
    outgoing Lambertian radiance = ρ E(n) / π

The paper's eq. 2, B = ρ E, calls B the radiosity (radiant exitance); divide by π for radiance. Error of the
9-term fit: under 3% on average, at most 9% per pixel in the paper's tests (the "maximum error under 5%" quoted
in the text refers to its comparison figure; I did not re-derive which figure). Not verified in detail.

**Quadratic form for shaders** (eqs. 12 and 13, checked). Constants:

    c1 = 0.429043   (= Â2 · 0.546274)
    c2 = 0.511664   (= Â1 · 0.488603)
    c3 = 0.743125   (= Â2 · 0.315392 · 3)
    c4 = 0.886227   (= Â0 · 0.282095)
    c5 = 0.247708   (= Â2 · 0.315392)

    E(n) = c1 L22 (x² − y²) + c3 L20 z² + c4 L00 − c5 L20
         + 2 c1 (L2−2 xy + L21 xz + L2−1 yz)
         + 2 c2 (L11 x + L1−1 y + L10 z)

with n = (x, y, z) a unit normal. The identities in parentheses were checked numerically here (for example
0.785398 · 0.546274 = 0.42904).

---

## 3. Physically based atmosphere

### 3.1 Media and phase functions (Hillaire 2020 §4, checked)

Densities (h is altitude):

    d_R(h) = exp(−h / 8 km)      d_M(h) = exp(−h / 1.2 km)      d_O(h) = max(0, 1 − |h − 25 km| / 15 km)

Earth coefficients (Table 1, ×10⁻⁶ m⁻¹, RGB), from Bruneton's 2017 implementation:

| Medium   | Scattering σ_s       | Absorption σ_a        |
|----------|----------------------|-----------------------|
| Rayleigh | 5.802, 13.558, 33.1  | 0                     |
| Mie      | 3.996                | 4.40                  |
| Ozone    | 0                    | 0.650, 1.881, 0.085   |

Ozone absorbs only; it is what keeps the sky blue at sunset. Extinction σ_t = σ_s + σ_a and
transmittance T(a,b) = exp(−∫ σ_t ds).

**Phase functions.** θ is the angle between the incident and the scattered propagation directions, ν = cosθ.

    Rayleigh:            p_R(θ)    = 3 (1 + cos²θ) / (16π)
    Cornette–Shanks:     p_M(θ, g) = 3/(8π) · (1 − g²)(1 + cos²θ) / [ (2 + g²)(1 + g² − 2g cosθ)^(3/2) ]     (default g = 0.8)
    Henyey–Greenstein:   p_HG(θ,g) = 1/(4π) · (1 − g²) / (1 + g² − 2g cosθ)^(3/2)
    isotropic:           p_u       = 1/(4π)

The Rayleigh and Cornette–Shanks forms are checked in Hillaire and in Bruneton's 2017 code (where k_R = 3/(16π) and
k_M = 3/(8π)·(1−g²)/(2+g²)). Hillaire notes that HG is an acceptable simpler replacement. The HG formula is the standard
one; I did not re-check it against Henyey & Greenstein 1941 (listed below). The Cornette–Shanks
form is Cornette & Shanks (1992) as cited by Hillaire (the paper cites it as [GK99]); the original was not read.
g > 0 means forward scattering, with the convention above.

### 3.2 Transmittance LUT

Hillaire stores T in the representation of Bruneton & Neyret 2008 §4 (256×64 in his tests). The exact mapping in
Bruneton's 2017 reference implementation (checked in the author's HTML-rendered code) is, with r_top and r_bot the
atmosphere and planet radii, r the radius of the point, μ the cosine of the view zenith angle:

    H     = √(r_top² − r_bot²)               (horizon distance from the top)
    ρ     = √(r² − r_bot²)                   (horizon distance from r)
    d     = max(−r μ + √(r² (μ² − 1) + r_top²), 0)     (distance to the top boundary)
    d_min = r_top − r,   d_max = ρ + H
    x_μ   = (d − d_min) / (d_max − d_min),   x_r = ρ / H
    u     = 0.5/W + x_μ (1 − 1/W),   v = 0.5/H_tex + x_r (1 − 1/H_tex)         (half-texel offset)

Inverse: ρ = H x_r, r = √(ρ² + r_bot²), d = d_min + x_μ (d_max − d_min), μ = (H² − ρ² − d²) / (2 r d) (μ = 1 if
d = 0), clamped to [−1, 1]. The 2008 paper's own mapping differs in detail from this one; Hillaire says "the same
representation" and his code follows the 2017 form. The exact 2008 formula was not checked.

### 3.3 Multiple-scattering LUT (Hillaire 2020 §5.5, eqs. 5–11, checked)

The aim is an O(1), scattering-order-independent term. Assumptions: the planet is a sphere, density depends only on
altitude, light scattered more than once is isotropic (p_u = 1/4π), and the light arriving at all points
around the shaded point x_s equals that at x_s. Visibility is ignored.

Second-order luminance towards x_s, gathered over the sphere of directions Ω_4π:

    L_2nd(x_s) = ∫_{Ω4π} L₀(x_s, −ω) p_u dω                                         (5)
    L₀(x, v) = T(x,p) L_o(p,v) + ∫₀^{‖p−x‖} σ_s(x) T(x, x−tv) S(x, ω_s) p_u E_I dt   (6)

where p is the surface point hit by the ray (planet ground, with L_o the ground luminance) or the atmosphere
top, S the shadow factor of the planet, E_I the sun illuminance. Transfer function of the neighbourhood (unitless,
in [0, 1]):

    f_ms = ∫_{Ω4π} L_f(x_s, −ω) p_u dω                                              (7)
    L_f(x, v) = ∫₀^{‖p−x‖} σ_s(x) T(x, x−tv) · 1 dt                                  (8)

L_f skips the shadow factor and the phase function (already inside L_2nd). Each ray order multiplies by f_ms, so the
geometric series is

    F_ms = 1 + f_ms + f_ms² + f_ms³ + … = 1 / (1 − f_ms)                              (9)
    Ψ_ms = L_2nd · F_ms                                                              (10)

Ψ_ms has unit sr⁻¹ and is multiplied by the light illuminance. The shading equation becomes

    L_scat(c, x, v) = σ_s(x) Σ_i ( T(c,x) S(x,l_i) p(v,l_i) + Ψ_ms ) E_i             (11)

(In the paper σ_s is the sum over media, and Ψ_ms is stored once for the combined medium.) Hillaire integrates
f_ms and L_2nd with 64 uniformly distributed directions per texel, and recommends the analytic in-segment
integral of the scattering along each ray (Hillaire 2015) to keep f_ms < 1. LUT parameterisation (eq. in §5.5.2):

    u = 0.5 + 0.5 cos θ_s     (θ_s the sun zenith angle)
    v = clamp( (h − R_ground) / (R_top − R_ground), 0, 1 )

Resolution 32×32, 20 steps per ray (Table 2; the "32²" is the extracted text of the table and is probably 32×32). Since 1/(1−f_ms) diverges as f_ms → 1, clamp f_ms below 1
(very dense atmospheres) and note that the method degrades for strongly anisotropic phase functions and
for atmospheres much thicker than Earth's, as Hillaire's Fig. 12 and §9 discuss.

### 3.4 Sky-view LUT (§5.3, checked)

A latitude/longitude image of the sky, oriented to the local up vector at the camera so the horizon is a horizontal
line. Latitude l ∈ [−π/2, π/2] is compressed towards the horizon:

    v = 0.5 + 0.5 · sign(l) · √( |l| / (π/2) )

Longitude is relative to the sun azimuth. Resolution 200×100 with 30 steps on PC (96×50 with 8 steps on mobile).
The LUT is valid for one camera altitude; it is rebuilt when the altitude changes, and the optional volumetric
shadow mode skips it. The aerial perspective volume is 32³ (32×32 over the screen and 32 depth slices).
The longitude convention and whether the sun azimuth is the u origin are from the UE implementation and were
not checked in the paper.

For an observer orbiting a black hole, the atmosphere would be that of a planet or a star in the scene; the sky-view LUT
assumes a camera inside the shell, not a ray that starts far outside.

---

## 4. Local frames, local patches and the Doppler factor

### 4.1 Tetrads (cf. `observers.md`)

ZAMO tetrad (Bardeen, Press & Teukolsky 1972; formulas as in `observers.md` §1):

    e_t̂ = (∂_t + ω ∂_φ)/α,  e_r̂ = √(Δ/Σ) ∂_r,  e_θ̂ = ∂_θ/√Σ,  e_φ̂ = ∂_φ/ϖ

The dual one-forms (coframe) are what map coordinate displacements to local proper displacements:

    e^t̂ = α dt        e^r̂ = √(Σ/Δ) dr       e^θ̂ = √Σ dθ       e^φ̂ = ϖ (dφ − ω dt)

so for a displacement dx^μ,

    dx^â = e^â_μ dx^μ

and for a displacement at equal coordinate time (dt = 0): (dr̂, dθ̂, dφ̂) = (√(Σ/Δ) dr, √Σ dθ, ϖ dφ).
These follow directly from the metric −α² dt² + (Σ/Δ) dr² + Σ dθ² + ϖ² (dφ − ω dt)² (checked as the inverse of the tetrad).

**Boost to an orbiting observer** with velocity v relative to the ZAMO (`observers.md` §2):

    u = γ (e_t̂ + vⁱ e_î)
    e_j = γ v_j e_t̂ + e_ĵ + (γ − 1) v_j vᵏ e_k̂ / v²

For a displacement (dt̂, dx̂) in the ZAMO frame the displacement in the observer's frame is the Lorentz transform

    dt' = γ (dt̂ − v·dx̂)
    dx'_∥ = γ (dx̂_∥ − v dt̂),    dx'_⊥ = dx̂_⊥

Two events at equal coordinate time (dt = 0) have dt̂ = 0 for the ZAMO but dt' = −γ v·dx̂ ≠ 0 for the moving
observer. Simultaneity differs, so a surface patch that is static in the ZAMO frame appears at different places to the
orbiting observer. That is the aberration and length contraction already used in the camera.

### 4.2 Validity of a locally flat patch (Fermi normal coordinates)

Along a geodesic, in Fermi normal coordinates (Manasse & Misner 1963; MTW §13.6)

    g_âb̂ = η_âb̂ − (1/3) R_âĉb̂d̂ x^ĉ x^d̂ + O(x³)

and for an accelerated observer (Fermi–Walker transport) the time–time part also has a term from the
acceleration, g_t̂t̂ = −(1 + a·x)² + …. So over a patch of size L the metric differs from Minkowski by

    δg ~ R L²,     R ~ M/r³         (Schwarzschild tidal eigenvalues are −2M/r³ radial and +M/r³ transverse;
                                     the Kretschmann invariant is 48 M²/r⁶, so |R| ≲ √48 M/r³ ≈ 6.9 M/r³)

For δg < ε, require L < √(ε / R) ≈ √(ε r³ / M). At r = 6M with ε = 10⁻²: L ≲ 0.1·√216 M ≈ 1.5 M. For Kerr the
components also depend on a, but the order stays M/r³ away from the ring singularity and the horizon region (the
tidal scale grows near r → r₊ in Kerr coordinates; not computed here). The acceleration term needs the observer to
be accelerated (a hovering or powered observer, not an orbiting geodesic one).

The quadratic form of the correction is the standard Fermi-normal-coordinate expansion, quoted from memory (MTW eq. 13.73 is
Not verified). The numeric thresholds are my own estimates.

### 4.3 Frequency ratio and intensity invariant

A photon with 4-momentum p has frequency ν = −p·u as measured by an observer u. For an emitter u_em and an observer u_obs,

    g ≡ ν_obs / ν_em = (p·u_obs) / (p·u_em)

(both signs negative, so the ratio is positive). In Kerr with a conserved E = −p_t and L = p_φ, for a ZAMO-based
emitter u_em = γ(e_t̂ + v·e_î) this gives the known searchlight form (cf. `disk_rendering.md` and `observers.md` §3).

The quantity I_ν/ν³ is a Lorentz invariant along a ray (Liouville's theorem: the occupation number f = I_ν c²/(2hν³)
is conserved). Hence

    I_obs(ν_obs) = g³ I_em(ν_em),        ν_em = ν_obs / g

and, integrating over frequency (dν_obs = g dν_em),

    I_obs,bol = ∫ I_obs dν_obs = g⁴ I_em,bol

For a surface of a given temperature T the Planck spectrum shifts to temperature gT and I_bol scales as g⁴ (Stefan–Boltzmann).
For the spectral radiance of a reflected-sunlight surface, the surface's own motion matters twice: the light that
hits it is shifted by g_1 (from the source to the surface), and the light that leaves it is shifted by g_2 (surface to
camera). The two Doppler factors are not each other's inverse unless the surface is at rest in both frames.

Sources for the invariant are Rybicki & Lightman (1979) (the invariance of I_ν/ν³ is derived in their chapter on radiative transfer / special relativity) and Misner, Thorne & Wheeler (1973), §22.6 on the photon distribution function. The exact
section numbers are Not verified.

---

## 5. Heightfield ray marching on spheres

### 5.1 Cone step mapping (Dummer 2006; checked in the author's short paper)

For each texel the preprocessing stores a cone whose tip sits on the surface at that texel. Define cone_ratio =
radius / height of the widest cone that stays above the heightfield (tan of its half angle). Cone_ratio lies in (0, 1).
The cone line in 2D is cone_ratio · y − x = 0. For a ray direction dS with vertical component z (downwards, in
[0,1]) and horizontal component iz = √(1 − z²), starting at height ht above the surface at this texel,

    Step_ratio = cone_ratio / (iz − z · cone_ratio)  =  1 / (iz / cone_ratio − z)
    ds = ht · Step_ratio

Stepping by ds never passes through the surface. A ray pointing straight down arrives in one step, grazing rays need
many. The paper stores √cone_ratio, since most ratios are small. The preprocessor searches outwards in expanding
squares for the steepest cone, O(N²) per texel worst case. Dummer's variants: fixed iteration count, loop to a count with
an additional 1/texture_size step, and exact (variable). Relaxed cone stepping (Policarpo & Oliveira, GPU Gems 3
ch. 18) is a variant; I did not read it.

### 5.2 Maximum mipmaps (Tevs, Ihrke, Seidel 2008)

Not verified against the paper text (the PDF was not reachable; the description comes from the abstract and from
secondary summaries). The data structure is a mip pyramid in which each texel stores the **maximum** of
the heights of its 2×2 children, so every texel is an upper bound over its footprint. Traversal is hierarchical: at a given
level the ray's segment over the current cell is compared with the stored maximum; if the segment stays above the
maximum the ray leaves the cell without a hit, otherwise it descends one level; at the finest level a
linear/bilinear intersection is done. Precomputation is a few downsample passes, so dynamic heightfields are cheap, unlike
cone maps. Check the exact rules for moving to the next cell and for going up a level in the paper
before implementing.

### 5.3 Adaptation to a sphere (my own derivation; not from either paper)

For a spherical shell r = R + h(θ,φ), with h in [0, h_max] over an equirectangular grid:
- Ray–sphere: intersect the ray with the outer sphere R + h_max to find the entry point, as the paper's bounding box does.
- Replace "ray height above cell max" by "minimum radius of the ray over the cell's angular extent versus R + max h
  in the cell". For a straight ray the radius along the ray is r(s)² = r_c² + (s − s_c)², with r_c the closest-approach
  radius, so the minimum over a segment is analytic (r_c if the closest approach lies inside the segment, else the
  nearer endpoint). This is a conservative test.
- With cone maps the "slope" must be measured with angular distances on a sphere, ratio = R-scaled arc length / height, with an
  arc-length scale factor that depends on latitude: a texel's φ extent is R sinθ Δφ. Near the poles the isotropic
  cones are too small in φ; the max mip pyramid has the same problem. Use cells with a latitude-dependent width, or
  a cube map / geodesic grid for the heights.
- Curved light: in this renderer the ray is a geodesic of Kerr, not a straight line. The surface is bounded by a thin
  shell and the segment of the geodesic over a cell can be approximated as straight when the step is short compared with
  the curvature radius (r³/M scale from §4.2).

---

## 6. Spectral upsampling (Jakob & Hanika 2019, checked)

The model gives, for each RGB value, a smooth reflectance spectrum in [0, 1] whose projection under a given
illuminant reproduces the RGB:

    f(λ) = S( c0 λ² + c1 λ + c2 ),        S(x) = 1/2 + x / (2 √(1 + x²))             (eqs. 2, 3)

The sigmoid S maps (−∞, ∞) to (0, 1), so the spectrum is automatically physical (energy conserving). The coefficients
c = (c0, c1, c2) are found offline by Gauss–Newton optimisation of the CIELAB distance between the target RGB
and the RGB of f, and stored in 3 cubes of 64³ entries (9 MiB for sRGB), indexed by a warped RGB lookup in which one axis is the
maximum component α. Darker colours produce smoother spectra. At run time: three trilinear lookups from the table, then six
floating-point operations per wavelength. The RGB-space coefficients are for one RGB space and one illuminant.
For fragment shaders, `rgb2spec` (github.com/mitsuba-renderer/rgb2spec) generates the table.

How it applies to Doppler shifting of reflected light:
- Texture colour (albedo) becomes a reflectance spectrum R(λ) = S(c·(λ, 1)).
- The observed spectrum is R(λ_obs / g) · E_source(λ_obs / g) · g³ (wavelength remapped by the Doppler factor g,
  and the radiance scaling from the I_ν/ν³ invariant of §4.3) integrated against the sensor/CIE curves to give RGB. Under the
  g-shift this is a ν-space statement; mind the Jacobian between I_λ and I_ν.
- The result is neither the RGB colour times g nor a simple hue rotation. Light shifted into the infra-red or ultraviolet
  drops out of the visible range, and the band edge of the sigmoid (spectra with values at 0 or 1 at the extremes)
  controls what enters.
- The model is defined only for the range of wavelengths covered by the CIE curves (roughly 360–830 nm) and for
  reflectances of non-emissive surfaces. For an emissive texture (a light source) the paper's note on illuminant
  spectra applies: the upsampled spectrum is a reflectance, and must be multiplied by an illuminant spectrum (D65 for
  sRGB white).
- The cost is per-pixel: evaluating at several wavelengths, then integrating against the colour matching functions (a
  few tens of samples or a precomputed 3×N matrix). A cheaper approximation precomputes the shifted RGB as a function of
  (RGB, g) in a 4D lookup, which I have not seen in the literature.

The last two bullets of the list above and the cost remarks are my own reading, not statements from the paper.

---

## Limitations of this note

- Section 1: the 2019 chapter was read as text (eqs. 25–30); the sin/cos form of the grazing-angle term is flagged above.
  The equirectangular analysis is derived here.
- Sections 4.2, 4.3, 5.2 and 5.3 contain statements marked "Not verified" or "my own derivation". Treat them as plans to
  check, not as citations.
- Dummer's paper is the short unpublished write-up (2006); there is no DOI.

---

## Sources

- Akenine-Möller, T., Nilsson, J., Andersson, M., Barré-Brisebois, C., Toth, R. & Karis, T. (2019). *Texture Level of Detail Strategies for Real-Time Ray Tracing.* In *Ray Tracing Gems*, ch. 20, pp. 321–345. Apress. doi:10.1007/978-1-4842-4427-2_20. (Crossref lists the first four authors; the full author list was not checked.)
- Akenine-Möller, T., Crassin, C., Boksansky, J., Belcour, L., Panteleev, A. & Wright, O. (2021). *Improved Shader and Texture Level of Detail Using Ray Cones.* Journal of Computer Graphics Techniques 10(1), 1–24. jcgt.org/published/0010/01/01/. (No DOI checked; page range not checked.)
- Ramamoorthi, R. & Hanrahan, P. (2001). *An Efficient Representation for Irradiance Environment Maps.* Proc. SIGGRAPH 2001, 497–500. doi:10.1145/383259.383317
- Hillaire, S. (2020). *A Scalable and Production Ready Sky and Atmosphere Rendering Technique.* Computer Graphics Forum 39(4), 13–22 (EGSR 2020). doi:10.1111/cgf.14050
- Hillaire, S. (2015). *Physically Based and Unified Volumetric Rendering in Frostbite.* SIGGRAPH Courses (cited by the 2020 paper for the analytic segment integration; not read).
- Bruneton, E. & Neyret, F. (2008). *Precomputed Atmospheric Scattering.* Computer Graphics Forum 27(4), 1079–1086. doi:10.1111/j.1467-8659.2008.01245.x
- Bruneton, E. (2017). *A Qualitative and Quantitative Evaluation of 8 Clear Sky Models.* IEEE TVCG 23(12), 2641–2655. doi:10.1109/TVCG.2016.2622272 (DOI not checked). Reference implementation: ebruneton.github.io/precomputed_atmospheric_scattering/ (functions.glsl, checked).
- Cornette, W. M. & Shanks, J. G. (1992). *Physical reasons for the phase function of Mie scattering.* Applied Optics 31(16), 3152–3160 (not read; DOI not checked).
- Henyey, L. G. & Greenstein, J. L. (1941). *Diffuse radiation in the Galaxy.* ApJ 93, 70–83. doi:10.1086/144246 (not read; DOI not checked).
- Bardeen, J. M., Press, W. H. & Teukolsky, S. A. (1972). *Rotating black holes: locally nonrotating frames, energy extraction, and scalar synchrotron radiation.* ApJ 178, 347. doi:10.1086/151796
- James, O., von Tunzelmann, E., Franklin, P. & Thorne, K. S. (2015). *Gravitational lensing by spinning black holes in astrophysics, and in the movie Interstellar.* Class. Quantum Grav. 32, 065001. doi:10.1088/0264-9381/32/6/065001, arXiv:1502.03808.
- Manasse, F. K. & Misner, C. W. (1963). *Fermi normal coordinates and some basic concepts in differential geometry.* J. Math. Phys. 4, 735. doi:10.1063/1.1724316 (DOI not checked; not read).
- Misner, C. W., Thorne, K. S. & Wheeler, J. A. (1973). *Gravitation.* W. H. Freeman. §13.6 (Fermi normal coordinates) and §22.6 (not verified).
- Rybicki, G. B. & Lightman, A. P. (1979). *Radiative Processes in Astrophysics.* Wiley. (Invariance of I_ν/ν³; section not verified.)
- Dummer, J. (2006). *Cone Step Mapping: An Iterative Ray-Heightfield Intersection Algorithm.* Unpublished technical note (lonesock.net). Read via faculty.digipen.edu/~gherron/references/References/SurfaceDetail/ConeStepMapping.pdf.
- Tevs, A., Ihrke, I. & Seidel, H.-P. (2008). *Maximum Mipmaps for Fast, Accurate, and Scalable Dynamic Height Field Rendering.* Proc. I3D 2008, 183–190. doi:10.1145/1342250.1342279 (the text was not read)
- Policarpo, F. & Oliveira, M. M. (2007). *Relaxed Cone Stepping for Relief Mapping.* GPU Gems 3, ch. 18 (not read).
- Jakob, W. & Hanika, J. (2019). *A Low-Dimensional Function Space for Efficient Spectral Upsampling.* Computer Graphics Forum 38(2), 147–155 (Eurographics 2019). doi:10.1111/cgf.13626. Code: github.com/mitsuba-renderer/rgb2spec

---

*DOIs of Hillaire 2020, Ramamoorthi & Hanrahan 2001, Bruneton & Neyret 2008, Tevs et al. 2008, Jakob & Hanika 2019,
Akenine-Möller et al. 2019 and Bardeen et al. 1972 were checked against Crossref. The formulas of sections 1 (eq. 1, 2, 25–30),
2, 3.1, 3.3, 3.4, 5.1 and 6 were checked against the paper texts. Everything marked "Not verified" or "my own derivation"
was not. Check section and equation numbers in a printed copy before citing them in the paper.*
