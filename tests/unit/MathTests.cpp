#include "TestHelpers.h"
#include <glm/gtc/matrix_transform.hpp>

// Placeholder unit tests — replace / extend with real cases.

TEST_CASE("GLM vec3 arithmetic", "[math][unit]")
{
    const glm::vec3 a(1.f, 2.f, 3.f);
    const glm::vec3 b(4.f, 5.f, 6.f);

    SECTION("addition")
    {
        REQUIRE(ApproxVec3(a + b, glm::vec3(5.f, 7.f, 9.f)));
    }

    SECTION("dot product")
    {
        REQUIRE(glm::dot(a, b) == Catch::Approx(32.f));
    }

    SECTION("cross product")
    {
        const glm::vec3 expected(-3.f, 6.f, -3.f);
        REQUIRE_THAT(glm::cross(a, b), IsApprox(expected));
    }
}

TEST_CASE("GLM mat4 identity round-trip", "[math][unit]")
{
    const glm::mat4 identity(1.f);
    const glm::mat4 translated = glm::translate(identity, glm::vec3(1.f, 2.f, 3.f));
    const glm::mat4 back       = glm::translate(translated, glm::vec3(-1.f, -2.f, -3.f));

    REQUIRE(ApproxMat4(back, identity));
}

