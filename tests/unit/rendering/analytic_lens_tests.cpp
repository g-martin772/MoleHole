#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import std;

using Catch::Approx;

namespace
{
    double LaunchAngle(const double psi, const double distanceM)
    {
        const double k = 1.0 - 2.0 / distanceM;
        const double radial = std::cos(psi);
        const double tangential = std::sin(psi) / std::sqrt(k);
        return std::atan2(tangential, radial);
    }

    double OrbitAngle(const double psi, const double distanceM)
    {
        const double k = 1.0 - 2.0 / distanceM;
        const double u = 1.0 / distanceM;
        const double b = distanceM * std::sin(psi) / std::sqrt(k);
        const double duDphi = std::sqrt(std::max(0.0, 1.0 / (b * b) - u * u * k));
        return std::atan2(u, std::copysign(duDphi, std::cos(psi)));
    }

    std::string ReadShader()
    {
        std::ifstream in(std::filesystem::path(MOLEHOLE_SOURCE_DIR) / "shaders/ray_tracing.glsl");
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
}

TEST_CASE("The analytic lens launch direction follows the Binet orbit at every angle", "[rendering][lens]")
{
    for (const double distance : {9.0, 14.0, 45.0, 400.0})
    {
        for (int degrees = 1; degrees < 180; ++degrees)
        {
            const double psi = degrees * std::numbers::pi / 180.0;
            INFO("D " << distance << " psi " << degrees);
            CHECK(LaunchAngle(psi, distance) == Approx(OrbitAngle(psi, distance)).margin(1e-9));
        }
    }
}

TEST_CASE("The analytic lens launch direction never collapses a band of view angles", "[rendering][lens]")
{
    for (const double distance : {9.0, 14.0, 45.0})
    {
        double previous = LaunchAngle(0.001, distance);
        for (int degrees = 1; degrees < 180; ++degrees)
        {
            const double angle = LaunchAngle(degrees * std::numbers::pi / 180.0, distance);
            INFO("D " << distance << " psi " << degrees);
            CHECK(angle > previous + 1e-4);
            previous = angle;
        }
    }
}

TEST_CASE("The analytic lens shader converts the pixel direction without clamping", "[rendering][lens]")
{
    const auto shader = ReadShader();
    const auto start = shader.find("vec3 staticObserverDirection");
    REQUIRE(start != std::string::npos);
    const auto body = shader.substr(start, shader.find("vec3 hybridRayTrace", start) - start);
    CHECK(body.find("normalize(cosPsi * inward + perpendicular / sqrt(k))") != std::string::npos);
    CHECK(body.find("min(") == std::string::npos);
}
