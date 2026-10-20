module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
export module MoleHole:Simulation.CameraMath;

import std;
import GPP;
import :Rendering.Frame;

export namespace MoleHole
{
    struct SceneCameraView
    {
        glm::dvec3 PositionM{0.0};
        glm::vec3 Front{0.0f, 0.0f, -1.0f};
        glm::vec3 Up{0.0f, 1.0f, 0.0f};
        glm::vec3 Right{1.0f, 0.0f, 0.0f};
        float Fov{60.0f};
        double NearPlaneM{0.1};
        double FarPlaneM{1000.0};
        std::uint64_t Guid{0}; // camera entity
    };

    [[nodiscard]] inline SceneCameraView MakeSceneCameraView(const GPP::TransformComponent& transform,
                                                             const GPP::CameraComponent& camera)
    {
        const glm::quat rotation = glm::normalize(transform.Rotation);
        SceneCameraView view;
        view.PositionM = transform.Position;
        view.Front = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        view.Up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        view.Right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        view.Fov = camera.Fov;
        view.NearPlaneM = camera.NearPlane;
        view.FarPlaneM = camera.FarPlane;
        return view;
    }

    [[nodiscard]] inline glm::mat4 SceneCameraViewMatrix(const SceneCameraView& view, const RenderFrame& frame)
    {
        const glm::vec3 position = frame.ToRender(view.PositionM);
        return glm::lookAt(position, position + view.Front, view.Up);
    }

    [[nodiscard]] inline std::optional<SceneCameraView> FindPrimarySceneCamera(const GPP::Scene& scene)
    {
        for (auto [entity, camera, transform] :
             scene.Registry().view<const GPP::CameraComponent, const GPP::TransformComponent>().each())
        {
            if (!camera.Primary) continue;
            auto view = MakeSceneCameraView(transform, camera);
            view.Guid = scene.GuidOf(entity);
            return view;
        }
        return std::nullopt;
    }
}
