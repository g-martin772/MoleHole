module;
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
export module MoleHole:Simulation.CameraMath;

import std;
import GPP;

export namespace MoleHole
{
    struct SceneCameraView
    {
        glm::vec3 Position{0.0f};
        glm::vec3 Front{0.0f, 0.0f, -1.0f};
        glm::vec3 Up{0.0f, 1.0f, 0.0f};
        glm::vec3 Right{1.0f, 0.0f, 0.0f};
        float Fov{60.0f};
        float NearPlane{0.1f};
        float FarPlane{1000.0f};
    };

    // Identity orientation looks down -Z with +Y up.
    [[nodiscard]] inline glm::quat LookAtRotation(const glm::vec3& eye, const glm::vec3& target,
                                                  const glm::vec3& up = {0.0f, 1.0f, 0.0f})
    {
        const glm::vec3 delta = target - eye;
        if (glm::dot(delta, delta) < 1.0e-12f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::vec3 forward = glm::normalize(delta);
        const glm::vec3 safeUp = std::abs(glm::dot(forward, glm::normalize(up))) > 0.9999f
                                     ? glm::vec3(0.0f, 0.0f, 1.0f)
                                     : up;
        return glm::quatLookAt(forward, safeUp);
    }

    [[nodiscard]] inline glm::vec3 LookAtEulerDegrees(const glm::vec3& eye, const glm::vec3& target)
    {
        return glm::degrees(glm::eulerAngles(LookAtRotation(eye, target)));
    }

    [[nodiscard]] inline SceneCameraView MakeSceneCameraView(const GPP::TransformComponent& transform,
                                                             const GPP::CameraComponent& camera)
    {
        const glm::quat rotation = glm::normalize(transform.Rotation);
        SceneCameraView view;
        view.Position = transform.Position;
        view.Front = rotation * glm::vec3(0.0f, 0.0f, -1.0f);
        view.Up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
        view.Right = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
        view.Fov = camera.Fov;
        view.NearPlane = camera.NearPlane;
        view.FarPlane = camera.FarPlane;
        return view;
    }

    [[nodiscard]] inline glm::mat4 SceneCameraViewMatrix(const SceneCameraView& view)
    {
        return glm::lookAt(view.Position, view.Position + view.Front, view.Up);
    }

    [[nodiscard]] inline std::optional<SceneCameraView> FindPrimarySceneCamera(const GPP::Scene& scene)
    {
        for (auto [entity, camera, transform] :
             scene.Registry().view<const GPP::CameraComponent, const GPP::TransformComponent>().each())
        {
            if (camera.Primary) return MakeSceneCameraView(transform, camera);
        }
        return std::nullopt;
    }
}
