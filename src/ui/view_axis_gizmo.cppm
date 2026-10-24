export module MoleHole:UI.ViewAxisGizmo;

import std;
import glm;

export namespace MoleHole
{
    enum class ViewAxis { PosX, NegX, PosY, NegY, PosZ, NegZ };

    inline constexpr std::array<ViewAxis, 6> kViewAxes{ViewAxis::PosX, ViewAxis::NegX, ViewAxis::PosY,
                                                       ViewAxis::NegY, ViewAxis::PosZ, ViewAxis::NegZ};
    inline constexpr float kSnapPitchDegrees = 89.9f;

    [[nodiscard]] constexpr glm::vec3 AxisDirection(const ViewAxis axis) noexcept
    {
        switch (axis)
        {
        case ViewAxis::PosX: return {1.0f, 0.0f, 0.0f};
        case ViewAxis::NegX: return {-1.0f, 0.0f, 0.0f};
        case ViewAxis::PosY: return {0.0f, 1.0f, 0.0f};
        case ViewAxis::NegY: return {0.0f, -1.0f, 0.0f};
        case ViewAxis::PosZ: return {0.0f, 0.0f, 1.0f};
        case ViewAxis::NegZ: return {0.0f, 0.0f, -1.0f};
        }
        return {0.0f, 0.0f, 1.0f};
    }

    [[nodiscard]] constexpr std::string_view AxisLabel(const ViewAxis axis) noexcept
    {
        switch (axis)
        {
        case ViewAxis::PosX: return "X";
        case ViewAxis::NegX: return "-X";
        case ViewAxis::PosY: return "Y";
        case ViewAxis::NegY: return "-Y";
        case ViewAxis::PosZ: return "Z";
        case ViewAxis::NegZ: return "-Z";
        }
        return {};
    }

    [[nodiscard]] constexpr std::string_view AxisViewName(const ViewAxis axis) noexcept
    {
        switch (axis)
        {
        case ViewAxis::PosX: return "+X (Right)";
        case ViewAxis::NegX: return "-X (Left)";
        case ViewAxis::PosY: return "+Y (Top)";
        case ViewAxis::NegY: return "-Y (Bottom)";
        case ViewAxis::PosZ: return "+Z (Front)";
        case ViewAxis::NegZ: return "-Z (Back)";
        }
        return {};
    }

    struct ViewAngles
    {
        float Yaw = 0.0f;
        float Pitch = 0.0f;
    };

    [[nodiscard]] constexpr ViewAngles SnapAngles(const ViewAxis axis, const float currentYaw) noexcept
    {
        switch (axis)
        {
        case ViewAxis::PosX: return {180.0f, 0.0f};
        case ViewAxis::NegX: return {0.0f, 0.0f};
        case ViewAxis::PosY: return {currentYaw, -kSnapPitchDegrees};
        case ViewAxis::NegY: return {currentYaw, kSnapPitchDegrees};
        case ViewAxis::PosZ: return {-90.0f, 0.0f};
        case ViewAxis::NegZ: return {90.0f, 0.0f};
        }
        return {currentYaw, 0.0f};
    }

    [[nodiscard]] inline glm::vec3 FrontFromAngles(const ViewAngles angles) noexcept
    {
        const float yaw = glm::radians(angles.Yaw);
        const float pitch = glm::radians(angles.Pitch);
        return {std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)};
    }

    struct SnappedView
    {
        glm::dvec3 PositionM{0.0};
        ViewAngles Angles;
    };

    [[nodiscard]] inline SnappedView SnapView(const glm::dvec3& cameraM, const glm::dvec3& pivotM, const ViewAxis axis,
                                              const float currentYaw)
    {
        const auto angles = SnapAngles(axis, currentYaw);
        const double distance = glm::distance(cameraM, pivotM);
        if (!(distance > 0.0)) return {cameraM, angles};
        return {pivotM - glm::dvec3(FrontFromAngles(angles)) * distance, angles};
    }

    struct AxisGizmoItem
    {
        ViewAxis Axis = ViewAxis::PosX;
        glm::vec2 Offset{0.0f};
        float Depth = 0.0f;
    };

    [[nodiscard]] inline std::array<AxisGizmoItem, 6> LayoutAxisGizmo(const glm::vec3& right, const glm::vec3& up,
                                                                      const glm::vec3& front)
    {
        std::array<AxisGizmoItem, 6> items;
        for (std::size_t i = 0; i < kViewAxes.size(); ++i)
        {
            const glm::vec3 direction = AxisDirection(kViewAxes[i]);
            items[i] = {kViewAxes[i], {glm::dot(direction, right), -glm::dot(direction, up)}, -glm::dot(direction, front)};
        }
        std::ranges::stable_sort(items, {}, &AxisGizmoItem::Depth);
        return items;
    }

    [[nodiscard]] inline std::optional<ViewAxis> HitAxisGizmo(const std::span<const AxisGizmoItem> items, const glm::vec2& mouse,
                                                              const float radiusPx, const float hitRadiusPx)
    {
        std::optional<ViewAxis> best;
        float bestDepth = -std::numeric_limits<float>::infinity();
        for (const auto& item : items)
        {
            if (glm::distance(mouse, item.Offset * radiusPx) > hitRadiusPx || item.Depth < bestDepth) continue;
            best = item.Axis;
            bestDepth = item.Depth;
        }
        return best;
    }
}
