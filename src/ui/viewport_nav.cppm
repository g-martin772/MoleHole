export module MoleHole:UI.ViewportNav;

import std;
import glm;
import GPP;

export namespace MoleHole
{
    inline constexpr double kMinFlySpeedMs = 1.0e-3;
    inline constexpr double kMaxFlySpeedMs = GPP::Units::kSpeedOfLight * 1.0e3;
    inline constexpr double kFlySpeedStep = 1.15;

    [[nodiscard]] inline double ClampFlySpeed(const double speedMs) noexcept
    {
        if (!std::isfinite(speedMs)) return kMinFlySpeedMs;
        return std::clamp(speedMs, kMinFlySpeedMs, kMaxFlySpeedMs);
    }

    [[nodiscard]] inline double ScaleFlySpeed(const double speedMs, const double scroll) noexcept
    {
        return ClampFlySpeed(speedMs * std::pow(kFlySpeedStep, scroll));
    }

    [[nodiscard]] inline std::string FormatFlySpeed(const double speedMs)
    {
        return "Fly speed " + GPP::Units::FormatQuantity(speedMs, GPP::Units::Dimension::Velocity, 3);
    }

    [[nodiscard]] inline double PanPerPixel(const double distanceM, const double fovDegrees, const double viewportHeightPx) noexcept
    {
        if (!(viewportHeightPx > 0.0) || !(distanceM > 0.0)) return 0.0;
        return 2.0 * distanceM * std::tan(glm::radians(fovDegrees) * 0.5) / viewportHeightPx;
    }

    struct BoundingSphere
    {
        glm::dvec3 Center{0.0};
        double Radius = 0.0;
    };

    [[nodiscard]] inline std::optional<BoundingSphere> EnclosingSphere(const std::span<const BoundingSphere> spheres)
    {
        if (spheres.empty()) return std::nullopt;
        glm::dvec3 low = spheres.front().Center - glm::dvec3(spheres.front().Radius);
        glm::dvec3 high = spheres.front().Center + glm::dvec3(spheres.front().Radius);
        for (const auto& sphere : spheres)
        {
            low = glm::min(low, sphere.Center - glm::dvec3(sphere.Radius));
            high = glm::max(high, sphere.Center + glm::dvec3(sphere.Radius));
        }
        BoundingSphere result{(low + high) * 0.5, 0.0};
        for (const auto& sphere : spheres)
            result.Radius = std::max(result.Radius, glm::distance(sphere.Center, result.Center) + sphere.Radius);
        return result;
    }

    [[nodiscard]] inline double FrameDistance(const double radiusM, const double fovDegrees, const double aspect,
                                              const double margin = 1.25) noexcept
    {
        const double halfVertical = glm::radians(fovDegrees) * 0.5;
        const double halfHorizontal = std::atan(std::tan(halfVertical) * std::max(aspect, 1.0e-3));
        const double half = std::clamp(std::min(halfVertical, halfHorizontal), 1.0e-3, 1.5);
        return std::max(radiusM, 0.0) * margin / std::sin(half);
    }

    struct ViewRequest
    {
        enum class Kind { FrameAll } What = Kind::FrameAll;
        int Index = 0;
    };

    class ViewRequests
    {
    public:
        void Push(const ViewRequest request)
        {
            std::scoped_lock lock(m_Mutex);
            m_Pending.push_back(request);
        }

        [[nodiscard]] std::vector<ViewRequest> Take()
        {
            std::scoped_lock lock(m_Mutex);
            return std::exchange(m_Pending, {});
        }

    private:
        std::mutex m_Mutex;
        std::vector<ViewRequest> m_Pending;
    };
}
