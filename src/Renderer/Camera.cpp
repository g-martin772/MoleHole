#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

Camera::Camera(float fov, float aspect, float nearPlane, float farPlane)
    : position(0.0f, 0.0f, 3.0f), worldUp(0.0f, 1.0f, 0.0f), yaw(-90.0f), pitch(0.0f),
      fov(fov), aspect(aspect), nearPlane(nearPlane), farPlane(farPlane) {
    UpdateCameraVectors();
}

void Camera::SetPosition(const glm::vec3& pos) {
    position = pos;
    UpdateCameraVectors();
}

void Camera::SetYawPitch(float y, float p) {
    yaw = y;
    pitch = p;
    UpdateCameraVectors();
}

void Camera::ProcessKeyboard(float forward, float rightMove, float upMove, float deltaTime) {
    float velocity = 5.0f * deltaTime;
    position += front * forward * velocity;
    position += right * rightMove * velocity;
    position += up * upMove * velocity;
}

void Camera::ProcessMouse(float xoffset, float yoffset, bool constrainPitch) {
    float sensitivity = 0.1f;
    yaw += xoffset * sensitivity;
    pitch += yoffset * sensitivity;
    if (constrainPitch) {
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;
    }
    UpdateCameraVectors();
}

glm::mat4 Camera::GetViewMatrix() const {
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::GetProjectionMatrix() const {
    return glm::perspective(glm::radians(fov), aspect, nearPlane, farPlane);
}

glm::mat4 Camera::GetViewProjectionMatrix() const {
    return GetProjectionMatrix() * GetViewMatrix();
}

glm::vec3 Camera::GetPosition() const {
    return position;
}

float Camera::GetYaw() const { return yaw; }
float Camera::GetPitch() const { return pitch; }

void Camera::UpdateCameraVectors() {
    glm::vec3 f;
    f.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    f.y = sin(glm::radians(pitch));
    f.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    front = glm::normalize(f);
    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

