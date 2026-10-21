# Terrain: height maps, procedural detail and ocean

A sphere with a **Terrain** component has relief. A height map (and, optionally, procedural detail) lifts or lowers the
surface; an ocean fills everything below sea level. Close to the body the relief is ray-marched, so the camera can
fly over mountains and see them against the sky. Farther away only the normals of the relief shade the sphere. These
notes give the data model, the height function, the march and the shading, and what does not work yet.

Code:
- `TerrainComponent` in `src/simulation/components.cppm`
- `FillTerrainGpu` in `src/rendering/bodies.cppm`
- `terrainDetail`, `terrainLandHeight`, `terrainHeight`, `terrainIsWater`, `terrainBounds`, `terrainNormal`,
  `terrainIntersect` in `shaders/terrain.glsl`
- the use in `shadeBodySurface` (`shaders/body.glsl`) and in the local-frame branch of
  `shaders/black_hole_rendering.comp`
- render setting `bodies.terrain_steps` in `src/rendering/render_settings.cpp`

Tests: no unit test covers the shader code. The component's YAML round trip comes from the reflection system.
`tests/unit/rendering/bodies_tests.cpp` covers the body entries around it.

Related notes: `textured_bodies.md` (the maps and the Height role), `local_frames.md` (the camera frame in which the
march runs), `m2_rendering_references.md` §5 (heightfield marching on spheres).

---

## 1. Fields

| Field | Default | Meaning |
|---|---|---|
| `Height` | empty | height map, equirectangular, loaded with the Height role (R16). Black = `HeightMin`, white = `HeightMax` |
| `HeightMin` | 0 m | height of black, relative to the sphere's radius |
| `HeightMax` | 6400 m | height of white |
| `Ocean` | true | everything below sea level (the sphere's radius) is water |
| `DetailAmplitude` | 0 m | amplitude of the procedural detail; 0 = none |
| `DetailScale` | 2000 m | wavelength of the largest detail octave |
| `DetailOctaves` | 5 | 1 to 10 |
| `Seed` | 1 | picks the region of the noise field |

`FillTerrainGpu` converts lengths to render units. The seed becomes a noise offset
(127.1 s, 311.7 s, 74.7 s) mod 1000, which only has to land in a different part of the field.

The sphere's radius is the reference: it is the sea level, the silhouette seen from far away, and the origin of the
heights. Outside a body's local frame the tracer still intersects the plain sphere of that radius.

**Height maps.** NASA's GEBCO relief for Earth is scaled 0 to 6400 m with black = sea level (`texture_sources.md` §3.4), which
is why `HeightMax` defaults to 6400 m. The file used by `templates/solar-system-bodies.yaml` (`tools/fetch_textures.py`,
`earth/height_5k.jpg`) is an 8-bit JPEG, so a level is about 25 m and the JPEG compression shows as noise. The
procedural planets of `tools/gen_planet_textures.py` write 16-bit PNGs.

## 2. The height function

The land height at a body-frame direction n is

    h_land(n) = mix(HeightMin, HeightMax, map(n)) + detail(n)

With no height map the first term is 0. The visible surface is

    h(n) = max(h_land, 0)   with an ocean,       h_land   without.

A point with an ocean and h_land ≤ 0 is **water**: flat at sea level, shaded with `GlossyRoughness`, and the normal map is
ignored on it (`textured_bodies.md` §5).

**Procedural detail** (`terrainDetail`) is a ridged fractal sum of simplex noise (`snoise`), in the style of Musgrave's
ridged multifractal:

    octave k:  v_k = (1 − |snoise(p_k)|)²,    weight w_k = 0.5, 0.25, 0.125, ...
    p_k  = 2.03^k · (n · R / DetailScale) + seed offset,      wavelength_k = DetailScale · 0.4926^k
    detail = DetailAmplitude · ( 2 Σ w_k v_k / Σ w_k − 1 )

so the detail lies in [−amplitude, +amplitude], with sharp crests and rounded valleys. This is a simplified form: the
original ridged multifractal also feeds each octave's value into the weight of the next, which is not done here. The
form was not checked against Musgrave's text: Not verified.

**Fading.** Octaves whose wavelength is less than about four pixel footprints would only alias. Each octave is faded by
clamp(wavelength / (4 · footprint) − 1, 0, 1) towards the constant 0.5, so the detail disappears smoothly with
distance. The height map is sampled with the same footprint, so its mip level follows too.

## 3. Ray march near the body

When the camera is inside a body's local frame (within 20 radii, `local_frames.md`), a ray is traced against the relief
instead of the plain sphere (`terrainIntersect`):

1. **Bounding shells.** The surface lies between `lowest` and `highest` above the radius:
   highest = max(HeightMin, HeightMax) + detail amplitude, lowest = min(...) − detail amplitude (with an ocean, lowest ≥ 0).
   Without a height map both are ± the detail amplitude. The ray starts at its entry to the outer shell.
   A ray that reaches the inner shell without a hit is stopped there.
2. **Steps.** At each step the height of the ray above the surface, `above` = |p| − R − h(p), is evaluated. The step is
   0.6 · `above`, at least half a pixel footprint (0.5 · pixelSpread · t) and at least 10⁻⁷ R. The factor assumes
   slopes below about 1.
3. **Bisection.** When `above` ≤ 0, the hit is refined between the last two points with 8 bisection steps.
4. **Out of steps.** A ray that uses up `bodies.terrain_steps` (default 128, 8 to 512) inside the outer shell **counts as a
   hit** at its current position. That avoids holes along the horizon, where grazing rays need many steps; the surface
   is about there.

The shading of the hit uses the sphere normal at the hit point (the relief normal is applied in `shadeBodySurface`,
§4). Atmosphere and clouds are composited on top as usual (`atmospheres.md`).

## 4. Shading and distance

At any distance `shadeBodySurface` calls `terrainNormal` for the land:

    angle = max(footprint / R, π / (rows of the height map))
    N = normalize( n − east · (h_e − h_0)/(angle R) − north · (h_n − h_0)/(angle R) )

where h_e and h_n are the heights one `angle` to the east and north. That is a finite difference over the larger of
the pixel footprint and one texel of the height map. Close up, the normals follow the map; far away, the footprint
dominates and the relief is smoothed.

A normal map on the Surface is applied on top, with the relief normal as its base. Water takes no relief and no normal map.
From far away, the sphere stays a smooth sphere of radius R with relief **shading** only. The mountains do not
protrude from the silhouette.

## 5. Settings

| Setting | Default | Effect |
|---|---|---|
| `bodies.terrain_steps` | 128 | steps of the relief march in the local frame (8 to 512) |
| `bodies.texture_max_size` | 4096 | a smaller limit blurs the height map |

## 6. Checked by renders

Reported from the implementation, with the Earth template at 15 km above the Andes (not repeated for these notes): a
curved limb with haze and relief against the sky. See `atmospheres.md` §8.

## Limitations

- **No self-shadowing.** The terrain casts no shadows on itself. Mountains are lit by the sun by their normals only. A
  light below the geometric horizon of the sphere is still blocked (`body_lighting.md` §5).
- **Steep slopes.** The step size assumes slopes below about 1. Steeper relief can be stepped through, which shows as
  holes or thin cracks.
- **Float precision.** With solar gravitational radii as the render unit (1476.6 m), Earth's radius is about 4300 render units
  and a float step there is 2⁻¹¹ units, about 0.72 m. Walking on the surface at metre scale therefore needs coordinates
  relative to the camera. That is planned for M5.
- **No physics.** The heights are not available to PhysX (M5); a rigid body does not collide with the relief.
- **Flat shading from far away.** No silhouette relief (§4).
- **Poles.** The east direction is undefined at the poles, so the finite differences return the sphere normal there
  (`terrainNormal` returns n when the east vector is shorter than 10⁻⁴).
- **Ocean.** The water is a flat, glossy surface. There are no waves, no depth colour, no foam, no refraction.

---

## Sources

- Musgrave, F. K. (2002). *Procedural Fractal Terrains.* In Ebert, D. S. et al., *Texturing and Modeling: A Procedural Approach*, 3rd ed., Morgan Kaufmann. The ridged multifractal. Not read for this note: Not verified.
- Dummer, J. (2006). *Cone Step Mapping: An Iterative Ray-Heightfield Intersection Algorithm.* Unpublished technical note, see `m2_rendering_references.md` §5.1. Not used by the code; the march is a plain height-proportional step (0.6 × height above the surface).
- Tevs, A., Ihrke, I. & Seidel, H.-P. (2008). *Maximum Mipmaps for Fast, Accurate, and Scalable Dynamic Height Field Rendering.* Proc. I3D 2008, 183–190. doi:10.1145/1342250.1342279. A possible later acceleration (text not read; see `m2_rendering_references.md` §5.2).
- NASA Earth Observatory. *Blue Marble Next Generation: topography and bathymetry* (GEBCO relief). Credit and file sizes: `texture_sources.md` §3.4.

*The code statements were read in the files listed at the top. The height map credit line and file facts come from
`texture_sources.md`.*
