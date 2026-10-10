#pragma once
#include <glm/glm.hpp>

class Camera {
public:
    Camera(float fov, float aspect, float nearPlane, float farPlane);
    void SetPosition(const glm::vec3& pos);
    void SetYawPitch(float yaw, float pitch);
    void ProcessKeyboard(float forward, float right, float up, float deltaTime);
    void ProcessKeyboard(float forward, float right, float up, float deltaTime, float speed);
    void ProcessMouse(float xoffset, float yoffset, bool constrainPitch = true);
    void ProcessMouse(float xoffset, float yoffset, float sensitivity, bool constrainPitch = true);
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetProjectionMatrix() const;
    glm::mat4 GetViewProjectionMatrix() const;
    glm::vec3 GetPosition() const;
    float GetYaw() const;
    float GetPitch() const;
    void SetAspect(float aspect);
    float GetFov() const;
    void SetFov(float newFov);
    glm::vec3 GetFront() const;
    glm::vec3 GetUp() const;
private:
    void UpdateCameraVectors();
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    glm::vec3 worldUp;
    float yaw;
    float pitch;
    float fov;
    float aspect;
    float nearPlane;
    float farPlane;
};
