# glTF compatibility

Loader: `gpp/src/graphics/assets/gltf*.cppm`, `gltf.cpp`. Mesh side: `src/rendering/mesh_cache.cppm`, `mesh_scene.cppm`,
`shaders/mesh_lighting.glsl`. Demo scene: `templates/model-showcase.yaml` (every model in one row, each scaled to 100 m
across, planets by their sphere, rings may overhang). Regression tests: `gpp/tests/unit/graphics/gltf_loader_gpu_tests.cpp`
(synthetic degradation file, every `assets/models/*/scene.gltf` when present) and `gltf_material_tests.cpp`.

## Models checked (RTX 3060, `Showcase` profile, traced meshes; times are one process loading all 17 in sequence)

| Model | Triangles | Materials | Textures (count, largest, formats; GPU MiB with mips) | Extensions | Tangent / normal / emissive / alpha / double sided | Load ms | Renders correctly |
|---|---|---|---|---|---|---|---|
| borg_cube | 12 | 3 | 3, 1024^2, png+jpg; 15 | KHR_materials_specular | authored / yes / no / no / yes | 92 | yes |
| stargate | 18578 | 4 | 4, 2048^2, jpg; 53 | none | authored / yes / yes / no / yes | 249 | yes |
| ISS | 384163 | 1 | 0 (one grey material); 0 | none | none / no / no / no / yes | 1028 | yes |
| Kelvin (existing) | 28342 | 9 | 15, 4096^2, png; 385 | emissive_strength, specular | authored / yes / yes / BLEND / yes | 1199 | yes |
| DSC Enterprise | 13948 | 2 | 5, 2048^2, png; 66 | none | generated / no / yes / BLEND / yes | 266 | yes |
| USS Discovery | 27006 | 6 | 11, 2048^2, png; 97 | none | generated / no / yes / BLEND / yes | 391 | yes |
| Dderidex Warbird | 29674 | 2 | 5, 1024^2, png; 18 | none | generated / no / yes / BLEND / yes | 144 | yes |
| Storm Infinity | 320404 | 5 | 7, 1024^2, png; 19 | pbrSpecularGlossiness (required) | authored / yes / yes / BLEND / yes | 427 | yes after the fix below (was untextured white) |
| Sun | 32512 | 1 | 1, 2048x1024, jpg; 10 | none | none / no / no / no / yes | 102 | yes (lit like any sphere, not emissive: the asset has no emissive data) |
| Mercury | 27136 | 2 | 2, 8192x4096, jpg+png; 341 | none | authored / yes / no / no / yes | 2093 | yes |
| Venus | 27136 | 2 | 3, 8192x4096, jpg+png; 215 | KHR_materials_specular | authored / yes / yes / BLEND / yes | 927 | yes (atmosphere shell is dithered coverage) |
| Earth | 15360 | 3 | 4, 8192x4096, png (gray, palette); 426 | emissive_strength, transmission (approximated) | none / no / yes / MASK+BLEND / yes | 1082 | planet and night lights yes; cloud shell wrong, see below |
| Moon | 9800 | 1 | 2, 8192x4096, jpg+png; 181 | KHR_materials_specular | authored / yes / no / no / yes | 765 | yes |
| Mars | 27136 | 2 | 1, 4096x2048, jpg; 42 | none | none / no / no / BLEND / yes | 204 | yes (atmosphere shell dithered) |
| Jupiter | 15986 | 1 | 1, 4096x2048, jpg; 42 | none | none / no / no / no / yes | 181 | yes |
| Saturn | 17021 | 2 | 3, 4096^2, jpg+png; 213 | none | authored / yes / no / BLEND / yes | 581 | yes (rings dithered, planet shadow on rings) |
| Uranus | 16503 | 2 | 2, 4096^2, jpg+png; 95 | none | none / no / no / MASK / yes | 171 | yes |

Zips without glTF (left untouched): `realistic-jupiter.zip`, `star-trek-dderidex-class-warbird.zip`,
`star-trek-uss-discovery.zip` (fbx plus textures only). The other 16 zips were unpacked into `assets/models/<zip name>/`
(same layout as the Kelvin folder: `scene.gltf`, `scene.bin`, `textures/`, `license.txt`) and deleted after sizes matched.

Peak VRAM for the whole showcase (about 2.1 GiB of textures) was 3.3 GiB above the idle desktop. The 17 models use about 70
texture slots in the shared table (limit 1024 with descriptor indexing).

## Findings

- Earth: `Material.003` (cloud shell, two nodes of the same radius as the surface) is MASK with cutoff 1.0 and a grey
  8-bit PNG base colour without an alpha channel. By the spec the alpha is 1 everywhere, so the shell is opaque and hides
  the surface in grey. Asset problem (the export lost the cloud opacity), not fixed in the loader. Fix in the asset: give the
  texture an alpha channel (alpha = luminance).
- Mercury, Venus and Mars contain two concentric spheres a few percent apart. In raster mode with the editor clip planes
  (0.1 to 10000 render units) the depth buffer cannot separate them at about 2 km and the inner sphere shows through as black
  speckles (it is in the shadow of the outer one). The traced pass is exact; the `Showcase` profile therefore uses traced meshes.
- Blended materials are dithered coverage (see `lighting-materials.md`): visible stipple on atmospheres and rings.
- Raster and traced agree (PSNR 29 dB on a row of ships and planets, differences are dither, texture LOD and aliasing).
- `GPP_GPU_DISABLE=descriptorIndexing,rayQuery`: 32 textures, the rest untextured with one warning ("more than 32 textures
  without descriptor indexing"); no crash, shadows off.
- About one export in twenty crashed in `ShaderFileWatcher::StartAsync` (shader hot-reload coroutine) at startup; independent
  of glTF, not investigated further.

## Loader changes in this pass

- `KHR_materials_pbrSpecularGlossiness`: diffuse factor and texture become base colour, roughness = 1 - glossiness, metallic 0,
  specular factor scales F0 (glossiness and specular textures ignored).
- Accessors: normalized and integer components (`KHR_mesh_quantization`), bounds checked; sparse and bufferView-less accessors
  (Draco) skip the primitive with a warning instead of failing the model; non-indexed geometry, triangle strips and fans load;
  points and lines are skipped with a warning; out-of-range indices skip the primitive.
- Images that cannot be decoded (webp, ktx2, bad data) or are missing leave the material untextured with a warning.
- Texture limits: images over the device's `maxImageDimension2D` are halved; if the scene's textures would exceed a quarter of the
  largest device-local heap the largest ones are halved until they fit (warning names each).
- Extension audit logged once per file: approximated, ignored, and required-but-unsupported.
- `Loaded glTF` log line now lists triangles, textures, texture MiB and load time.
- Scenes preload their meshes on the scene-load worker (no multi-second stall on the first rendered frame).
- Scenes may carry `EditorCamera: {PositionM, Yaw, Pitch, Fov, SpeedMs}`, applied to the editor camera on load.

Stress file (4.0 M triangles, five 16384x8192 jpg textures): loads in 19 s on the worker, one texture reduced to 8192x4096 by
the 3 GiB budget, renders.

## Extension support

| Extension | Status |
|---|---|
| KHR_materials_emissive_strength, KHR_materials_ior, KHR_materials_specular, KHR_texture_transform | supported |
| KHR_mesh_quantization | supported |
| KHR_materials_pbrSpecularGlossiness | approximated (see above) |
| KHR_materials_transmission | approximated by the material's alpha mode and factor, warned |
| KHR_draco_mesh_compression, EXT_meshopt_compression | not supported, primitives skipped with warning |
| KHR_texture_basisu, EXT_texture_webp | not supported, material untextured, warning |
| KHR_materials_clearcoat, sheen, volume, iridescence, anisotropy, unlit, KHR_lights_punctual, EXT_mesh_gpu_instancing | ignored with warning |

## Known limits

- Sparse accessors, morph targets, skins and animations are not evaluated (static bind pose geometry only, first scene).
- Only `TEXCOORD_0/1`; wrapT is taken from wrapS.
- Alpha blend is order independent dithering, not sorted blending; blended and masked meshes cast no shadows.
- Without descriptor indexing only 32 textures across all loaded models are bound.
- Texture decode (stb) is single threaded: an 8k PNG costs about a second.
- Meshes are loaded once and never freed (`MeshCache`).
