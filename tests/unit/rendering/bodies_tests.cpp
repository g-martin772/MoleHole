#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    entt::entity AddSphere(Scene& scene, const glm::vec3& color, const glm::vec3& spinAxis)
    {
        const auto entity = scene.CreateEntity("Sphere", "Sphere");
        scene.Registry().emplace<TransformComponent>(entity, TransformComponent{});
        scene.Registry().emplace<SphereComponent>(entity, SphereComponent{.Color = color, .SpinAxis = spinAxis});
        return entity;
    }

    void CheckNear(const glm::vec3& a, const glm::vec3& b)
    {
        CHECK(a.x == Approx(b.x).margin(1e-5));
        CHECK(a.y == Approx(b.y).margin(1e-5));
        CHECK(a.z == Approx(b.z).margin(1e-5));
    }

    const BodyTextureSlotFn kNoTextures = [](const std::string&, TextureRole) { return -1; };
}

TEST_CASE("Equirectangular coordinates follow the map convention", "[rendering][bodies]")
{
    // Longitude 0 on the equator is the centre of the map, +90 deg east is three quarters across
    const auto prime = EquirectUv({1.0f, 0.0f, 0.0f});
    CHECK(prime.x == Approx(0.5f));
    CHECK(prime.y == Approx(0.5f));
    CHECK(EquirectUv({0.0f, 0.0f, -1.0f}).x == Approx(0.75f));
    CHECK(EquirectUv({0.0f, 0.0f, 1.0f}).x == Approx(0.25f));
    CHECK(EquirectUv({0.0f, 1.0f, 0.0f}).y == Approx(0.0f));  // north pole: top row
    CHECK(EquirectUv({0.0f, -1.0f, 0.0f}).y == Approx(1.0f)); // south pole: bottom row
}

TEST_CASE("Body orientation puts the pole on the spin axis and turns eastward", "[rendering][bodies]")
{
    const glm::vec3 axis = glm::normalize(glm::vec3(1.0f, 2.0f, -0.5f));
    CheckNear(QuatRotate(BodyOrientation(axis, 1.3), {0.0f, 1.0f, 0.0f}), axis);
    CheckNear(QuatRotate(BodyOrientation({0.0f, -1.0f, 0.0f}, 0.0), {0.0f, 1.0f, 0.0f}), {0.0f, -1.0f, 0.0f});

    // A quarter turn carries longitude 0 to where longitude +90 deg was
    const auto turned = QuatRotate(BodyOrientation({0.0f, 1.0f, 0.0f}, std::numbers::pi / 2.0), {1.0f, 0.0f, 0.0f});
    CheckNear(turned, {0.0f, 0.0f, -1.0f});
    CHECK(EquirectUv(turned).x == Approx(0.75f));

    // Body -> world -> body is the identity
    const auto q = BodyOrientation(axis, 0.7);
    CheckNear(QuatRotate(QuatConjugate(q), QuatRotate(q, {0.3f, -0.2f, 0.9f})), {0.3f, -0.2f, 0.9f});
}

TEST_CASE("Body entries line up with the uploaded spheres", "[rendering][bodies]")
{
    RegisterComponents();
    Scene scene;
    const std::array<glm::vec3, 3> axes{glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)};
    for (std::size_t i = 0; i < axes.size(); ++i)
    {
        AddSphere(scene, glm::vec3(static_cast<float>(i) / 10.0f), axes[i]);
    }
    RaytraceParamsGpu params{};
    FillSceneData(params, scene, RenderFrame{});
    const auto bodies = FillBodyData(scene, kNoTextures);
    REQUIRE(params.NumSpheres == 3);
    for (int i = 0; i < params.NumSpheres; ++i)
    {
        // The colour identifies the sphere; its body entry must carry the same sphere's pole
        const auto index = static_cast<std::size_t>(std::lround(params.Spheres[static_cast<std::size_t>(i)].Color.r * 10.0f));
        CheckNear(QuatRotate(bodies[static_cast<std::size_t>(i)].Orientation, {0.0f, 1.0f, 0.0f}), axes[index]);
    }
}

TEST_CASE("Body entries line up with the uploaded spheres when a Star is among them", "[rendering][bodies]")
{
    RegisterComponents();
    Scene scene;
    const std::array<glm::vec3, 4> axes{glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                                        glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))};
    std::array<entt::entity, 4> entities{};
    for (std::size_t i = 0; i < axes.size(); ++i)
    {
        entities[i] = AddSphere(scene, glm::vec3(static_cast<float>(i) / 10.0f), axes[i]);
    }
    // The second sphere is a Star as well (a Sun with a Sphere), the fourth has a Surface; FillSceneData uploads Stars first
    scene.Registry().emplace<StarComponent>(entities[1]);
    scene.Registry().emplace<SurfaceComponent>(entities[3], SurfaceComponent{});

    RaytraceParamsGpu params{};
    FillSceneData(params, scene, RenderFrame{});
    const auto bodies = FillBodyData(scene, kNoTextures);
    const auto uploaded = UploadedSpheres(scene);
    REQUIRE(params.NumSpheres == 4);
    REQUIRE(uploaded.size() == 4);
    CHECK(uploaded.front() == entities[1]); // the Star leads
    for (int i = 0; i < params.NumSpheres; ++i)
    {
        const auto at = static_cast<std::size_t>(i);
        if (i == 0) continue; // a Star's colour is its radiance, not the sphere's marker colour
        const auto index = static_cast<std::size_t>(std::lround(params.Spheres[at].Color.r * 10.0f));
        INFO("sphere " << i << " is entity " << index);
        CheckNear(QuatRotate(bodies[at].Orientation, {0.0f, 1.0f, 0.0f}), axes[index]);
        CHECK((bodies[at].Flags.x == 1) == (index == 3)); // only the fourth has a Surface
    }
}

TEST_CASE("Surface maps get slots by role and a bare sphere uses its texture as albedo", "[rendering][bodies]")
{
    RegisterComponents();
    Scene scene;
    const auto bare = AddSphere(scene, glm::vec3(1.0f), {0.0f, 1.0f, 0.0f});
    scene.Registry().get<SphereComponent>(bare).TexturePath = "moon.png";
    const auto planet = AddSphere(scene, glm::vec3(1.0f), {0.0f, 1.0f, 0.0f});
    scene.Registry().emplace<SurfaceComponent>(planet, SurfaceComponent{
        .Albedo = "earth/albedo.jpg", .Normal = "earth/normal.png", .Emissive = "earth/night.jpg",
        .RoughnessFactor = 0.5f, .RoughnessIsSpecular = true});

    std::vector<std::pair<std::string, TextureRole>> requested;
    const auto bodies = FillBodyData(scene, [&](const std::string& path, const TextureRole role)
    {
        if (path.empty()) return -1;
        requested.emplace_back(path, role);
        return static_cast<int>(requested.size()) - 1;
    });

    // The view's order is the registry's, not the order of creation: find each body by what it asked for
    REQUIRE(requested.size() == 4);
    const bool planetFirst = requested[0].first != "moon.png";
    const auto& bareGpu = bodies[planetFirst ? 1 : 0];
    const auto& planetGpu = bodies[planetFirst ? 0 : 1];
    const int moon = planetFirst ? 3 : 0;
    const int first = planetFirst ? 0 : 1;
    CHECK(requested[static_cast<std::size_t>(moon)] == std::pair<std::string, TextureRole>{"moon.png", TextureRole::Color});
    CHECK(requested[static_cast<std::size_t>(first)].second == TextureRole::Color);     // albedo
    CHECK(requested[static_cast<std::size_t>(first + 1)].second == TextureRole::Data);  // normal
    CHECK(requested[static_cast<std::size_t>(first + 2)].second == TextureRole::Color); // emissive
    CHECK(bareGpu.Maps == glm::ivec4(moon, -1, -1, -1));
    CHECK(planetGpu.Maps == glm::ivec4(first, first + 1, -1, first + 2));
    CHECK(planetGpu.Material.x == Approx(0.5f));
    CHECK(planetGpu.Material.z == Approx(1.0f));
}

TEST_CASE("Rotation advances on the body's own clock", "[simulation][bodies]")
{
    CHECK(AdvanceRotation(0.0, 6.0, 24.0) == Approx(std::numbers::pi / 2.0));
    CHECK(AdvanceRotation(0.0, 30.0, 24.0) == Approx(std::numbers::pi / 2.0)); // wraps
    CHECK(AdvanceRotation(1.0, 5.0, 0.0) == 1.0);                              // no period: fixed

    RegisterComponents();
    Scene scene;
    scene.SetTimeScale(1.0);
    const auto plain = AddSphere(scene, glm::vec3(1.0f), {0.0f, 1.0f, 0.0f});
    scene.Registry().emplace<SurfaceComponent>(plain, SurfaceComponent{.RotationPeriod = 40.0});
    const auto slow = AddSphere(scene, glm::vec3(1.0f), {0.0f, 1.0f, 0.0f});
    scene.Registry().emplace<SurfaceComponent>(slow, SurfaceComponent{.RotationPeriod = 40.0});
    scene.Registry().emplace<GeodesicBodyComponent>(
        slow, GeodesicBodyComponent{.Status = GeodesicBodyStatus::FreeFall, .ClockRate = 0.5});

    BodyRotationModule module;
    module.OnTick(scene, 10.0f);
    CHECK(scene.Registry().get<SurfaceComponent>(plain).RotationAngle == Approx(std::numbers::pi / 2.0));
    CHECK(scene.Registry().get<SurfaceComponent>(slow).RotationAngle == Approx(std::numbers::pi / 4.0));
}

TEST_CASE("Surface round-trips through YAML without its runtime angle", "[simulation][bodies]")
{
    RegisterComponents();
    Scene scene;
    const auto entity = AddSphere(scene, glm::vec3(1.0f), {0.0f, 1.0f, 0.0f});
    scene.Registry().emplace<SurfaceComponent>(entity, SurfaceComponent{
        .Albedo = "earth/albedo.jpg", .RotationPeriod = 86164.1, .PrimeMeridian = 12.5f, .RotationAngle = 2.0});

    Scene loaded;
    loaded.DeserializeFromYaml(scene.SerializeToYaml());
    const auto& surface = loaded.Registry().get<SurfaceComponent>(loaded.FindByGuid(scene.GuidOf(entity)));
    CHECK(surface.Albedo == "earth/albedo.jpg");
    CHECK(surface.RotationPeriod == Approx(86164.1));
    CHECK(surface.PrimeMeridian == Approx(12.5f));
    CHECK(surface.RotationAngle == 0.0);
}

TEST_CASE("Probe directions cover the sphere in the map convention", "[rendering][bodies][lighting]")
{
    CheckNear(ProbeDirection(0.5f, 0.5f), {1.0f, 0.0f, 0.0f});
    CheckNear(ProbeDirection(0.75f, 0.5f), {0.0f, 0.0f, -1.0f});
    CheckNear(ProbeDirection(0.5f, 0.0f), {0.0f, 1.0f, 0.0f});
    // Same convention as the surface maps: a probe direction maps back to its own (u, v)
    const auto uv = EquirectUv(ProbeDirection(0.3f, 0.7f));
    CHECK(uv.x == Approx(0.3f).margin(1e-5));
    CHECK(uv.y == Approx(0.7f).margin(1e-5));
}

TEST_CASE("SH irradiance of a uniform sky is pi times its radiance", "[rendering][bodies][lighting]")
{
    const auto sh = ProjectRadianceSh([](const glm::vec3&) { return glm::vec3(1.0f, 0.5f, 0.25f); });
    for (const auto& n : {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 3.0f))})
    {
        const auto e = EvaluateIrradianceSh(sh, n);
        CHECK(e.r == Approx(std::numbers::pi).epsilon(0.01));
        CHECK(e.g == Approx(0.5 * std::numbers::pi).epsilon(0.01));
        CHECK(e.b == Approx(0.25 * std::numbers::pi).epsilon(0.01));
    }
}

TEST_CASE("SH irradiance of a sky lit from above follows the cosine law", "[rendering][bodies][lighting]")
{
    // Radiance 1 over the upper hemisphere: E(n) = pi (1 + n.y) / 2 exactly; 9 coefficients reproduce it to a few %
    const auto sh = ProjectRadianceSh([](const glm::vec3& d) { return glm::vec3(d.y > 0.0f ? 1.0f : 0.0f); });
    CHECK(EvaluateIrradianceSh(sh, {0.0f, 1.0f, 0.0f}).r == Approx(std::numbers::pi).epsilon(0.03));
    CHECK(EvaluateIrradianceSh(sh, {1.0f, 0.0f, 0.0f}).r == Approx(0.5 * std::numbers::pi).epsilon(0.03));
    CHECK(EvaluateIrradianceSh(sh, {0.0f, -1.0f, 0.0f}).r == Approx(0.0).margin(0.1));
}

TEST_CASE("Lights are uploaded in render units with the lighting mode", "[rendering][bodies][lighting]")
{
    RegisterComponents();
    Scene scene;
    const auto sun = scene.CreateEntity("Sun");
    scene.Registry().emplace<TransformComponent>(sun, TransformComponent{});
    scene.Registry().emplace<LightComponent>(sun, LightComponent{.Color = {1.0f, 0.5f, 0.25f}, .Intensity = 2.0f,
                                                                .Direction = {0.0f, 0.0f, 3.0f}});
    const auto lamp = scene.CreateEntity("Lamp");
    scene.Registry().emplace<TransformComponent>(lamp, TransformComponent{.Position = {2000.0, 0.0, 0.0}});
    scene.Registry().emplace<LightComponent>(lamp, LightComponent{.Kind = BodyLightKind::Point, .ReferenceDistance = 500.0});

    RenderFrame frame;
    frame.LengthUnitM = 100.0;
    const auto block = FillLightData(scene, frame, BodyLightingMode::Probe);
    CHECK(block.Header.x == 2);
    CHECK(block.Header.y == static_cast<int>(BodyLightingMode::Probe));
    bool sawSun = false;
    bool sawLamp = false;
    for (int i = 0; i < block.Header.x; ++i)
    {
        const auto& light = block.Lights[static_cast<std::size_t>(i)];
        if (light.Position.w == 0.0f)
        {
            sawSun = true;
            CheckNear(glm::vec3(light.Position), {0.0f, 0.0f, 1.0f});
            CheckNear(glm::vec3(light.Color), {2.0f, 1.0f, 0.5f});
        }
        else
        {
            sawLamp = true;
            CheckNear(glm::vec3(light.Position), {20.0f, 0.0f, 0.0f});
            CHECK(light.Color.w == Approx(5.0f));
        }
    }
    CHECK(sawSun);
    CHECK(sawLamp);
}

TEST_CASE("Atmospheres and clouds keep a Surface lit and arrive in render units", "[rendering][bodies]")
{
    RegisterComponents();
    Scene scene;
    const auto planet = AddSphere(scene, glm::vec3(1.0f), {0.0f, 1.0f, 0.0f});
    scene.Registry().get<SphereComponent>(planet).Radius = 6.0e6;
    scene.Registry().emplace<SurfaceComponent>(planet, SurfaceComponent{});
    scene.Registry().emplace<AtmosphereComponent>(planet, AtmosphereComponent{.Height = 1.0e5});
    scene.Registry().emplace<CloudLayerComponent>(planet, CloudLayerComponent{.Altitude = 4000.0, .Opacity = 0.5f});
    const auto marker = AddSphere(scene, glm::vec3(1.0f), {0.0f, 1.0f, 0.0f});
    scene.Registry().emplace<AtmosphereComponent>(marker, AtmosphereComponent{});

    const auto bodies = FillBodyData(scene, kNoTextures, 1000.0);
    for (int k = 0; k < 2; ++k)
    {
        const auto& gpu = bodies[static_cast<std::size_t>(k)];
        if (gpu.Cloud.x > 0.0f)
        {
            CHECK(gpu.Flags.x == 1);
            CHECK(gpu.AtmosphereShape.x == Approx(6100.0f));       // (R + height) / 1000 m
            CHECK(gpu.AtmosphereShape.y == Approx(8.0f));          // Rayleigh scale height
            CHECK(gpu.Rayleigh.z == Approx(33.1e-6f * 1000.0f));   // per render unit
            CHECK(gpu.Cloud.x == Approx(6004.0f));
            CHECK(gpu.Cloud.y == Approx(0.5f));
            CHECK(gpu.Cloud.z == Approx(-1.0f));                   // no coverage map: uniform
        }
        else
        {
            CHECK(gpu.Flags.x == 0); // a marker with an atmosphere stays a marker
        }
    }
}
