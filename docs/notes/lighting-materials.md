# Lighting and materials of meshes

Roadmap: `docs/physics-roadmap/05-lighting-materials.md` (L-2, L-3, L-5, L-9). Code: `src/rendering/lighting.cppm`,
`mesh_scene.cppm`, `mesh_cache.cppm` (`MeshMaterialTable`), `shaders/mesh_lighting.glsl`, `mesh.frag`, `mesh_trace.glsl`.

## Image units

Meshes, lights and the tracer write scene-linear values. **Image value 1 is the radiance of the BT.2408 reference white,
L_v = 203 cd/m2.** Radiance in the light buffer is radiometric RGB in the 683 lm/W convention of gpp (`LightGpu`), so

    image = L [W m-2 sr-1] * 683 / 203          (RadianceToImage())

- **Emissive**: `EmissiveRadiance = emissiveFactor * KHR_materials_emissive_strength * 203 / 683` (gpp `PackGltfMaterial`),
  so an emissive value of 1 is image value 1 at EV 0 (reference white), strength 5 is 5. Under a physical camera the exposure
  stage multiplies by `203 * ExposureScale(N, t, ISO)` (the Filament scale acts on cd/m2), so an emissive of 1 shows as the
  luminance 203 cd/m2 * 1/(1.2 * 2^EV100).
- **Default key light**: a scene with no `DirectionalLight`/`PointLight`/`SpotLight`/`AreaLight` and no `Star` gets one
  directional light of irradiance 203/683 = 0.297 W/m2 from direction (1,1,1): exactly the old hard-coded light of
  radiance 1 (a white diffuse surface facing it shows 1/pi). Existing templates keep their brightness.
- **Sky**: uniform radiance `lighting.sky_radiance` (default 0.0089 W/m2/sr = the old ambient 0.03 image units) times
  occlusion, times the BRDF's directional albedo (white furnace form).
- Tone mapping and exposure are applied downstream as before (`post.tonemap`, `post.exposure_mode`).

## Materials

glTF P0 through `GltfMaterialLibrary`: one material SSBO and one descriptor-indexed texture table per pass (1024 textures
with `DescriptorIndexing`, 32 without; textures past the capacity are untextured and logged), material index as a push
constant (raster) or instance field (traced). No per-draw descriptor sets. Both passes evaluate `pbr/material.glsl` and
`pbr/brdf.glsl` (normal map with the tangent from the `ExtraBuffer`, metallic-roughness, occlusion on the sky term only,
emissive, KHR texture transform/specular/ior, double sided).

- **Alpha MASK**: cutoff test, in both passes.
- **Alpha BLEND**: approximated by dithered coverage, not sorted blending: a hit is kept when alpha exceeds an interleaved
  gradient noise threshold of the pixel (`meshDitherThreshold`, same pixel coordinates in both passes). Order independent,
  depth correct, noisy for alpha near 0.5 (the ship's windows and glass).
- **Single sided**: back faces are discarded (raster) or rejected in the candidate loop of the ray query (traced).
- **Shadow rays** see only opaque casters (TLAS instance mask 1); masked and blended materials cast no shadows.
- **Texture LOD** in the traced pass: ray cone footprint (`pixel angle * t`) against the triangle's world and uv area, per
  texture size. The raster pass uses derivatives.
- The DFG table of the BRDF is a storage buffer (`MESH_BIND_DFG`), not gpp's constant array: dynamically indexed it becomes
  per-thread local memory and the big tracer kernel faults on the driver (`Xid 13 SKEDCHECK02_LOCAL_MEMORY_LOW_SIZE`,
  device lost on the RTX 3060).

## Lights

`BuildMeshLighting` = gpp `BuildLightBuffer` (positions in metres relative to the frame origin; the shader multiplies render
positions by the length unit) + Stars + disk light + default key light, uploaded each frame to a slot of a ring of 8
(render targets and frames in flight do not share buffers). Point, spot, directional and area lights are evaluated;
sphere/disk/rect lights use the representative point nearest the reflection vector and an `E = L A cos / d2` irradiance.
Shadow rays (`lighting.shadows`, ray query only): origin offset along the geometric normal, `tMax` to the light.
Without `rayQuery` the meshes are lit without shadows; without `descriptorIndexing` the traced kernel is unavailable and
the raster pass runs with 32 textures (`GPP_GPU_DISABLE=descriptorIndexing,rayQuery` tested).

## Star

`Star { Teff, Radius, Mass, LimbDarkening }`: `L = 4 pi R2 sigma T4`, colour from the Planck spectrum (relative luminance 1).
- Tracer: a sphere with centre radiance `sigma T4 / pi * K(T)/683 * 683/203 / (1 - u/3)` image units and the limb profile
  `1 - u (1 - mu)`; `K(T)` is the luminous efficacy of the blackbody (93 lm/W for the Sun), so the Sun is not 7 times too
  bright as it would be with all power at 555 nm. Sphere components without a Star are unchanged.
- Light: a point light with radius, `I = L / 4 pi * K(T)/683` W/sr, i.e. `E = L / (4 pi d2)` at the receiver.
- Not done: the Mass is not a gravity source.
Sun at 1 AU: bolometric 1361 W/m2 (test), about 1.27e5 lx.

## Disk light (`lighting.disk_light`, approximation L2)

A point source at the heaviest black hole with the disk luminosity `L = eta Mdot c2 = ratio L_Edd` (`eta` from
`RadiativeEfficiency`, `Mdot = ratio L_Edd / (eta c2)`), emitting like a flat Lambertian disk: `I = L |cos theta| / 2 pi` about
the spin axis, `1/d2` with `d` clamped to the ISCO radius, no light inside the horizon, ship self-shadowing by shadow rays,
colour from `BlackbodyColor(T)` of `disk.display_temperature` or the peak temperature (clamped to 40 000 K by the helper).
The magnitude is calibrated to the *displayed* disk: `disk.glsl` shows the peak radiance `sigma T_peak4 / pi` as 0.25 image
units (`DISK_REFERENCE_LUMINANCE`, mirrored in `kDiskReferenceLuminance`), so the light is scaled by `0.25 pi / (sigma
T_peak4)`; a stellar-mass hole at 0.1 Eddington is X-ray, its true visible power would be 1e-9 of this. Not modelled: lensing
of the light on its way to the ship, redshift at the ship, occlusion of the far side of the disk by the hole, disk extent
(the point is at the hole). This is the first step of the GR probe.

## Exposure

`post.exposure_mode = Physical` with `post.camera_aperture` N, `post.camera_shutter` t, `post.camera_iso` S:
`EV100 = log2(N2/t * 100/S)`, exposure `203 / (1.2 * 2^EV100) * 2^EV` (the EV setting compensates). Needs a tone map. The
tracer's disk and sky are not radiometric yet, so only meshes and Stars are physically scaled. Sunny 16 (f/16, 1/100 s,
ISO 100) maps a white diffuse surface under 1.27e5 lx to about 1.3.
