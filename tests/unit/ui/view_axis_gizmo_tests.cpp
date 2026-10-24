#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

import MoleHole;
import glm;
import std;

using namespace MoleHole;
using Catch::Approx;

TEST_CASE("Each snap view looks along the negative of its axis", "[ui][viewport]")
{
    for (const auto axis : kViewAxes)
    {
        const auto front = FrontFromAngles(SnapAngles(axis, 33.0f));
        const auto direction = AxisDirection(axis);
        CHECK(front.x == Approx(-direction.x).margin(5.0e-3));
        CHECK(front.y == Approx(-direction.y).margin(5.0e-3));
        CHECK(front.z == Approx(-direction.z).margin(5.0e-3));
    }
}

TEST_CASE("Top and bottom views keep the current yaw and stay off the pole", "[ui][viewport]")
{
    CHECK(SnapAngles(ViewAxis::PosY, 42.0f).Yaw == 42.0f);
    CHECK(SnapAngles(ViewAxis::NegY, -7.0f).Yaw == -7.0f);
    CHECK(SnapAngles(ViewAxis::PosY, 0.0f).Pitch < 90.0f);
    CHECK(SnapAngles(ViewAxis::PosY, 0.0f).Pitch == -kSnapPitchDegrees);
    CHECK(SnapAngles(ViewAxis::NegY, 0.0f).Pitch == kSnapPitchDegrees);
}

TEST_CASE("Snapping orbits the pivot at constant distance", "[ui][viewport]")
{
    const glm::dvec3 pivot{100.0, 50.0, -20.0};
    const glm::dvec3 camera{100.0, 50.0, 480.0};
    const auto snapped = SnapView(camera, pivot, ViewAxis::PosX, 0.0f);
    CHECK(glm::distance(snapped.PositionM, pivot) == Approx(500.0));
    CHECK(snapped.PositionM.x == Approx(600.0).margin(5.0e-3));
    CHECK(snapped.PositionM.y == Approx(50.0).margin(5.0e-3));
    CHECK(snapped.PositionM.z == Approx(-20.0).margin(5.0e-3));
}

TEST_CASE("Snapping at the pivot only turns the camera", "[ui][viewport]")
{
    const glm::dvec3 here{1.0, 2.0, 3.0};
    const auto snapped = SnapView(here, here, ViewAxis::NegZ, 0.0f);
    CHECK(snapped.PositionM == here);
    CHECK(snapped.Angles.Yaw == 90.0f);
}

TEST_CASE("The default camera shows +Z on the viewer's side and +X on the right", "[ui][viewport]")
{
    const glm::vec3 front{0.0f, 0.0f, -1.0f};
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    const glm::vec3 right{1.0f, 0.0f, 0.0f};
    const auto items = LayoutAxisGizmo(right, up, front);
    const auto find = [&](const ViewAxis axis) { return *std::ranges::find(items, axis, &AxisGizmoItem::Axis); };
    CHECK(find(ViewAxis::PosX).Offset.x == Approx(1.0f));
    CHECK(find(ViewAxis::PosY).Offset.y == Approx(-1.0f));
    CHECK(find(ViewAxis::PosZ).Depth == Approx(1.0f));
    CHECK(find(ViewAxis::NegZ).Depth == Approx(-1.0f));
    CHECK(items.front().Depth <= items.back().Depth);
}

TEST_CASE("Hit testing prefers the item facing the viewer", "[ui][viewport]")
{
    const std::array<AxisGizmoItem, 3> items{{
        {ViewAxis::NegZ, {0.0f, 0.0f}, -1.0f},
        {ViewAxis::PosZ, {0.0f, 0.0f}, 1.0f},
        {ViewAxis::PosX, {1.0f, 0.0f}, 0.0f},
    }};
    CHECK(HitAxisGizmo(items, {1.0f, 0.0f}, 40.0f, 10.0f) == ViewAxis::PosZ);
    CHECK(HitAxisGizmo(items, {40.0f, 3.0f}, 40.0f, 10.0f) == ViewAxis::PosX);
    CHECK_FALSE(HitAxisGizmo(items, {200.0f, 200.0f}, 40.0f, 10.0f).has_value());
}

TEST_CASE("A snapped camera's axis gizmo has the clicked axis facing the viewer", "[ui][viewport]")
{
    for (const auto axis : kViewAxes)
    {
        const auto angles = SnapAngles(axis, 10.0f);
        const auto front = FrontFromAngles(angles);
        const glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 up = glm::normalize(glm::cross(right, front));
        const auto items = LayoutAxisGizmo(right, up, front);
        CHECK(items.back().Axis == axis);
    }
}
