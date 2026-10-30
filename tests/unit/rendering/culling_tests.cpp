#include <catch2/catch_test_macros.hpp>

import GPP;
import MoleHole;
import glm;
import std;

using namespace MoleHole;

namespace
{
    Frustum LookDownMinusZ(const float far = 100.0f)
    {
        glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, far);
        projection[1][1] *= -1.0f;
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        return MakeFrustum(projection * view);
    }
}

TEST_CASE("Spheres in front of the camera are kept", "[rendering][culling]")
{
    const auto frustum = LookDownMinusZ();
    CHECK(frustum.Valid);
    CHECK(SphereInFrustum(frustum, {0.0f, 0.0f, -10.0f, 1.0f}));
    CHECK(SphereInFrustum(frustum, {0.0f, 0.0f, -10.0f, 0.0f}));
}

TEST_CASE("Spheres outside any plane are culled", "[rendering][culling]")
{
    const auto frustum = LookDownMinusZ();
    CHECK_FALSE(SphereInFrustum(frustum, {0.0f, 0.0f, 10.0f, 1.0f}));
    CHECK_FALSE(SphereInFrustum(frustum, {100.0f, 0.0f, -10.0f, 1.0f}));
    CHECK_FALSE(SphereInFrustum(frustum, {-100.0f, 0.0f, -10.0f, 1.0f}));
    CHECK_FALSE(SphereInFrustum(frustum, {0.0f, 100.0f, -10.0f, 1.0f}));
    CHECK_FALSE(SphereInFrustum(frustum, {0.0f, -100.0f, -10.0f, 1.0f}));
    CHECK_FALSE(SphereInFrustum(frustum, {0.0f, 0.0f, -200.0f, 1.0f}));
}

TEST_CASE("Spheres touching the volume or containing the camera are kept", "[rendering][culling]")
{
    const auto frustum = LookDownMinusZ();
    CHECK(SphereInFrustum(frustum, {0.0f, 0.0f, 0.5f, 1.0f}));
    CHECK(SphereInFrustum(frustum, {0.0f, 0.0f, 0.0f, 5.0f}));
    CHECK(SphereInFrustum(frustum, {0.0f, 0.0f, 0.0f, 1.0e9f}));
    const float edge = 10.0f * std::tan(glm::radians(30.0f));
    CHECK(SphereInFrustum(frustum, {0.0f, edge + 0.5f, -10.0f, 1.0f}));
    CHECK_FALSE(SphereInFrustum(frustum, {0.0f, edge + 2.0f, -10.0f, 1.0f}));
    CHECK(SphereInFrustum(frustum, {0.0f, 0.0f, -105.0f, 10.0f}));
}

TEST_CASE("Behind the camera is culled even when the sphere is wide", "[rendering][culling]")
{
    const auto frustum = LookDownMinusZ();
    CHECK_FALSE(SphereInFrustum(frustum, {0.0f, 0.0f, 50.0f, 40.0f}));
    CHECK(SphereInFrustum(frustum, {0.0f, 0.0f, 50.0f, 60.0f}));
}

TEST_CASE("A frustum with an infinite far plane culls on the other five", "[rendering][culling]")
{
    glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    projection[2][2] = -1.0f;
    projection[3][2] = -0.1f;
    projection[1][1] *= -1.0f;
    const auto frustum = MakeFrustum(projection * glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(frustum.Valid);
    CHECK(SphereInFrustum(frustum, {0.0f, 0.0f, -1.0e7f, 1.0f}));
    CHECK_FALSE(SphereInFrustum(frustum, {0.0f, 0.0f, 10.0f, 1.0f}));
}

TEST_CASE("Degenerate input keeps everything", "[rendering][culling]")
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    CHECK_FALSE(MakeFrustum(glm::mat4(nan)).Valid);
    CHECK_FALSE(MakeFrustum(glm::mat4(inf)).Valid);
    CHECK(SphereInFrustum(MakeFrustum(glm::mat4(nan)), {1000.0f, 0.0f, 0.0f, 1.0f}));
    CHECK(SphereInFrustum(Frustum{}, {1000.0f, 0.0f, 0.0f, 1.0f}));

    const auto frustum = LookDownMinusZ();
    CHECK(SphereInFrustum(frustum, {1000.0f, 0.0f, 0.0f, -1.0f}));
    CHECK(SphereInFrustum(frustum, {1000.0f, 0.0f, 0.0f, nan}));
    CHECK(SphereInFrustum(frustum, {nan, 0.0f, 0.0f, 1.0f}));
    CHECK(SphereInFrustum(frustum, {1000.0f, inf, 0.0f, 1.0f}));
    CHECK(SphereInFrustum(frustum, {1000.0f, 0.0f, 0.0f, inf}));
}

TEST_CASE("Mesh draws carry the bounds of their mesh", "[rendering][culling]")
{
    const MeshDraw draw;
    CHECK(draw.Bounds.w < 0.0f);
    CHECK(SphereInFrustum(LookDownMinusZ(), draw.Bounds));
}

TEST_CASE("Culling agrees with the raster clip volume on a rotated camera", "[rendering][culling]")
{
    glm::mat4 projection = glm::perspective(glm::radians(50.0f), 1.0f, 0.5f, 1000.0f);
    projection[1][1] *= -1.0f;
    const glm::vec3 eye{3.0f, 4.0f, 5.0f};
    const glm::vec3 forward = glm::normalize(glm::vec3(-0.4f, 0.1f, -0.9f));
    const glm::mat4 viewProjection = projection * glm::lookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    const auto frustum = MakeFrustum(viewProjection);
    int mismatches = 0;
    for (int x = -20; x <= 20; ++x)
    {
        for (int y = -20; y <= 20; ++y)
        {
            for (int z = -20; z <= 20; ++z)
            {
                const glm::vec3 point = eye + glm::vec3(x, y, z) * 3.0f;
                const glm::vec4 clip = viewProjection * glm::vec4(point, 1.0f);
                const bool inside = clip.w > 0.0f && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= 0.0f
                                    && clip.z <= clip.w;
                if (inside && !SphereInFrustum(frustum, glm::vec4(point, 0.0f))) ++mismatches;
            }
        }
    }
    CHECK(mismatches == 0);
}
