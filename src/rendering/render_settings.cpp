module;
#include <yaml-cpp/yaml.h>
module MoleHole;

import :Rendering.Settings;
import std;
import GPP;

using namespace GPP;

namespace MoleHole
{
    namespace
    {
        constexpr const char* kDoc = "USER_GUIDE.md#render-profiles-and-settings";

        SettingRegistry<RenderSettings> BuildRegistry()
        {
            using R = RenderSettings;
            SettingRegistry<R> registry;

            registry.Add(Setting("spacetime.solution", &R::Solution, {
                .Label = "Solution", .Group = "Spacetime",
                .Tooltip = "Metric of every black hole. Spin a* and charge q* come from each hole's inspector.\n"
                           "Only the integrated transport evaluates the full metric.",
                .Doc = kDoc,
                .Options = {"Schwarzschild", "Kerr", "ReissnerNordstrom", "KerrNewman"},
                .OptionLabels = {"Schwarzschild", "Kerr", "Reissner-Nordstrom", "Kerr-Newman"}}));

            registry.Add(Setting("transport.mode", &R::TransportMode, {
                .Label = "Ray transport", .Group = "Transport",
                .Tooltip = "How camera rays travel through curved space.\n"
                           "Straight: no bending, analytic shadow and thin disk, no ray marching. Fastest, for editing.\n"
                           "Analytic lens: precomputed deflection, fast and good-looking.\n"
                           "Integrated: geodesic integration of the selected metric, physically accurate.",
                .Doc = kDoc,
                .Options = {"Straight", "AnalyticLens", "Integrated"},
                .OptionLabels = {"Straight", "Analytic lens", "Integrated (geodesics)"},
                .OptionCosts = {0, 1, 4}}));
            registry.Add(Setting("transport.step_size", &R::RayStepSize, {
                .Label = "Step size", .Group = "Transport",
                .Tooltip = "Ray march step in render units (r_g of the render length unit). Smaller is more accurate and slower.",
                .Doc = kDoc, .Min = 0.001, .Max = 1.0, .Scale = SettingScale::Log, .Cost = 3,
                .Format = "%.4f", .VisibleWhen = "transport.mode>=1"}));
            registry.Add(Setting("transport.max_steps", &R::MaxRaySteps, {
                .Label = "Max steps", .Group = "Transport",
                .Tooltip = "Upper bound of ray march steps per pixel. Rays that run out are drawn as the sky.",
                .Doc = kDoc, .Min = 100, .Max = 200000, .Cost = 3, .VisibleWhen = "transport.mode>=1"}));
            registry.Add(Setting("transport.adaptive_rate", &R::AdaptiveStepRate, {
                .Label = "Adaptive step rate", .Group = "Transport",
                .Tooltip = "How strongly the step shrinks close to a black hole.",
                .Doc = kDoc, .Min = 0.01, .Max = 10.0, .Scale = SettingScale::Log, .Format = "%.2f",
                .VisibleWhen = "transport.mode>=1"}));
            registry.Add(Setting("transport.gpu_precision", &R::GpuPrecision, {
                .Label = "GPU precision", .Group = "Transport",
                .Tooltip = "Arithmetic of the geodesic integration. Only the Integrated transport uses it.\n"
                           "f64 runs at 1/32-1/64 rate on consumer GPUs.",
                .Doc = kDoc,
                .Options = {"f32", "f64", "f128"},
                .OptionRequires = {"", std::string(GpuCapability::kShaderFloat64), std::string(GpuCapability::kNever)},
                .OptionCosts = {0, 4, 5}, .CostWhen = "transport.mode>=2"}));

            registry.Add(Setting("radiometry.redshift", &R::GravitationalRedshift, {
                .Label = "Gravitational redshift", .Group = "Radiometry",
                .Tooltip = "Shift disk colour with the gravitational potential.", .Doc = kDoc}));
            registry.Add(Setting("radiometry.doppler", &R::DopplerBeaming, {
                .Label = "Doppler beaming", .Group = "Radiometry",
                .Tooltip = "Brighten the approaching side of the disk and dim the receding side.", .Doc = kDoc}));

            registry.Add(Setting("disk.enabled", &R::AccretionDisk, {
                .Label = "Accretion disk", .Group = "Disk",
                .Tooltip = "Thin disk in each black hole's equatorial plane.\n"
                           "The Straight transport draws a simplified analytic disk (no volume, height or noise detail).",
                .Doc = kDoc, .Cost = 1}));
            registry.Add(Setting("disk.volumetric", &R::AccretionDiskVolumetric, {
                .Label = "Volumetric", .Group = "Disk",
                .Tooltip = "Off: geometrically thin disk, shaded exactly where rays cross the equatorial plane.\n"
                           "On: flared 3D disk with radiative transfer through the gas.\n"
                           "Not used by the Straight transport.",
                .Doc = kDoc, .Cost = 3, .VisibleWhen = "disk.enabled & transport.mode>=1"}));
            registry.Add(Setting("disk.height", &R::AccDiskHeight, {
                .Label = "Height", .Group = "Disk",
                .Tooltip = "Gaussian scale height in M at r = 10 M; the disk flares outwards with constant H / r.\n"
                           "Only the volumetric disk has a height.",
                .Doc = kDoc, .Min = 0.0, .Max = 2.0, .VisibleWhen = "disk.enabled & transport.mode>=1"}));
            registry.Add(Setting("disk.speed", &R::AccDiskSpeed, {
                .Label = "Speed", .Group = "Disk",
                .Tooltip = "Angular speed of the inner edge in rad/s of wall time. Gas further out follows Kepler's\n"
                           "law of the active metric, so the disk shears instead of turning rigidly.",
                .Doc = kDoc, .Min = 0.0, .Max = 5.0, .VisibleWhen = "disk.enabled"}));
            registry.Add(Setting("disk.noise_scale", &R::AccDiskNoiseScale, {
                .Label = "Noise scale", .Group = "Disk", .Doc = kDoc, .Min = 0.0, .Max = 5.0,
                .VisibleWhen = "disk.enabled"}));
            registry.Add(Setting("disk.noise_lod", &R::AccDiskNoiseLOD, {
                .Label = "Noise detail", .Group = "Disk", .Tooltip = "Not used by the Straight transport.", .Doc = kDoc,
                .Min = 0.0, .Max = 10.0, .VisibleWhen = "disk.enabled & transport.mode>=1"}));
            registry.Add(Setting("disk.accretion_rate", &R::DiskAccretionRate, {
                .Label = "Accretion rate", .Group = "Disk",
                .Tooltip = "Mass inflow as a fraction of the Eddington rate. Sets the physical temperature of the\n"
                           "Page-Thorne disk together with the hole's mass and the metric's radiative efficiency.",
                .Doc = kDoc, .Min = 1e-4, .Max = 1.0, .Scale = SettingScale::Log, .Format = "%.4f",
                .VisibleWhen = "disk.enabled & transport.mode>=1"}));
            registry.Add(Setting("disk.opacity", &R::DiskOpacity, {
                .Label = "Opacity", .Group = "Disk",
                .Tooltip = "Optical depth straight through the disk. Low: translucent gas, the far side shines through.\n"
                           "High: an opaque glowing surface.",
                .Doc = kDoc, .Min = 0.01, .Max = 100.0, .Scale = SettingScale::Log, .Format = "%.2f",
                .VisibleWhen = "disk.enabled & transport.mode>=1"}));
            registry.Add(Setting("disk.display_temperature", &R::DiskDisplayTemperature, {
                .Label = "Display temperature", .Group = "Disk",
                .Tooltip = "Colour of the hottest gas on screen in K, like an exposure for colour: every temperature is\n"
                           "scaled by the same factor, so ratios, redshift and the radial gradient stay physical.\n"
                           "0 = physical (stellar-mass holes are ~10^7 K and look blue-white).",
                .Doc = kDoc, .Min = 0.0, .Max = 40000.0, .Format = "%.0f K",
                .VisibleWhen = "disk.enabled & transport.mode>=1"}));
            registry.Add(Setting("disk.hot_spots", &R::DiskHotSpots, {
                .Label = "Hot spots", .Group = "Disk",
                .Tooltip = "Hotter clumps of gas on circular geodesics of the metric.",
                .Doc = kDoc, .Min = 0, .Max = 8, .VisibleWhen = "disk.enabled & transport.mode>=1"}));

            registry.Add(Setting("objects.black_holes", &R::RenderBlackHoles, {
                .Label = "Black holes", .Group = "Objects", .Doc = kDoc}));
            registry.Add(Setting("objects.spheres", &R::RenderSpheres, {
                .Label = "Spheres", .Group = "Objects", .Doc = kDoc}));
            registry.Add(Setting("objects.meshes", &R::RenderMeshes, {
                .Label = "Meshes", .Group = "Objects", .Tooltip = "Draw mesh objects (glTF models).",
                .Doc = kDoc, .Cost = 1}));
            registry.Add(Setting("objects.mesh_mode", &R::MeshDrawMode, {
                .Label = "Mesh mode", .Group = "Objects",
                .Tooltip = "Raster: drawn over the traced image, not lensed.\n"
                           "Traced: hit by the tracer's rays (hardware ray queries), so meshes are bent by the hole and\n"
                           "hidden by the shadow, the disk and other objects. The Straight transport always rasterises.",
                .Doc = kDoc,
                .Options = {"Raster", "Traced"},
                .OptionLabels = {"Raster overlay", "Traced (lensed)"},
                .OptionRequires = {"", std::string(GpuCapability::kRayQuery)},
                .OptionCosts = {0, 1},
                .VisibleWhen = "objects.meshes & transport.mode>=1"}));

            registry.Add(Setting("bodies.textures", &R::BodyTextures, {
                .Label = "Surface maps", .Group = "Bodies",
                .Tooltip = "Albedo, normal, roughness and emissive maps of the spheres' Surface component.",
                .Doc = kDoc, .Cost = 1, .Requires = std::string(GpuCapability::kDescriptorIndexing),
                .VisibleWhen = "objects.spheres"}));
            registry.Add(Setting("bodies.texture_max_size", &R::BodyTextureMaxSize, {
                .Label = "Max map size", .Group = "Bodies",
                .Tooltip = "Largest side of a surface map on the GPU in pixels; larger maps drop their top mip levels.\n"
                           "Changing it reloads every map.",
                .Doc = kDoc, .Min = 256, .Max = 16384, .Scale = SettingScale::Log, .Format = "%d px",
                .VisibleWhen = "objects.spheres & bodies.textures"}));
            registry.Add(Setting("bodies.lighting", &R::BodyLightingMode, {
                .Label = "Lighting", .Group = "Bodies",
                .Tooltip = "Fixed: one white light from (1, 1, 1), for looking at a surface.\n"
                           "Probe: each body traces the sky around it every frame (64x32 rays): the disk, stars and other\n"
                           "bodies light it lensed, redshifted and Doppler-shifted by its motion, plus the scene's Light\n"
                           "components. The Straight transport uses the Light components only.",
                .Doc = kDoc, .Cost = 1,
                .Options = {"Fixed", "Probe"},
                .OptionCosts = {0, 1},
                .VisibleWhen = "objects.spheres"}));
            registry.Add(Setting("bodies.atmospheres", &R::BodyAtmospheres, {
                .Label = "Atmospheres", .Group = "Bodies",
                .Tooltip = "Rayleigh, Mie and ozone scattering of the Atmosphere components (Hillaire 2020): blue sky,\n"
                           "limb glow and reddened light at the terminator.",
                .Doc = kDoc, .Cost = 1, .VisibleWhen = "objects.spheres"}));
            registry.Add(Setting("bodies.atmosphere_samples", &R::BodyAtmosphereSamples, {
                .Label = "Atmosphere samples", .Group = "Bodies",
                .Tooltip = "Steps through an atmosphere for each part of a view ray between cloud and ring crossings;\n"
                           "each step also traces 6 steps towards every light.",
                .Doc = kDoc, .Min = 4, .Max = 64, .Cost = 1, .VisibleWhen = "objects.spheres & bodies.atmospheres"}));
            registry.Add(Setting("bodies.terrain_steps", &R::TerrainSteps, {
                .Label = "Terrain steps", .Group = "Bodies",
                .Tooltip = "Steps of the relief march when the camera is near a body with a Terrain (within 20 radii).\n"
                           "Farther away the relief only shades the sphere.",
                .Doc = kDoc, .Min = 8, .Max = 512, .Cost = 1, .VisibleWhen = "objects.spheres"}));

            registry.Add(Setting("camera.third_person", &R::ThirdPerson, {
                .Label = "Third person crosshair", .Group = "Camera", .Doc = kDoc}));
            registry.Add(Setting("camera.third_person_distance", &R::ThirdPersonDistance, {
                .Label = "Distance", .Group = "Camera", .Doc = kDoc, .Min = 1.0, .Max = 100.0,
                .VisibleWhen = "camera.third_person"}));
            registry.Add(Setting("camera.third_person_height", &R::ThirdPersonHeight, {
                .Label = "Height", .Group = "Camera", .Doc = kDoc, .Min = 0.0, .Max = 50.0,
                .VisibleWhen = "camera.third_person"}));
            registry.Add(Setting("camera.observer", &R::Observer, {
                .Label = "Observer", .Group = "Camera",
                .Tooltip = "Motion of the camera relative to the black hole, for the Integrated transport.\n"
                           "Its velocity aberrates the sky and Doppler-shifts the disk.\n"
                           "ZAMO: zero angular momentum, dragged along by the hole's rotation. Exists everywhere.\n"
                           "Static: at rest relative to the distant stars. Impossible inside the ergosphere.\n"
                           "Orbiting: circles the spin axis at the angular velocity of a circular orbit.\n"
                           "Raindrop: falling freely from rest at infinity.\n"
                           "Where the chosen observer cannot exist the camera falls back to the ZAMO.",
                .Doc = kDoc,
                .Options = {"Zamo", "Static", "Orbiting", "Raindrop"},
                .OptionLabels = {"ZAMO", "Static", "Orbiting", "Raindrop (free fall)"},
                .VisibleWhen = "transport.mode>=2"}));
            registry.Add(Setting("camera.observer_orbit", &R::ObserverOrbit, {
                .Label = "Orbit direction", .Group = "Camera",
                .Tooltip = "Prograde orbits turn with the black hole, retrograde ones against it.",
                .Doc = kDoc,
                .Options = {"Prograde", "Retrograde"},
                .VisibleWhen = "transport.mode>=2 & camera.observer=2"}));
            registry.Add(Setting("camera.length_unit", &R::LengthUnit, {
                .Label = "Render length unit", .Group = "Camera",
                .Tooltip = "Unit of the geometrized render space (G = c = 1). Ray step size and the third-person\n"
                           "offsets are in these units.",
                .Doc = kDoc,
                .Options = {"SolarMass", "PrimaryBlackHole"},
                .OptionLabels = {"Solar r_g (legacy numbers)", "Heaviest black hole r_g (scale invariant)"}}));

            registry.Add(Setting("sampling.resolution_scale", &R::ResolutionScale, {
                .Label = "Resolution scale", .Group = "Sampling",
                .Tooltip = "Share of the viewport resolution the tracer renders at. The result is scaled to fit.",
                .Doc = kDoc, .Min = 25.0, .Max = 200.0, .Cost = 4, .Format = "%.0f%%"}));

            registry.Add(Setting("sampling.governor", &R::FrameGovernor, {
                .Label = "Frame-time governor", .Group = "Sampling",
                .Tooltip = "While editing or playing, lowers the resolution scale when the frame time exceeds the budget and\n"
                           "restores it when there is room. Never active in reference views and exports.",
                .Doc = kDoc}));
            registry.Add(Setting("sampling.budget_ms", &R::FrameBudgetMs, {
                .Label = "Frame budget", .Group = "Sampling", .Doc = kDoc, .Min = 4.0, .Max = 500.0,
                .Scale = SettingScale::Log, .Format = "%.1f ms", .VisibleWhen = "sampling.governor"}));

            registry.Add(Setting("post.tonemap", &R::ToneMapping, {
                .Label = "Tone mapping", .Group = "Post",
                .Tooltip = "Maps the HDR radiance of the tracer to the screen. None keeps the raw values (legacy look).",
                .Doc = kDoc,
                .Options = {"None", "Reinhard", "Aces"},
                .OptionLabels = {"None (raw)", "Reinhard", "ACES filmic"}}));
            registry.Add(Setting("post.exposure_mode", &R::Exposure, {
                .Label = "Exposure", .Group = "Post",
                .Tooltip = "Manual: fixed exposure value. Auto: meters the image's log-average luminance each frame.\n"
                           "Physical camera: exposure from aperture, shutter and ISO (EV100 model); the EV below is a compensation.",
                .Doc = kDoc,
                .Options = {"Manual", "Auto", "Physical"},
                .OptionLabels = {"Manual", "Auto", "Physical camera"},
                .VisibleWhen = "post.tonemap>=1"}));
            registry.Add(Setting("post.exposure", &R::ExposureEv, {
                .Label = "Exposure (EV)", .Group = "Post",
                .Tooltip = "Exposure compensation in stops (x2 per stop), applied after metering.",
                .Doc = kDoc, .Min = -10.0, .Max = 10.0, .Format = "%+.1f EV", .VisibleWhen = "post.tonemap>=1"}));
            registry.Add(Setting("post.exposure_key", &R::AutoExposureKey, {
                .Label = "Auto key", .Group = "Post",
                .Tooltip = "Grey level the metered average is mapped to.",
                .Doc = kDoc, .Min = 0.01, .Max = 1.0, .Scale = SettingScale::Log, .Format = "%.2f",
                .VisibleWhen = "post.tonemap>=1 & post.exposure_mode=1"}));
            registry.Add(Setting("post.camera_aperture", &R::CameraAperture, {
                .Label = "Aperture (f/N)", .Group = "Post",
                .Tooltip = "f-number N of the physical camera: exposure scales with 1/N^2.",
                .Doc = kDoc, .Min = 0.7, .Max = 64.0, .Scale = SettingScale::Log, .Format = "f/%.1f",
                .VisibleWhen = "post.tonemap>=1 & post.exposure_mode=2"}));
            registry.Add(Setting("post.camera_shutter", &R::CameraShutter, {
                .Label = "Shutter (s)", .Group = "Post",
                .Tooltip = "Exposure time t of the physical camera in seconds: exposure scales with t.",
                .Doc = kDoc, .Min = 1e-5, .Max = 30.0, .Scale = SettingScale::Log, .Format = "%.5f s",
                .VisibleWhen = "post.tonemap>=1 & post.exposure_mode=2"}));
            registry.Add(Setting("post.camera_iso", &R::CameraIso, {
                .Label = "ISO", .Group = "Post",
                .Tooltip = "Sensor sensitivity S of the physical camera: exposure scales with S / 100.",
                .Doc = kDoc, .Min = 25.0, .Max = 102400.0, .Scale = SettingScale::Log, .Format = "%.0f",
                .VisibleWhen = "post.tonemap>=1 & post.exposure_mode=2"}));
            registry.Add(Setting("post.fxaa", &R::Fxaa, {
                .Label = "Anti-aliasing (FXAA)", .Group = "Post", .Doc = kDoc, .Cost = 1}));
            registry.Add(Setting("post.bloom", &R::Bloom, {
                .Label = "Bloom", .Group = "Post", .Doc = kDoc, .Cost = 2}));
            registry.Add(Setting("post.bloom_threshold", &R::BloomThreshold, {
                .Label = "Threshold", .Group = "Post", .Doc = kDoc, .Min = 0.0, .Max = 5.0,
                .VisibleWhen = "post.bloom"}));
            registry.Add(Setting("post.bloom_passes", &R::BloomBlurPasses, {
                .Label = "Blur passes", .Group = "Post", .Doc = kDoc, .Min = 1, .Max = 20, .Cost = 2,
                .VisibleWhen = "post.bloom"}));
            registry.Add(Setting("post.bloom_intensity", &R::BloomIntensity, {
                .Label = "Intensity", .Group = "Post", .Doc = kDoc, .Min = 0.0, .Max = 100.0,
                .VisibleWhen = "post.bloom"}));
            registry.Add(Setting("post.bloom_debug", &R::BloomDebug, {
                .Label = "Show bloom only", .Group = "Post", .Doc = kDoc, .VisibleWhen = "post.bloom"}));
            registry.Add(Setting("post.lens_flare", &R::LensFlare, {
                .Label = "Lens flare", .Group = "Post", .Doc = kDoc, .Cost = 1}));
            registry.Add(Setting("post.flare_intensity", &R::LensFlareIntensity, {
                .Label = "Flare intensity", .Group = "Post", .Doc = kDoc, .Min = 0.0, .Max = 100.0,
                .VisibleWhen = "post.lens_flare"}));
            registry.Add(Setting("post.flare_threshold", &R::LensFlareThreshold, {
                .Label = "Flare threshold", .Group = "Post", .Doc = kDoc, .Min = 0.0, .Max = 5.0,
                .VisibleWhen = "post.lens_flare"}));

            registry.Add(Setting("lighting.disk_light", &R::DiskLight, {
                .Label = "Disk light", .Group = "Lighting",
                .Tooltip = "The accretion disk lights the meshes: a point source at the primary hole radiating\n"
                           "L = eta Mdot c^2 as a flat Lambertian emitter, shown at the display brightness of the disk.\n"
                           "Approximation: no lensing of the light, no occlusion of the disk by the hole.",
                .Doc = kDoc}));
            registry.Add(Setting("lighting.shadows", &R::Shadows, {
                .Label = "Mesh shadows", .Group = "Lighting",
                .Tooltip = "Shadow rays against the mesh acceleration structure (self shadowing of the ship).\n"
                           "Needs ray queries; without them meshes are lit without shadows.",
                .Doc = kDoc, .Cost = 1}));
            registry.Add(Setting("lighting.sky_radiance", &R::SkyRadiance, {
                .Label = "Sky radiance (W/m2/sr)", .Group = "Lighting",
                .Tooltip = "Uniform ambient radiance on meshes. The default equals the old constant ambient (0.03 image units).",
                .Doc = kDoc, .Min = 0.0, .Max = 100.0, .Format = "%.4f"}));

            registry.Add(Setting("overlay.gravity_grid", &R::ShowGravityGrid, {
                .Label = "Gravity grid", .Group = "Overlays",
                .Tooltip = "Draws the geodesic grid plane as a separate overlay pass.", .Doc = kDoc, .Cost = 1}));
            registry.Add(Setting("overlay.physics_colliders", &R::ShowPhysicsDebug, {
                .Label = "Physics collider wireframe", .Group = "Overlays",
                .Tooltip = "PhysX's own debug wireframe for every active collider.", .Doc = kDoc}));
            registry.Add(Setting("overlay.object_paths", &R::ShowObjectPaths, {
                .Label = "Object path trails", .Group = "Overlays",
                .Tooltip = "Recent world-space path of every mesh (green) and sphere (magenta) while playing.",
                .Doc = kDoc}));
            registry.Add(Setting("overlay.characteristic_radii", &R::ShowCharacteristicRadii, {
                .Label = "Characteristic radii", .Group = "Overlays",
                .Tooltip = "Every black hole's horizon (grey) and ergosphere (purple, in two meridian planes), and the\n"
                           "equatorial photon orbit (orange), marginally bound orbit (green) and ISCO (blue).\n"
                           "Bright: prograde orbits, dim: retrograde.\n"
                           "Integrated transport: the tracer draws the rings where rays cross the equator, so they are\n"
                           "lensed like the disk (with secondary images) and the ergoregion is tinted; otherwise they are\n"
                           "plain lines, which sit inside the lensed shadow.",
                .Doc = kDoc}));
            registry.Add(Setting("overlay.predicted_orbits", &R::ShowPredictedOrbits, {
                .Label = "Predicted orbits", .Group = "Overlays",
                .Tooltip = "Where every Geodesic Body will go, integrated ahead from its current state: bright now,\n"
                           "fading towards the end of the span. Drawn without lensing.",
                .Doc = kDoc}));
            registry.Add(Setting("overlay.prediction_span", &R::PredictionSpan, {
                .Label = "Prediction span", .Group = "Orbit overlays",
                .Tooltip = "Coordinate time to predict ahead, in M (GM/c³ of each body's black hole).",
                .Doc = kDoc, .Min = 10.0, .Max = 100000.0, .Scale = SettingScale::Log, .Format = "%.0f M",
                .VisibleWhen = "overlay.predicted_orbits"}));

            registry.Add(Setting("debug.view", &R::Debug, {
                .Label = "Debug view", .Group = "Debug",
                .Tooltip = "Replaces the image by a diagnostic of the tracer.\n"
                           "Step count: heat map of ray march steps per pixel.\n"
                           "Termination: why each ray ended (sky, horizon, object, disk, ran out of steps).\n"
                           "Constraint drift (integrated only): how far each ray strays from a light path.\n"
                           "Ergosphere & ISCO (integrated only): ergosphere tinted, innermost stable orbit as a ring.",
                .Doc = kDoc,
                .Options = {"None", "StepCount", "Termination", "ConstraintDrift", "Ergosphere"},
                .OptionLabels = {"Off", "Step count heat map", "Termination reason", "Geodesic constraint drift",
                                 "Ergosphere & ISCO"}}));

            return registry;
        }

        RenderProfile MakeBuiltIn(std::string name, std::string description, RenderSettings settings)
        {
            RenderProfile profile;
            profile.Name = std::move(name);
            profile.Description = std::move(description);
            profile.BuiltIn = true;
            profile.Settings = settings;
            return profile;
        }

        std::vector<RenderProfile> BuildBuiltIns()
        {
            std::vector<RenderProfile> profiles;

            RenderSettings flat;
            flat.TransportMode = Transport::Straight;
            flat.Bloom = false;
            flat.LensFlare = false;
            flat.MeshDrawMode = MeshMode::Raster;
            profiles.push_back(MakeBuiltIn("Flat", "Straight rays, analytic shadow and disk, no lensing, no bloom. Fast, for editing and placing objects.", flat));

            profiles.push_back(MakeBuiltIn("Lensed", "Analytic lens with bloom and flares. The interactive default.", RenderSettings{}));

            RenderSettings traced;
            traced.TransportMode = Transport::Integrated;
            traced.RayStepSize = 0.05f;
            traced.MaxRaySteps = 4000;
            profiles.push_back(MakeBuiltIn("Traced", "Geodesic integration at preview tolerance. Cinematic preview, seconds per frame.", traced));

            RenderSettings reference;
            reference.TransportMode = Transport::Integrated;
            reference.AccretionDiskVolumetric = true;
            reference.LensFlare = false;
            reference.RayStepSize = 0.01f;
            reference.MaxRaySteps = 100000;
            profiles.push_back(MakeBuiltIn("Reference", "Geodesic integration at reference tolerance with the volumetric disk. For final renders.", reference));

            return profiles;
        }

        constexpr std::array<std::string_view, kRenderContextCount> kContextNames{"Edit", "Play", "Reference", "Export"};
        constexpr std::array<std::string_view, kRenderContextCount> kContextLabels{
            "Viewport: editing", "Viewport: playing", "Viewport: reference", "Export"};
        constexpr std::array<std::string_view, kRenderContextCount> kDefaultProfiles{"Flat", "Lensed", "Reference", "Lensed"};

        template <typename T>
        void Read(const YAML::Node& node, const char* key, T& out)
        {
            if (const auto child = node[key]; child && child.IsScalar())
            {
                try { out = child.template as<T>(); } catch (const std::exception&) {}
            }
        }
    }

    const SettingRegistry<RenderSettings>& RenderSettingRegistry()
    {
        static const auto registry = BuildRegistry();
        return registry;
    }

    std::vector<std::string> SanitizeForDevice(RenderSettings& settings, const GpuCapabilities& caps)
    {
        std::vector<std::string> notes;
        const RenderSettings defaults;
        for (const auto& setting : RenderSettingRegistry().All())
        {
            const auto required = setting.RequiresFor(settings);
            if (caps.Has(required)) continue;

            const auto before = RenderSettingRegistry().ToText(settings, setting);
            setting.Set(settings, setting.Get(defaults));
            if (!caps.Has(setting.RequiresFor(settings)))
            {
                setting.Set(settings, setting.Type == FieldType::Bool ? FieldValue{false} : FieldValue{0});
            }
            notes.push_back(std::format("{}: '{}' needs {}, using '{}'", setting.Meta.Label, before,
                                        CapabilityLabel(required), RenderSettingRegistry().ToText(settings, setting)));
        }
        return notes;
    }

    std::string_view RenderContextName(const RenderContext context) noexcept
    {
        return kContextNames[static_cast<std::size_t>(context)];
    }

    std::string_view RenderContextLabel(const RenderContext context) noexcept
    {
        return kContextLabels[static_cast<std::size_t>(context)];
    }

    std::optional<RenderContext> RenderContextFromName(const std::string_view name) noexcept
    {
        for (int i = 0; i < kRenderContextCount; ++i)
        {
            if (kContextNames[static_cast<std::size_t>(i)] == name) return static_cast<RenderContext>(i);
        }
        return std::nullopt;
    }

    const std::vector<RenderProfile>& BuiltInRenderProfiles()
    {
        static const auto profiles = BuildBuiltIns();
        return profiles;
    }

    RenderSettings LegacyRenderSettings(const YAML::Node& state)
    {
        RenderSettings out;
        bool lensing = true;
        bool accurate = false;
        Read(state, "GravitationalLensing", lensing);
        Read(state, "PhysicallyAccurate", accurate);
        out.TransportMode = accurate ? Transport::Integrated : lensing ? Transport::AnalyticLens : Transport::Straight;

        int metric = static_cast<int>(out.Solution);
        Read(state, "MetricType", metric);
        out.Solution = static_cast<Spacetime>(std::clamp(metric, 0, 3));
        int frame = static_cast<int>(out.LengthUnit);
        Read(state, "FrameMode", frame);
        out.LengthUnit = static_cast<FrameMode>(std::clamp(frame, 0, 1));

        Read(state, "GravitationalRedshift", out.GravitationalRedshift);
        Read(state, "DopplerBeaming", out.DopplerBeaming);
        Read(state, "AccretionDisk", out.AccretionDisk);
        Read(state, "AccretionDiskVolumetric", out.AccretionDiskVolumetric);
        Read(state, "AccDiskHeight", out.AccDiskHeight);
        Read(state, "AccDiskSpeed", out.AccDiskSpeed);
        Read(state, "AccDiskNoiseScale", out.AccDiskNoiseScale);
        Read(state, "AccDiskNoiseLOD", out.AccDiskNoiseLOD);
        Read(state, "RenderBlackHoles", out.RenderBlackHoles);
        Read(state, "RenderSpheres", out.RenderSpheres);
        Read(state, "RayStepSize", out.RayStepSize);
        Read(state, "MaxRaySteps", out.MaxRaySteps);
        Read(state, "AdaptiveStepRate", out.AdaptiveStepRate);
        Read(state, "AntiAliasing", out.Fxaa);
        Read(state, "BloomEnabled", out.Bloom);
        Read(state, "BloomThreshold", out.BloomThreshold);
        Read(state, "BloomBlurPasses", out.BloomBlurPasses);
        Read(state, "BloomIntensity", out.BloomIntensity);
        Read(state, "BloomDebug", out.BloomDebug);
        Read(state, "LensFlare", out.LensFlare);
        Read(state, "LensFlareIntensity", out.LensFlareIntensity);
        Read(state, "LensFlareThreshold", out.LensFlareThreshold);
        Read(state, "ThirdPerson", out.ThirdPerson);
        Read(state, "ThirdPersonDistance", out.ThirdPersonDistance);
        Read(state, "ThirdPersonHeight", out.ThirdPersonHeight);
        Read(state, "ShowGravityGrid", out.ShowGravityGrid);
        Read(state, "ShowPhysicsDebug", out.ShowPhysicsDebug);
        Read(state, "ShowObjectPaths", out.ShowObjectPaths);
        std::string precision;
        Read(state, "GpuPrecision", precision);
        if (const auto parsed = Units::PrecisionFromName(precision); parsed && *parsed != Units::Precision::F128)
            out.GpuPrecision = *parsed;

        RenderSettingRegistry().Clamp(out);
        return out;
    }

    std::string RenderExtensionText(const Scene& scene)
    {
        const auto* node = scene.FindExtension(kRenderExtensionKey);
        return node ? YAML::Dump(*node) : std::string();
    }

    void StoreRenderExtension(Scene& scene, const std::string& yaml)
    {
        if (yaml.empty())
        {
            scene.RemoveExtension(kRenderExtensionKey);
            return;
        }
        scene.SetExtension(kRenderExtensionKey, YAML::Load(yaml));
    }

    RenderProfileSet::RenderProfileSet()
        : m_Profiles(BuiltInRenderProfiles())
    {
        for (int i = 0; i < kRenderContextCount; ++i) m_Context[static_cast<std::size_t>(i)] = kDefaultProfiles[static_cast<std::size_t>(i)];
    }

    const RenderProfile* RenderProfileSet::Find(const std::string_view name) const
    {
        const auto it = std::ranges::find(m_Profiles, name, &RenderProfile::Name);
        return it == m_Profiles.end() ? nullptr : &*it;
    }

    const RenderProfile& RenderProfileSet::ProfileFor(const RenderContext context) const
    {
        if (const auto* profile = Find(m_Context[static_cast<std::size_t>(context)])) return *profile;
        return *Find(kDefaultProfiles[static_cast<std::size_t>(context)]);
    }

    bool RenderProfileSet::Assign(const RenderContext context, const std::string_view profileName)
    {
        if (!Find(profileName)) return false;
        m_Context[static_cast<std::size_t>(context)] = std::string(profileName);
        return true;
    }

    std::string RenderProfileSet::UniqueName(const std::string_view wanted) const
    {
        if (!Find(wanted)) return std::string(wanted);
        for (int suffix = 2;; ++suffix)
        {
            auto candidate = std::format("{} {}", wanted, suffix);
            if (!Find(candidate)) return candidate;
        }
    }

    const RenderProfile& RenderProfileSet::Replace(const RenderContext context, const RenderSettings& settings)
    {
        const auto& current = ProfileFor(context);
        if (!current.BuiltIn)
        {
            auto& profile = *std::ranges::find(m_Profiles, current.Name, &RenderProfile::Name);
            profile.Settings = settings;
            return profile;
        }
        RenderProfile fork;
        fork.Name = UniqueName(current.Name + " (edited)");
        fork.Description = current.Description;
        fork.Base = current.Name;
        fork.Settings = settings;
        m_Profiles.push_back(std::move(fork));
        m_Context[static_cast<std::size_t>(context)] = m_Profiles.back().Name;
        return m_Profiles.back();
    }

    const RenderProfile& RenderProfileSet::Duplicate(const RenderContext context)
    {
        const auto& current = ProfileFor(context);
        RenderProfile copy;
        copy.Name = UniqueName(current.Name + " copy");
        copy.Description = current.Description;
        copy.Base = current.BuiltIn ? current.Name : current.Base;
        copy.Settings = current.Settings;
        m_Profiles.push_back(std::move(copy));
        m_Context[static_cast<std::size_t>(context)] = m_Profiles.back().Name;
        return m_Profiles.back();
    }

    void RenderProfileSet::Add(RenderProfile profile)
    {
        profile.BuiltIn = false;
        profile.Name = UniqueName(profile.Name);
        m_Profiles.push_back(std::move(profile));
    }

    bool RenderProfileSet::Remove(const std::string_view name)
    {
        const auto it = std::ranges::find(m_Profiles, name, &RenderProfile::Name);
        if (it == m_Profiles.end() || it->BuiltIn) return false;
        m_Profiles.erase(it);
        for (int i = 0; i < kRenderContextCount; ++i)
        {
            auto& assigned = m_Context[static_cast<std::size_t>(i)];
            if (assigned == name) assigned = std::string(kDefaultProfiles[static_cast<std::size_t>(i)]);
        }
        return true;
    }

    bool RenderProfileSet::Rename(const std::string_view name, const std::string_view newName)
    {
        if (newName.empty() || newName == name || Find(newName)) return false;
        const auto it = std::ranges::find(m_Profiles, name, &RenderProfile::Name);
        if (it == m_Profiles.end() || it->BuiltIn) return false;
        it->Name = std::string(newName);
        for (auto& assigned : m_Context)
        {
            if (assigned == name) assigned = std::string(newName);
        }
        return true;
    }

    YAML::Node RenderProfileSet::ToYaml() const
    {
        YAML::Node node;
        node["Version"] = kRenderExtensionVersion;
        YAML::Node contexts(YAML::NodeType::Map);
        for (int i = 0; i < kRenderContextCount; ++i)
        {
            contexts[std::string(kContextNames[static_cast<std::size_t>(i)])] = m_Context[static_cast<std::size_t>(i)];
        }
        node["Contexts"] = contexts;

        YAML::Node profiles(YAML::NodeType::Sequence);
        for (const auto& profile : m_Profiles)
        {
            if (profile.BuiltIn) continue;
            YAML::Node entry;
            entry["Name"] = profile.Name;
            if (!profile.Description.empty()) entry["Description"] = profile.Description;
            const auto* base = Find(profile.Base);
            entry["Base"] = base && base->BuiltIn ? base->Name : std::string("Lensed");
            const RenderSettings baseline = base && base->BuiltIn ? base->Settings : RenderSettings{};
            entry["Set"] = RenderSettingRegistry().Encode(profile.Settings, &baseline);
            profiles.push_back(entry);
        }
        if (profiles.size() > 0) node["Profiles"] = profiles;
        return node;
    }

    RenderProfileSet RenderProfileSet::FromYaml(const YAML::Node& node, std::vector<std::string>* warnings)
    {
        RenderProfileSet set;
        const auto warn = [&](std::string message)
        {
            if (warnings) warnings->push_back(std::move(message));
        };
        if (!node || !node.IsMap()) return set;

        if (const auto profiles = node["Profiles"]; profiles && profiles.IsSequence())
        {
            for (const auto& entry : profiles)
            {
                const auto name = entry["Name"] ? entry["Name"].as<std::string>("") : std::string();
                if (name.empty())
                {
                    warn("render profile without a name skipped");
                    continue;
                }
                if (set.Find(name))
                {
                    warn("render profile '" + name + "' duplicates an existing name, skipped");
                    continue;
                }
                RenderProfile profile;
                profile.Name = name;
                profile.Description = entry["Description"] ? entry["Description"].as<std::string>("") : std::string();
                const auto baseName = entry["Base"] ? entry["Base"].as<std::string>("Lensed") : std::string("Lensed");
                const auto* base = set.Find(baseName);
                if (!base || !base->BuiltIn)
                {
                    warn("render profile '" + name + "' has unknown base '" + baseName + "', using Lensed");
                    base = set.Find("Lensed");
                }
                profile.Base = base->Name;
                profile.Settings = base->Settings;
                for (const auto& key : RenderSettingRegistry().Decode(profile.Settings, entry["Set"]))
                {
                    warn("render profile '" + name + "': setting '" + key + "' ignored");
                }
                set.m_Profiles.push_back(std::move(profile));
            }
        }

        if (const auto contexts = node["Contexts"]; contexts && contexts.IsMap())
        {
            for (const auto& entry : contexts)
            {
                const auto context = RenderContextFromName(entry.first.as<std::string>(""));
                const auto profileName = entry.second.as<std::string>("");
                if (!context) continue;
                if (!set.Assign(*context, profileName))
                {
                    warn("context '" + std::string(RenderContextName(*context)) + "' names unknown profile '"
                         + profileName + "', using the default");
                }
            }
        }
        return set;
    }
}
