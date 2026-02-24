#include "TestHelpers.h"
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <numeric>

// Placeholder benchmarks — replace / extend with real cases.
// Run directly for full output:
//   ./build/tests/MathBenchmarks --benchmark-samples 200

TEST_CASE("GLM mat4 multiply benchmark", "[math][benchmark]")
{
    const glm::mat4 a = glm::rotate(glm::mat4(1.f), 0.5f, glm::vec3(0, 1, 0));
    const glm::mat4 b = glm::translate(glm::mat4(1.f), glm::vec3(1.f, 2.f, 3.f));

    BENCHMARK("mat4 * mat4")
    {
        return a * b;
    };

    BENCHMARK("mat4 * vec4")
    {
        return a * glm::vec4(1.f, 2.f, 3.f, 1.f);
    };
}

TEST_CASE("std::vector push_back benchmark", "[memory][benchmark]")
{
    BENCHMARK_ADVANCED("push_back 1k floats")(Catch::Benchmark::Chronometer meter)
    {
        std::vector<float> v;
        v.reserve(1000);
        meter.measure([&]
        {
            v.clear();
            for (int i = 0; i < 1000; ++i)
                v.push_back(static_cast<float>(i));
            return v.size();
        });
    };
}

