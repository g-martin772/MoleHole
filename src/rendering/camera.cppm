export module MoleHole:Rendering.Camera;

import std;
import glm;
import GPP;

export namespace MoleHole
{
    class Camera
    {
    public:
        Camera(float fov = 60.0f, float aspect = 16.0f / 9.0f, float nearPlane = 0.1f, float farPlane = 10000.0f)
            : m_Fov(fov), m_Aspect(aspect), m_NearPlane(nearPlane), m_FarPlane(farPlane)
        {
            UpdateCameraVectors();
        }

        void SetPosition(const glm::vec3& position) { m_Position = position; }
        void SetYawPitch(float yaw, float pitch)
        {
            m_Yaw = yaw;
            m_Pitch = pitch;
            UpdateCameraVectors();
        }

        void ProcessKeyboard(float forward, float right, float up, float deltaTime, float speed = 5.0f)
        {
            const float velocity = speed * deltaTime;
            m_Position += m_Front * forward * velocity;
            m_Position += m_Right * right * velocity;
            m_Position += m_Up * up * velocity;
        }

        void ProcessMouse(float xOffset, float yOffset, float sensitivity = 0.1f, bool constrainPitch = true)
        {
            m_Yaw += xOffset * sensitivity;
            m_Pitch += yOffset * sensitivity;
            if (constrainPitch)
            {
                m_Pitch = std::clamp(m_Pitch, -89.0f, 89.0f);
            }
            UpdateCameraVectors();
        }

        [[nodiscard]] glm::mat4 GetViewMatrix() const
        {
            return glm::lookAt(m_Position, m_Position + m_Front, m_Up);
        }

        [[nodiscard]] glm::mat4 GetProjectionMatrix() const
        {
            return glm::perspective(glm::radians(m_Fov), m_Aspect, m_NearPlane, m_FarPlane);
        }

        [[nodiscard]] glm::mat4 GetViewProjectionMatrix() const
        {
            return GetProjectionMatrix() * GetViewMatrix();
        }

        [[nodiscard]] const glm::vec3& GetPosition() const noexcept { return m_Position; }
        [[nodiscard]] const glm::vec3& GetFront() const noexcept { return m_Front; }
        [[nodiscard]] const glm::vec3& GetUp() const noexcept { return m_Up; }
        [[nodiscard]] const glm::vec3& GetRight() const noexcept { return m_Right; }
        [[nodiscard]] float GetYaw() const noexcept { return m_Yaw; }
        [[nodiscard]] float GetPitch() const noexcept { return m_Pitch; }
        [[nodiscard]] float GetFov() const noexcept { return m_Fov; }
        void SetFov(float fov) noexcept { m_Fov = fov; }
        void SetAspect(float aspect) noexcept { m_Aspect = aspect; }

    private:
        void UpdateCameraVectors()
        {
            const glm::vec3 front{
                std::cos(glm::radians(m_Yaw)) * std::cos(glm::radians(m_Pitch)),
                std::sin(glm::radians(m_Pitch)),
                std::sin(glm::radians(m_Yaw)) * std::cos(glm::radians(m_Pitch))
            };
            m_Front = glm::normalize(front);
            m_Right = glm::normalize(glm::cross(m_Front, m_WorldUp));
            m_Up = glm::normalize(glm::cross(m_Right, m_Front));
        }

        glm::vec3 m_Position{0.0f, 20.0f, 100.0f};
        glm::vec3 m_WorldUp{0.0f, 1.0f, 0.0f};
        glm::vec3 m_Front{0.0f, 0.0f, -1.0f};
        glm::vec3 m_Up{0.0f, 1.0f, 0.0f};
        glm::vec3 m_Right{1.0f, 0.0f, 0.0f};
        float m_Yaw{-90.0f};
        float m_Pitch{0.0f};
        float m_Fov;
        float m_Aspect;
        float m_NearPlane;
        float m_FarPlane;
    };
}
