#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace GPP;
using namespace MoleHole;
using Catch::Approx;

namespace
{
    constexpr std::uint64_t kPrograde = 7720000000000000002ull;
    constexpr std::uint64_t kRetrograde = 7720000000000000003ull;
    constexpr std::uint64_t kEccentric = 7720000000000000004ull;
    constexpr std::uint64_t kRaindrop = 7720000000000000005ull;
    constexpr std::uint64_t kCamera = 7720000000000000006ull;

    Scene LoadM1Scene()
    {
        RegisterComponents();
        std::ifstream file(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "templates" / "m1-orbits.yaml");
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        Scene scene("M1");
        scene.DeserializeFromYaml(text);
        return scene;
    }

    const GeodesicBodyComponent& Body(const Scene& scene, const std::uint64_t guid)
    {
        return scene.Registry().get<GeodesicBodyComponent>(scene.FindByGuid(guid));
    }
}

TEST_CASE("The M1 acceptance scene shows frame dragging, precession and capture", "[simulation][geodesic][templates][m1]")
{
    Scene scene = LoadM1Scene();
    CHECK(scene.Metadata().TimeScale == 100.0);
    REQUIRE(LoadSceneGraphs(scene).Items.size() == 1);
    // The scene's render profile turns the orbit overlays on
    const std::string render = RenderExtensionText(scene);
    CHECK(render.contains("overlay.characteristic_radii: true"));
    CHECK(render.contains("overlay.predicted_orbits: true"));

    GeodesicSimulationModule module;
    module.OnInit(scene);
    for (const auto guid : {kPrograde, kRetrograde, kEccentric, kRaindrop, kCamera})
    {
        INFO(guid);
        REQUIRE(Body(scene, guid).Status == GeodesicBodyStatus::FreeFall);
    }
    CHECK(Body(scene, kPrograde).State.R == Approx(10.0).epsilon(1e-6));
    CHECK(Body(scene, kRetrograde).State.R == Approx(10.0).epsilon(1e-6));
    const double proStart = Body(scene, kPrograde).State.Phi;
    const double retroStart = Body(scene, kRetrograde).State.Phi; // starts on the opposite side, φ = π

    // About 2000 M of coordinate time, tracking the eccentric orbit's periapsis passages
    std::vector<double> periapsisAzimuths;
    double previousR = Body(scene, kEccentric).State.R;
    bool falling = false;
    double rMin = previousR;
    double rMax = previousR;
    for (int tick = 0; tick < 1000; ++tick)
    {
        module.OnTick(scene, 0.1f);
        const auto& eccentric = Body(scene, kEccentric).State;
        if (falling && eccentric.R > previousR) periapsisAzimuths.push_back(eccentric.Phi);
        falling = eccentric.R < previousR;
        previousR = eccentric.R;
        rMin = std::min(rMin, eccentric.R);
        rMax = std::max(rMax, eccentric.R);
    }

    // Frame dragging: at the same r the retrograde orbit is faster in coordinate time, Ω ∝ 1/(r^{3/2} ∓ a)
    const double a = 0.9;
    const double ratio = std::abs(Body(scene, kRetrograde).State.Phi - retroStart) / (Body(scene, kPrograde).State.Phi - proStart);
    CHECK(ratio == Approx((std::pow(10.0, 1.5) + a) / (std::pow(10.0, 1.5) - a)).epsilon(1e-6));
    CHECK(Body(scene, kRetrograde).State.AngularMomentum < 0.0);

    // Periapsis precession: each radial period sweeps clearly more than one full turn, and the orbit stays bound
    REQUIRE(periapsisAzimuths.size() >= 3);
    for (std::size_t i = 1; i < periapsisAzimuths.size(); ++i)
    {
        CHECK(periapsisAzimuths[i] - periapsisAzimuths[i - 1] > 2.0 * std::numbers::pi + 1.0);
    }
    CHECK(rMin > 6.0);
    CHECK(rMax < 14.1);
    CHECK(Body(scene, kEccentric).Status == GeodesicBodyStatus::FreeFall);

    // The raindrop has fallen in; the camera is still in free fall on its own orbit
    CHECK(Body(scene, kRaindrop).Status == GeodesicBodyStatus::Captured);
    CHECK(Body(scene, kCamera).Status == GeodesicBodyStatus::FreeFall);
}
