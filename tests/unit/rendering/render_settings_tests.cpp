#include <catch2/catch_test_macros.hpp>
#include <yaml-cpp/yaml.h>

import GPP;
import MoleHole;
import vulkan;
import std;

using namespace GPP;
using namespace MoleHole;

namespace
{
    GpuCapabilities FullGpu()
    {
        GpuCapabilities caps;
        caps.DeviceName = "Test GPU";
        caps.ShaderFloat64 = true;
        caps.RayQuery = true;
        caps.AccelerationStructure = true;
        return caps;
    }

    std::string TextOf(const RenderProfileSet& set) { return YAML::Dump(set.ToYaml()); }
}

TEST_CASE("Every render setting is declared once with a label, a group and sane limits", "[rendering][settings]")
{
    const auto& registry = RenderSettingRegistry();
    std::set<std::string> keys;
    for (const auto& setting : registry.All())
    {
        INFO(setting.Key);
        CHECK(keys.insert(setting.Key).second);
        CHECK(setting.Key.find('.') != std::string::npos);
        CHECK_FALSE(setting.Meta.Label.empty());
        CHECK_FALSE(setting.Meta.Group.empty());
        CHECK(setting.Meta.Cost >= 0);
        CHECK(setting.Meta.Cost <= 5);
        if (setting.Type == FieldType::Enum)
        {
            CHECK_FALSE(setting.Meta.Options.empty());
            CHECK((setting.Meta.OptionLabels.empty() || setting.Meta.OptionLabels.size() == setting.Meta.Options.size()));
            CHECK((setting.Meta.OptionRequires.empty() || setting.Meta.OptionRequires.size() == setting.Meta.Options.size()));
            CHECK((setting.Meta.OptionCosts.empty() || setting.Meta.OptionCosts.size() == setting.Meta.Options.size()));
        }
        for (const auto clause : std::views::split(setting.Meta.VisibleWhen, '&'))
        {
            auto dependency = std::string(std::string_view(clause));
            dependency = dependency.substr(0, dependency.find_first_of("=!>"));
            std::erase(dependency, ' ');
            CHECK(registry.Find(dependency) != nullptr);
        }
    }
    CHECK(registry.Groups().front() == "Spacetime");
}

TEST_CASE("Default settings survive a YAML round trip and a text round trip", "[rendering][settings]")
{
    const auto& registry = RenderSettingRegistry();
    RenderSettings changed;
    changed.TransportMode = Transport::Integrated;
    changed.Solution = Spacetime::KerrNewman;
    changed.GpuPrecision = Units::Precision::F64;
    changed.RayStepSize = 0.05f;
    changed.MaxRaySteps = 4000;
    changed.Bloom = false;
    changed.LengthUnit = FrameMode::SolarMass;
    changed.Debug = DebugView::Termination;
    changed.ResolutionScale = 62.5f;

    RenderSettings loaded;
    CHECK(registry.Decode(loaded, YAML::Load(YAML::Dump(registry.Encode(changed)))).empty());
    CHECK(registry.Diff(changed, loaded).empty());

    RenderSettings parsed;
    for (const auto& setting : registry.All())
    {
        INFO(setting.Key);
        REQUIRE(registry.SetFromText(parsed, setting.Key, registry.ToText(changed, setting)));
    }
    CHECK(registry.Diff(changed, parsed).empty());
}

TEST_CASE("The Lensed built-in equals the struct defaults so profiles can be saved as diffs", "[rendering][profiles]")
{
    const RenderProfileSet set;
    CHECK(RenderSettingRegistry().Diff(set.Find("Lensed")->Settings, RenderSettings{}).empty());
    CHECK(set.Find("Flat")->Settings.TransportMode == Transport::Straight);
    CHECK(set.Find("Traced")->Settings.TransportMode == Transport::Integrated);
    CHECK(set.Find("Reference")->Settings.AccretionDiskVolumetric);
    for (const auto& profile : set.Profiles()) CHECK(profile.BuiltIn);

    const auto& flat = set.Find("Flat")->Settings;
    CHECK_FALSE(flat.UsesBloomChain());
    CHECK(flat.Fxaa);
}

TEST_CASE("The Straight transport hides the disk settings its analytic disk ignores", "[rendering][settings]")
{
    const auto& registry = RenderSettingRegistry();
    const auto visible = [&](const RenderSettings& settings, const char* key)
    {
        return registry.IsVisible(settings, *registry.Find(key));
    };
    RenderSettings flat = RenderProfileSet().Find("Flat")->Settings;
    for (const auto* key : {"disk.volumetric", "disk.height", "disk.noise_lod"}) CHECK_FALSE(visible(flat, key));
    for (const auto* key : {"disk.enabled", "disk.speed", "disk.noise_scale", "radiometry.redshift", "radiometry.doppler"})
        CHECK(visible(flat, key));

    const RenderSettings lensed;
    for (const auto* key : {"disk.volumetric", "disk.height", "disk.noise_lod"}) CHECK(visible(lensed, key));
    RenderSettings noDisk;
    noDisk.AccretionDisk = false;
    CHECK_FALSE(visible(noDisk, "disk.volumetric"));
}

TEST_CASE("The trace stage runs the analytic kernel for straight rays and the marcher otherwise", "[rendering][pipeline]")
{
    RenderSettings settings;
    settings.TransportMode = Transport::Straight;
    settings.GpuPrecision = Units::Precision::F64;
    CHECK(SelectTraceKernel(settings, true) == TraceKernel::Flat);
    settings.TransportMode = Transport::AnalyticLens;
    CHECK(SelectTraceKernel(settings, true) == TraceKernel::Marcher);
    settings.TransportMode = Transport::Integrated;
    CHECK(SelectTraceKernel(settings, true) == TraceKernel::MarcherF64);
    CHECK(SelectTraceKernel(settings, false) == TraceKernel::Marcher);
    settings.GpuPrecision = Units::Precision::F32;
    CHECK(SelectTraceKernel(settings, true) == TraceKernel::Marcher);
    CHECK(SelectTraceKernel(settings, true, true) == TraceKernel::MarcherMeshes);
    settings.GpuPrecision = Units::Precision::F64;
    CHECK(SelectTraceKernel(settings, true, true) == TraceKernel::MarcherMeshesF64);
    CHECK(SelectTraceKernel(settings, false, true) == TraceKernel::MarcherMeshes);
    settings.TransportMode = Transport::Straight;
    CHECK(SelectTraceKernel(settings, true, true) == TraceKernel::Flat);
}

TEST_CASE("Meshes are traced in the lensed profiles and rasterised for straight rays or without ray queries", "[rendering][pipeline]")
{
    const RenderProfileSet set;
    CHECK_FALSE(WantsTracedMeshes(set.Find("Flat")->Settings));
    CHECK(WantsTracedMeshes(set.Find("Lensed")->Settings));
    CHECK(WantsTracedMeshes(set.Find("Traced")->Settings));
    CHECK(WantsTracedMeshes(set.Find("Reference")->Settings));

    RenderSettings settings;
    settings.RenderMeshes = false;
    CHECK_FALSE(WantsTracedMeshes(settings));
    settings.RenderMeshes = true;
    settings.TransportMode = Transport::Straight;
    CHECK_FALSE(WantsTracedMeshes(settings));

    RenderSettings lensed;
    auto caps = FullGpu();
    caps.Disable(GpuCapability::kRayQuery);
    const auto notes = SanitizeForDevice(lensed, caps);
    REQUIRE(notes.size() == 1);
    CHECK(lensed.MeshDrawMode == MeshMode::Raster);
    CHECK_FALSE(WantsTracedMeshes(lensed));
}

TEST_CASE("Default contexts follow the architecture table, export keeps today's look", "[rendering][profiles]")
{
    const RenderProfileSet set;
    CHECK(set.ProfileName(RenderContext::Edit) == "Flat");
    CHECK(set.ProfileName(RenderContext::Play) == "Lensed");
    CHECK(set.ProfileName(RenderContext::Reference) == "Reference");
    CHECK(set.ProfileName(RenderContext::Export) == "Lensed");
    CHECK(RenderContextFromName("Reference") == RenderContext::Reference);
    CHECK_FALSE(RenderContextFromName("nope"));
}

TEST_CASE("Editing a built-in forks it, editing a user profile changes it in place", "[rendering][profiles]")
{
    RenderProfileSet set;
    auto edited = set.SettingsFor(RenderContext::Edit);
    edited.Bloom = true;

    const auto& fork = set.Replace(RenderContext::Edit, edited);
    CHECK_FALSE(fork.BuiltIn);
    CHECK(fork.Name == "Flat (edited)");
    CHECK(fork.Base == "Flat");
    CHECK(set.ProfileName(RenderContext::Edit) == "Flat (edited)");
    CHECK_FALSE(set.Find("Flat")->Settings.Bloom);

    edited.Bloom = false;
    edited.BloomIntensity = 9.0f;
    set.Replace(RenderContext::Edit, edited);
    CHECK(set.Profiles().size() == 5);
    CHECK(set.Find("Flat (edited)")->Settings.BloomIntensity == 9.0f);
}

TEST_CASE("Duplicate, rename and remove keep the contexts pointing at real profiles", "[rendering][profiles]")
{
    RenderProfileSet set;
    set.Duplicate(RenderContext::Play);
    CHECK(set.ProfileName(RenderContext::Play) == "Lensed copy");
    set.Duplicate(RenderContext::Play);
    CHECK(set.ProfileName(RenderContext::Play) == "Lensed copy copy");
    REQUIRE(set.Find("Lensed copy copy"));
    CHECK(set.Find("Lensed copy copy")->Base == "Lensed");

    CHECK(set.Rename("Lensed copy copy", "Cinematic"));
    CHECK(set.ProfileName(RenderContext::Play) == "Cinematic");
    CHECK_FALSE(set.Rename("Cinematic", "Flat"));
    CHECK_FALSE(set.Rename("Flat", "Mine"));
    CHECK_FALSE(set.Rename("Cinematic", ""));

    CHECK_FALSE(set.Remove("Flat"));
    CHECK(set.Remove("Cinematic"));
    CHECK(set.ProfileName(RenderContext::Play) == "Lensed");
    CHECK_FALSE(set.Remove("Cinematic"));
    CHECK_FALSE(set.Assign(RenderContext::Edit, "missing"));
}

TEST_CASE("A profile set is saved as the user profiles' differences from their base", "[rendering][profiles]")
{
    RenderProfileSet set;
    auto traced = set.SettingsFor(RenderContext::Reference);
    traced.RayStepSize = 0.02f;
    set.Assign(RenderContext::Export, "Reference");
    set.Replace(RenderContext::Export, traced);

    const auto node = set.ToYaml();
    CHECK(node["Version"].as<int>() == kRenderExtensionVersion);
    CHECK(node["Contexts"]["Export"].as<std::string>() == "Reference (edited)");
    REQUIRE(node["Profiles"].size() == 1);
    CHECK(node["Profiles"][0]["Base"].as<std::string>() == "Reference");
    CHECK(node["Profiles"][0]["Set"].size() == 1);
    CHECK(node["Profiles"][0]["Set"]["transport.step_size"].as<float>() == 0.02f);

    std::vector<std::string> warnings;
    const auto loaded = RenderProfileSet::FromYaml(YAML::Load(YAML::Dump(node)), &warnings);
    CHECK(warnings.empty());
    CHECK(TextOf(loaded) == TextOf(set));
    CHECK(loaded.SettingsFor(RenderContext::Export).RayStepSize == 0.02f);
    CHECK(loaded.SettingsFor(RenderContext::Export).AccretionDiskVolumetric);
}

TEST_CASE("Loading a profile set tolerates stale or hand-edited YAML", "[rendering][profiles]")
{
    const auto node = YAML::Load(R"(
Version: 1
Contexts: {Edit: Gone, Play: Mine, Weird: Flat, Export: Lensed}
Profiles:
  - {Name: Mine, Base: Traced, Set: {post.bloom: false, no.such.key: 3, transport.max_steps: 99999999}}
  - {Name: Flat, Base: Lensed}
  - {Name: Orphan, Base: Vanished}
  - {Description: nameless}
)");
    std::vector<std::string> warnings;
    const auto set = RenderProfileSet::FromYaml(node, &warnings);

    CHECK(set.ProfileName(RenderContext::Edit) == "Flat");
    CHECK(set.ProfileName(RenderContext::Play) == "Mine");
    const auto& mine = *set.Find("Mine");
    CHECK(mine.Settings.TransportMode == Transport::Integrated);
    CHECK_FALSE(mine.Settings.Bloom);
    CHECK(mine.Settings.MaxRaySteps == 200000);
    CHECK(set.Find("Orphan")->Base == "Lensed");
    CHECK(set.Find("Flat")->BuiltIn);
    CHECK(warnings.size() >= 5);

    CHECK(RenderProfileSet::FromYaml(YAML::Node()).Profiles().size() == 4);
    CHECK(RenderProfileSet::FromYaml(YAML::Load("[1, 2]")).Profiles().size() == 4);
}

TEST_CASE("Old molehole_state.yaml render toggles migrate into one profile", "[rendering][migration]")
{
    const auto state = YAML::Load(R"(
GravitationalLensing: false
PhysicallyAccurate: false
MetricType: 3
FrameMode: 0
AccretionDiskVolumetric: true
AccDiskHeight: 0.153
RayStepSize: 0.05
MaxRaySteps: 5
BloomEnabled: false
AntiAliasing: false
ShowGravityGrid: true
GpuPrecision: f64
)");
    const auto legacy = LegacyRenderSettings(state);
    CHECK(legacy.TransportMode == Transport::Straight);
    CHECK(legacy.Solution == Spacetime::KerrNewman);
    CHECK(legacy.LengthUnit == FrameMode::SolarMass);
    CHECK(legacy.AccretionDiskVolumetric);
    CHECK(legacy.AccDiskHeight == 0.153f);
    CHECK(legacy.RayStepSize == 0.05f);
    CHECK(legacy.MaxRaySteps == 100);
    CHECK_FALSE(legacy.Bloom);
    CHECK_FALSE(legacy.Fxaa);
    CHECK(legacy.ShowGravityGrid);
    CHECK(legacy.GpuPrecision == Units::Precision::F64);

    CHECK(LegacyRenderSettings(YAML::Load("{PhysicallyAccurate: true, GravitationalLensing: true}")).TransportMode
          == Transport::Integrated);
    CHECK(LegacyRenderSettings(YAML::Load("{GravitationalLensing: true}")).TransportMode == Transport::AnalyticLens);
    CHECK(LegacyRenderSettings(YAML::Load("{MetricType: 42}")).Solution == Spacetime::KerrNewman);
    CHECK(LegacyRenderSettings(YAML::Load("{GpuPrecision: f128}")).GpuPrecision == Units::Precision::F32);
    CHECK(RenderSettingRegistry().Diff(LegacyRenderSettings(YAML::Node()), RenderSettings{}).empty());
}

TEST_CASE("Settings a device cannot run fall back with a note", "[rendering][capabilities]")
{
    RenderSettings settings;
    settings.TransportMode = Transport::Integrated;
    settings.GpuPrecision = Units::Precision::F64;

    auto unchanged = settings;
    CHECK(SanitizeForDevice(unchanged, FullGpu()).empty());
    CHECK(unchanged.GpuPrecision == Units::Precision::F64);

    auto caps = FullGpu();
    caps.Disable(GpuCapability::kShaderFloat64);
    const auto notes = SanitizeForDevice(settings, caps);
    REQUIRE(notes.size() == 1);
    CHECK(settings.GpuPrecision == Units::Precision::F32);
    CHECK(notes[0].find("64-bit") != std::string::npos);
    CHECK(settings.TransportMode == Transport::Integrated);

    RenderSettings quad;
    quad.GpuPrecision = Units::Precision::F128;
    CHECK(SanitizeForDevice(quad, FullGpu()).size() == 1);
    CHECK(quad.GpuPrecision == Units::Precision::F32);
}

TEST_CASE("The service resolves settings for the active context", "[rendering][service]")
{
    RenderSettingsService service;
    service.SetCapabilities(FullGpu());
    CHECK(service.ActiveContext() == RenderContext::Edit);
    CHECK(service.Effective().TransportMode == Transport::Straight);

    service.SetActiveContext(RenderContext::Play);
    CHECK(service.Effective().TransportMode == Transport::AnalyticLens);
    service.SetActiveContext(RenderContext::Reference);
    CHECK(service.Effective().TransportMode == Transport::Integrated);
    CHECK(service.Effective().AccretionDiskVolumetric);
}

TEST_CASE("Edits fork a built-in, are written to the scene once, and survive a scene reload from YAML", "[rendering][service]")
{
    RenderSettingsService service;
    CHECK_FALSE(service.SceneWritePending());
    CHECK_FALSE(service.PersistsToScene());

    auto edited = service.Profiles().SettingsFor(RenderContext::Edit);
    edited.Bloom = true;
    service.Edit(RenderContext::Edit, edited);
    CHECK(service.Effective().Bloom);
    CHECK(service.PersistsToScene());
    REQUIRE(service.SceneWritePending());

    const auto written = service.TakeSceneWrite();
    REQUIRE(written);
    CHECK_FALSE(service.SceneWritePending());
    CHECK_FALSE(service.TakeSceneWrite());

    // Equal settings are not an edit
    service.Edit(RenderContext::Edit, service.Profiles().SettingsFor(RenderContext::Edit));
    CHECK_FALSE(service.SceneWritePending());

    RenderSettingsService other;
    other.LoadScene(*written);
    CHECK(other.Effective().Bloom);
    CHECK(other.Profiles().ProfileName(RenderContext::Edit) == "Flat (edited)");
    CHECK(other.PersistsToScene());
    CHECK_FALSE(other.SceneWritePending());
}

TEST_CASE("A scene without render settings starts from the app defaults", "[rendering][service]")
{
    RenderSettingsService service;
    RenderProfileSet defaults;
    auto settings = defaults.SettingsFor(RenderContext::Play);
    settings.RayStepSize = 0.5f;
    defaults.Replace(RenderContext::Play, settings);

    service.SetDefaults(defaults);
    service.SetActiveContext(RenderContext::Play);
    CHECK(service.Effective().RayStepSize == 0.5f); // before any scene is loaded

    auto edited = service.Profiles().SettingsFor(RenderContext::Edit);
    edited.Bloom = true;
    service.Edit(RenderContext::Edit, edited);

    service.LoadScene({});
    CHECK(service.Effective().RayStepSize == 0.5f);
    CHECK_FALSE(service.Profiles().SettingsFor(RenderContext::Edit).Bloom);
    CHECK_FALSE(service.PersistsToScene());

    service.UseCurrentAsDefaults();
    CHECK(service.Defaults().ProfileName(RenderContext::Play) == "Lensed (edited)");
}

TEST_CASE("Unreadable scene YAML falls back to defaults and reports it", "[rendering][service]")
{
    RenderSettingsService service;
    service.LoadScene("{ this is: [not closed");
    CHECK(service.Profiles().ProfileName(RenderContext::Edit) == "Flat");
    CHECK_FALSE(service.Warnings().empty());
}

TEST_CASE("Export overrides and --profile apply only where they should", "[rendering][service]")
{
    RenderSettingsService service;
    service.SetExportOverrides({{"transport.mode", "Integrated"}, {"post.bloom", "off"}, {"bad.key", "1"}});

    service.SetActiveContext(RenderContext::Play);
    CHECK(service.Effective().TransportMode == Transport::AnalyticLens);
    CHECK(service.Effective().Bloom);

    service.SetActiveContext(RenderContext::Export);
    CHECK(service.Effective().TransportMode == Transport::Integrated);
    CHECK_FALSE(service.Effective().Bloom);

    service.SetExportOverrides({});
    CHECK(service.Effective().TransportMode == Transport::AnalyticLens);

    service.SetProfileOverride("Reference");
    CHECK(service.ProfileOverridden());
    CHECK(service.ProfileFor(RenderContext::Edit).Name == "Reference");
    CHECK(service.Effective().TransportMode == Transport::Integrated);
    service.SetProfileOverride("missing");
    CHECK_FALSE(service.ProfileOverridden());
    CHECK(service.Effective().TransportMode == Transport::AnalyticLens);
}

TEST_CASE("The effective settings never ask the device for what it lacks", "[rendering][service]")
{
    RenderSettingsService service;
    auto caps = FullGpu();
    caps.Disable(GpuCapability::kShaderFloat64);
    service.SetCapabilities(caps);
    service.SetExportOverrides({{"transport.gpu_precision", "f64"}});
    service.SetActiveContext(RenderContext::Export);
    CHECK(service.Effective().GpuPrecision == Units::Precision::F32);
    CHECK(service.EffectiveNotes().size() == 1);

    service.SetCapabilities(FullGpu());
    CHECK(service.Effective().GpuPrecision == Units::Precision::F64);
    CHECK(service.EffectiveNotes().empty());
}

TEST_CASE("Overrides posted from another thread apply on the next drain and die with the scene", "[rendering][service]")
{
    RenderSettingsService service;
    service.SetActiveContext(RenderContext::Play);
    CHECK_FALSE(service.DrainPosted());

    std::thread poster([&]
    {
        service.PostOverride("post.bloom", "false");
        service.PostOverride("sampling.resolution_scale", "50");
        service.PostOverride("nope", "1");
        service.PostOverride("post.bloom_passes", "many");
    });
    poster.join();
    CHECK(service.Effective().Bloom);

    std::vector<std::string> errors;
    CHECK(service.DrainPosted(&errors));
    CHECK(errors.size() == 2);
    CHECK_FALSE(service.Effective().Bloom);
    CHECK(service.Effective().ResolutionScale == 50.0f);
    CHECK_FALSE(service.SceneWritePending()); // scripts do not edit the profile

    service.ClearRuntimeOverrides();
    CHECK(service.Effective().Bloom);

    service.PostOverride("post.bloom", "off");
    service.DrainPosted();
    service.LoadScene({});
    CHECK(service.Effective().Bloom);
}

TEST_CASE("The render extension is stored in and read from a scene, and Stop keeps it", "[rendering][scene]")
{
    Scene scene("RenderExt");
    CHECK(RenderExtensionText(scene).empty());

    RenderSettingsService service;
    auto edited = service.Profiles().SettingsFor(RenderContext::Edit);
    edited.ResolutionScale = 50.0f;
    service.Edit(RenderContext::Edit, edited);
    const auto text = service.SceneYaml();

    StoreRenderExtension(scene, text);
    CHECK(RenderExtensionText(scene) == YAML::Dump(YAML::Load(text)));

    // Play snapshot taken before the edit; Stop restores it but keeps the render settings
    Scene snapshot("RenderExt");
    auto live = scene.Clone();
    StoreRenderExtension(live, {});
    CHECK(RenderExtensionText(live).empty());
    StoreRenderExtension(snapshot, text);
    Scene::SyncInto(snapshot, live);
    RenderSettingsService reloaded;
    reloaded.LoadScene(RenderExtensionText(live));
    CHECK(reloaded.Effective().ResolutionScale == 50.0f);

    StoreRenderExtension(scene, {});
    CHECK(RenderExtensionText(scene).empty());
}

TEST_CASE("Edits mirrored through a running simulation reach its published snapshot", "[rendering][scene]")
{
    RegisterComponents();
    auto logger = std::make_shared<Logger>();
    SceneManager scenes(logger, std::make_shared<FileSystem>());
    auto runner = scenes.CreateSimulation(Scene("RenderMirror"), std::vector<std::shared_ptr<ISimulationModule>>{});
    runner->Start();
    runner->SetPaused(true);

    RenderSettingsService service;
    auto edited = service.Profiles().SettingsFor(RenderContext::Play);
    edited.BloomIntensity = 7.0f;
    service.Edit(RenderContext::Play, edited);
    const auto text = *service.TakeSceneWrite();
    runner->EnqueueEdit([text](Scene& scene) { StoreRenderExtension(scene, text); });

    std::string published;
    for (int i = 0; i < 200 && published.empty(); ++i)
    {
        published = RenderExtensionText(*runner->AcquireSnapshot());
        if (published.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    runner->Stop();

    RenderSettingsService reader;
    reader.LoadScene(published);
    CHECK(reader.Profiles().SettingsFor(RenderContext::Play).BloomIntensity == 7.0f);
}

TEST_CASE("The tracer size follows the resolution scale", "[rendering][pipeline]")
{
    const vk::Extent3D display{1920, 1080, 1};
    auto scaled = ScaledTraceExtent(display, 100.0f);
    CHECK(scaled.width == 1920);
    CHECK(scaled.height == 1080);
    scaled = ScaledTraceExtent(display, 50.0f);
    CHECK(scaled.width == 960);
    CHECK(scaled.height == 540);
    scaled = ScaledTraceExtent(display, 200.0f);
    CHECK(scaled.width == 3840);
    CHECK(ScaledTraceExtent(display, 1.0f).width == 480);      // clamped to 25 %
    CHECK(ScaledTraceExtent(display, 1000.0f).width == 3840);  // clamped to 200 %
    CHECK(ScaledTraceExtent({1, 1, 1}, 25.0f).width == 1);     // never empty
    CHECK(ScaledTraceExtent({7, 5, 1}, 50.0f).height == 3);
}

TEST_CASE("The governor shrinks over budget, recovers with headroom and holds in between", "[rendering][governor]")
{
    CHECK(GovernorStep(1.0f, 60.0, 33.0) < 1.0f);
    CHECK(GovernorStep(1.0f, 60.0, 33.0) >= 0.7f);
    CHECK(GovernorStep(0.5f, 10.0, 33.0) > 0.5f);
    CHECK(GovernorStep(0.7f, 30.0, 33.0) == 0.7f);
    CHECK(GovernorStep(1.0f, 10.0, 33.0) == 1.0f);
    CHECK(GovernorStep(0.25f, 500.0, 33.0) == 0.25f);

    // Converges towards the budget with a frame time that scales with the pixel count
    float scale = 1.0f;
    for (int i = 0; i < 30; ++i) scale = GovernorStep(scale, 120.0 * scale * scale, 33.0);
    const double settled = 120.0 * scale * scale;
    CHECK(settled <= 33.0 * 1.15);
    CHECK(settled >= 33.0 * 0.5);
    CHECK(std::fmod(scale * 20.0f, 1.0f) < 1e-3f);

    // Even a tiny overshoot moves, so the loop cannot stall just above the budget
    CHECK(GovernorStep(1.0f, 33.0 * 1.16, 33.0) < 1.0f);
    CHECK(GovernorStep(0.5f, 33.0 * 0.69, 33.0) > 0.5f);
}
