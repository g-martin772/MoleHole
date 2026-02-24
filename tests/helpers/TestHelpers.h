#pragma once

#include <catch2/catch_all.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/epsilon.hpp>
#include <chrono>
#include <string>
#include <spdlog/spdlog.h>

// ── GLM Approx matchers ───────────────────────────────────────────────────────

// Use inside REQUIRE / CHECK:
//   REQUIRE(ApproxVec3(a, b));
//   REQUIRE(ApproxVec3(a, b, 1e-4f));

inline bool ApproxVec2(const glm::vec2& a, const glm::vec2& b, float eps = 1e-5f)
{
    return glm::all(glm::epsilonEqual(a, b, eps));
}

inline bool ApproxVec3(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f)
{
    return glm::all(glm::epsilonEqual(a, b, eps));
}

inline bool ApproxVec4(const glm::vec4& a, const glm::vec4& b, float eps = 1e-5f)
{
    return glm::all(glm::epsilonEqual(a, b, eps));
}

inline bool ApproxMat4(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f)
{
    for (int col = 0; col < 4; ++col)
        if (!glm::all(glm::epsilonEqual(a[col], b[col], eps)))
            return false;
    return true;
}

// ── Scoped timer ──────────────────────────────────────────────────────────────
// Prints elapsed time on destruction. Useful for quick manual timing in tests.
//
// Usage:
//   { ScopedTimer t("MyOperation"); DoThing(); }

class ScopedTimer
{
public:
    explicit ScopedTimer(std::string label)
        : m_Label(std::move(label))
        , m_Start(std::chrono::high_resolution_clock::now())
    {}

    ~ScopedTimer()
    {
        auto end = std::chrono::high_resolution_clock::now();
        auto us  = std::chrono::duration_cast<std::chrono::microseconds>(end - m_Start).count();
        spdlog::info("[Timer] {}: {:.3f} ms", m_Label, us / 1000.0);
    }

    double ElapsedMs() const
    {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now - m_Start).count() / 1000.0;
    }

private:
    std::string m_Label;
    std::chrono::high_resolution_clock::time_point m_Start;
};

// ── Catch2 custom matchers for glm types ─────────────────────────────────────

class Vec3Matcher : public Catch::Matchers::MatcherBase<glm::vec3>
{
public:
    explicit Vec3Matcher(const glm::vec3& expected, float eps = 1e-5f)
        : m_Expected(expected), m_Eps(eps) {}

    bool match(const glm::vec3& actual) const override
    {
        return ApproxVec3(actual, m_Expected, m_Eps);
    }

    std::string describe() const override
    {
        return "≈ vec3(" + std::to_string(m_Expected.x) + ", "
                         + std::to_string(m_Expected.y) + ", "
                         + std::to_string(m_Expected.z) + ") ±" + std::to_string(m_Eps);
    }

private:
    glm::vec3 m_Expected;
    float     m_Eps;
};

inline Vec3Matcher IsApprox(const glm::vec3& v, float eps = 1e-5f) { return Vec3Matcher(v, eps); }

