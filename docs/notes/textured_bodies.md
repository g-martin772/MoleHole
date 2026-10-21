# Textured bodies: Surface maps, rotation and the texture cache

Milestone M2 turns the traced spheres into planets. A sphere with a **Surface** component gets equirectangular
albedo, normal, roughness and emissive maps, rotates on its own clock, and is shaded by the lighting described in
`body_lighting.md`. These notes cover the maps: the file formats, the coordinate convention, the level-of-detail
choice, the texture cache, the import path, and what a sphere without a Surface still does.

Code:
- `SurfaceComponent` in `src/simulation/components.cppm`
- `BodyGpu`, `MakeBodyGpu`, `FillBodyData`, `EquirectUv`, `BodyOrientation`, `TextureSlotTable` in `src/rendering/bodies.cppm`
- `BodyRotationModule` in `src/simulation/body_rotation_module.cppm`
- `shaders/body.glsl` (`equirectUv`, `bodyTextureLod`, `sampleBodyMap`, `shadeBodySurface`) and `shaders/sphere.glsl`
- the texture system in gpp: `dependencies/gpp/src/graphics/assets/texture.cppm` and `.cpp` (`TextureCache`)
- `TraceStage` (`UploadBodies`) in `src/rendering/render_pipeline.cpp`
- `ImportTexture` in `src/application/texture_import.cppm` and `BrowseAsset` in `plugins/scene_window_layer.cpp`
- render settings `bodies.textures` and `bodies.texture_max_size` in `src/rendering/render_settings.cpp`

Tests:
- `tests/unit/rendering/bodies_tests.cpp` (convention, orientation, slots, rotation, YAML round trip)
- `tests/unit/application/texture_import_tests.cpp`
- `dependencies/gpp/tests/unit/graphics/texture_tests.cpp` (roles, mips, MaxSize, decoding)
- `tests/unit/rendering/render_settings_tests.cpp` (the `DescriptorIndexing` capability for `bodies.textures`)

Related notes: `body_lighting.md`, `atmospheres.md`, `terrain.md`, `local_frames.md`. The literature for the ray-cone
LOD is collected in `m2_rendering_references.md` §1; the sources of the maps themselves are in `texture_sources.md`
and `interstellar_planets.md`.

---

## 1. Components

`SurfaceComponent` fields (defaults from the code):

| Field | Default | Meaning |
|---|---|---|
| `Albedo` | empty | sRGB colour map |
| `Normal` | empty | tangent-space normal map (x east, y north, z up), linear |
| `Roughness` | empty | linear, red channel |
| `Emissive` | empty | sRGB colour map, for example city lights on the night side |
| `Tint` | (1, 1, 1) | multiplies the albedo, together with `Sphere.Color` |
| `RoughnessFactor` | 0.9 | multiplies the roughness map; the roughness of the whole surface without a map |
| `RoughnessIsSpecular` | false | the roughness map is a specular mask, see §5 |
| `GlossyRoughness` | 0.45 | roughness of the glossy parts when the map is a specular mask |
| `NormalStrength` | 1 | scales the x and y of the tangent-space normal |
| `EmissiveStrength` | 1 | scales the emissive map |
| `RotationPeriod` | 0 | seconds of the body's proper time per turn; 0 = not rotating |
| `PrimeMeridian` | 0 | degrees, added to the rotation angle |
| `RotationAngle` | 0 | runtime only, radians. Advanced by `BodyRotationModule`, not saved to YAML |

An empty path means "no map": the shader uses a neutral value (white albedo, flat normal, roughness factor only, no
emission). A map that is still loading, failed to load, or found no free slot (§4) is treated the same way.

## 2. The equirectangular convention

A body has its own frame: +Y is the sphere's `SpinAxis` (north), +X points through longitude 0 when the rotation angle
is 0. For a body-frame unit direction n (`EquirectUv`, mirrored by `equirectUv` in `body.glsl`):

    east longitude  λ = atan2(−n_z, n_x)
    latitude        φ = asin(n_y)
    u = 0.5 + λ / 2π        (u = 0 at λ = −180°, growing east)
    v = 0.5 − φ / π         (v = 0 at the north pole)

So +X is the centre of the map and −Z is λ = +90°. A test checks these values. The same convention is used for
the probe directions of `body_lighting.md` and for the maps written by `tools/gen_planet_textures.py`.

**Rotation.** `BodyOrientation(spinAxis, angle)` is the quaternion that takes +Y to the spin axis, followed by a
turn about +Y by `angle = RotationAngle + PrimeMeridian` (in radians). The turn is eastward: a point at longitude λ
moves towards λ + angle. The shader rotates the world normal into the body frame with the conjugate quaternion
before computing (u, v). A cloud layer uses the surface angle plus its own `RotationAngle`, so clouds turn with the
ground unless they have a `RotationPeriod` of their own (`atmospheres.md`).

`BodyRotationModule` runs in the `PostSim` phase, after the geodesic module, so the clock rate belongs to this tick's
position. For each Surface with `RotationPeriod > 0` it advances the angle by 2π · (dt · TimeScale · rate) / period.
`rate` is 1 for an ordinary sphere. For a Geodesic Body in free fall it is the body's `ClockRate`, so the rotation
runs on the body's proper time, dτ = ClockRate · dt (`observers.md` §4). A test checks that a body with
ClockRate 0.5 turns half as far as a plain one. Miller's planet near Gargantua therefore turns far slower, in
distant time, than the same planet far away.

## 3. Texture roles and the GPU format

The gpp texture system decodes with stb_image (PNG, JPEG, TGA, BMP, PSD, GIF, PIC, Radiance HDR). Each map is loaded
with a **role** that fixes the GPU format and the way the mip chain is built (`TextureRole`):

| Role | GPU format | Used for | Mip filter |
|---|---|---|---|
| Color | `R8G8B8A8_SRGB` | albedo, emissive, ring strip | 2×2 box in linear light, re-encoded to sRGB |
| Data | `R8G8B8A8_UNORM` | normal, roughness, cloud coverage | 2×2 box on the stored values |
| Height | `R16_UNORM` | terrain heights (`terrain.md`) | 2×2 box on the 16-bit values |
| Hdr | `R16G16B16A16_SFLOAT` | high dynamic range colour | 2×2 box in linear floats |

- **Height from 8-bit sources.** The Height role loads through `stbi_load_16` as greyscale, so an 8-bit file is widened to
  16 bits. It has only 256 distinct heights then. Use a 16-bit PNG for smooth relief.
- **Mip chain.** The whole chain down to 1×1 is built on the CPU at load. An odd last row or column is folded into its
  neighbour's average. A test checks that the mean of black and white is sRGB 188 for Color and 128 for Data.
- **Normal mips are not renormalised.** They are plain averages of the stored components, so a distant, rough normal map
  gets shorter vectors. The shader normalises the perturbed normal afterwards.
- **MaxSize.** `bodies.texture_max_size` (default 4096, 256 to 16384, logarithmic slider) drops the top mip levels
  until both sides fit. The source size is kept in `SourceWidth` and `SourceHeight`. Levels that are too large are
  only stepping stones during the build.
- **Sampler.** Equirectangular maps repeat in longitude and clamp at the poles (`kEquirectangularSampling`), with linear
  filtering and all mip levels.

## 4. The texture cache

`TextureCache` (owned by the render resources) loads a map the first time `Get(path, role)` is called for it.

- **Threads.** One worker thread decodes and builds the mips. The upload to the GPU happens on the render thread in
  `Update`, once per frame, before the trace stage fills the body buffer.
- **Hot reload.** `Update` polls the modification time of every loaded file every 500 ms. A changed file is decoded
  again. The old image stays alive for 4 more updates (`RetireFrames`), because frames in flight may still read it. A
  failed reload keeps showing the previous image and logs a warning.
- **Changing MaxSize.** `SetMaxSize` with a new value reloads every texture.
- **Pending count.** `PendingCount()` is the number of textures that are decoding. The trace stage reports "not ready"
  while it is above zero, so an export does not start before the maps are there (`body_lighting.md` §5 for the
  second condition).
- **Slots.** `UploadBodies` gives each distinct image a slot in order of first use (`TextureSlotTable`), at most 64. The
  shader indexes the array `u_bodyTextures[64]` with the slot numbers stored in `BodyGpu`. Empty slots point at a
  white dummy texture. More than 64 distinct images log a warning once; the extra maps are drawn untextured.
- **Fallbacks.** The array is non-uniformly indexed, so the shader needs `descriptorIndexing`. Without the capability
  the define `GPP_BODY_TEXTURES` is not set, `bodies.textures` is greyed out, and bodies stay untextured (flat albedo
  from `Sphere.Color` and `Tint`).

## 5. Roughness and specular masks

`shadeBodySurface` uses a Lambert diffuse term plus a GGX specular lobe (Schlick Fresnel with F0 = 0.04, Smith
geometry). Roughness comes from the red channel of the roughness map:

- plain map: roughness = `RoughnessFactor` × map (the map darkens the factor)
- `RoughnessIsSpecular`: roughness = mix(`RoughnessFactor`, `GlossyRoughness`, map). White parts of the map take the
  glossy roughness, black parts the factor. Earth's specular map (white ocean) is used this way
- a water surface of a Terrain (`terrain.md`) always takes `GlossyRoughness`
- the result is clamped to [0.02, 1]

**GlossyRoughness = 0.45.** Wind-roughened sea is modelled as a rough surface with a mean-square slope of the waves.
Cox & Munk (1954) give the slope variance 0.003 + 5.12 × 10⁻³ U for a wind speed U. At U = 7 m/s that is 0.0388, an
rms slope of about 0.2. With GGX alpha = roughness², alpha = 0.2 gives roughness = √0.2 ≈ 0.45. Identifying the rms
slope with the GGX alpha is a rough approximation chosen here. The Cox & Munk coefficients are quoted from memory:
Not verified.

## 6. Spheres without a Surface

A sphere with no Surface component keeps the pre-M2 behaviour: a flat, unlit colour (`Sphere.Color`), multiplied by
`Sphere.TexturePath` as an albedo map if it has one. These are markers (orbit demos, trail targets). This keeps every
template made before M2 visually unchanged. An A/B render of `templates/m1-orbits.yaml` before and after was
compared during the implementation to check it; that comparison was not repeated when writing these notes.

A sphere with a mass and a main-sequence temperature (a star, `getTemperatureFromMass` in the shader) still glows in
its blackbody colour from the LUT, with or without a Surface. Only spheres with a Surface get a sky probe (`body_lighting.md`).

An Atmosphere on a marker is uploaded but the marker has no probe and no lit Surface; a test checks that it stays a
marker (`Flags.x == 0`).

## 7. Texture level of detail

A compute shader has no screen-space derivatives, so the mip level comes from a **ray cone** (Akenine-Möller et al.
2019, 2021; `m2_rendering_references.md` §1). The footprint of one pixel on the surface is

    footprint = pixelSpread · distance,      pixelSpread = 2 tan(fov/2) / image height
    lod = log2( footprint / (texelSize · max(cos, 0.05)) ),      texelSize = π R / texture height

where distance is the straight distance from the camera to the hit, `cos` = |n·d| the angle between the view ray and
the surface normal, R the sphere radius, and the texel size is the arc length of one row of the map. `textureLod`
clamps the result to the available mips.

- **Lensing is not included.** The distance is the straight distance, not the length of the bent ray, and the
  magnification of the bundle by the gravitational field is ignored. Near a black hole a lensed planet is
  magnified or squeezed, and its texture is too blurry or too sharp. `m2_rendering_references.md` §1.1 discusses the
  bundle footprint that would be needed.
- **Pole anisotropy.** The texel size uses the latitude spacing. Along a parallel the texels are shorter by sin θ,
  so the choice is the sharp one of the two options in `m2_rendering_references.md` §1.1 and may alias in u near
  the poles.
- **Local frames.** In a body's local frame (`local_frames.md`) the distance is the proper distance to the surface,
  and there is no lensing to leave out.

## 8. Display units

The lighting is in the same units as the rest of the tracer, with white = 1. A Surface that is lit by an irradiance of
π at normal incidence and has an albedo of 1 shows 1.0 for the Lambert term alone (albedo/π · E). In the code the
diffuse term is multiplied by (1 − F) with the Fresnel term F ≥ 0.04, and the specular lobe is added, so the
total at normal incidence is slightly different. A `Light` of the default `Intensity` π is calibrated with that
rule (`body_lighting.md`).

## 9. Getting maps into the project

Textures are **not in git**: `assets/textures/` is in `.gitignore`.

- `tools/fetch_textures.py` downloads the Earth and Saturn maps listed in `tools/texture_manifest.yaml` (groups
  `core`, `core-2k`, `nasa`), checks SHA-256 where the manifest has one, converts TIFF downloads to PNG (needs
  Pillow) and writes `assets/textures/ATTRIBUTION.md`. Sources and licences: `texture_sources.md`.
- `tools/gen_planet_textures.py` generates original maps for Miller's, Mann's and Edmunds' planets (the film's assets
  are not available). The output is deterministic for a given seed and size and is seamless at the date line and
  at the poles, because it samples 3D noise on the unit sphere.
- **The "..." button.** Every asset-path field in the inspector has a "..." button. For an image it calls
  `ImportTexture` (`texture_import.cppm`):
  - extensions: png, jpg, jpeg, tga, bmp, psd, hdr, pic, gif. TIFF and EXR are refused with a message, because stb
    cannot decode them. Convert them first
  - a file inside the project is referenced where it is, with a project-relative path
  - a file outside is copied to `assets/textures/imported/`. An identical file already there is reused, and a
    different file of the same name gets `_1`, `_2`, ...
  - warnings are shown under the field: not 2:1 (spheres expect an equirectangular map), and larger than 16384 px (the
    top mips are dropped)
  - other file types (for example scripts) are stored as a project-relative path when they lie inside the project
    and as an absolute path otherwise

## Limitations

- **Single set of coordinates.** No tiling, no second UV set, no per-body texture transform other than the rotation.
- **Sphere only.** Bodies are spheres. Saturn is drawn at its equatorial radius with no flattening.
- **Lensing and LOD.** See §7.
- **Pole filtering.** The mip chain is a plain 2×2 box. Texels near the poles cover less solid angle than those at
  the equator and are weighted equally (`m2_rendering_references.md` §1.1).
- **Slot limit.** 64 distinct images per scene. 16 spheres at most (`kMaxSpheres`).
- **Memory.** `TextureCache::GpuBytes()` reports the total, but there is no eviction. Lower `bodies.texture_max_size`
  if a scene runs out of memory.
- **Not verified.** The A/B claim of §6 and the behaviour on GPUs without `descriptorIndexing` were not re-run for
  these notes.

---

## Sources

- Akenine-Möller, T., Nilsson, J., Andersson, M., Barré-Brisebois, C., Toth, R. & Karis, T. (2019). *Texture Level of Detail Strategies for Real-Time Ray Tracing.* In *Ray Tracing Gems*, ch. 20, pp. 321–345. Apress. doi:10.1007/978-1-4842-4427-2_20
- Akenine-Möller, T., Crassin, C., Boksansky, J., Belcour, L., Panteleev, A. & Wright, O. (2021). *Improved Shader and Texture Level of Detail Using Ray Cones.* Journal of Computer Graphics Techniques 10(1), 1–24. jcgt.org/published/0010/01/01/ (details and the checking status: `m2_rendering_references.md`)
- Cox, C. & Munk, W. (1954). *Measurement of the roughness of the sea surface from photographs of the sun's glitter.* J. Opt. Soc. Am. 44(11), 838–850. Not verified (coefficients from memory; DOI not checked).
- Walter, B., Marschner, S. R., Li, H. & Torrance, K. E. (2007). *Microfacet models for refraction through rough surfaces.* Eurographics Symposium on Rendering. The GGX distribution. Not read for this note; the shader form is the common one.
- Sources of the maps: `texture_sources.md` (Solar System Scope, CC BY 4.0; NASA Earth Observatory and Goddard imagery) and `interstellar_planets.md` for the planets' parameters.

*The code statements were read in the files listed at the top. The two literature statements marked "Not verified" were
not checked against the papers.*
