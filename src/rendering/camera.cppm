export module MoleHole:Rendering.Camera;

import std;
import glm;
import GPP;
import :Rendering.Frame;

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

        void SetPosition(const glm::dvec3& positionM) { m_Position = positionM; }
        void SetYawPitch(float yaw, float pitch)
        {
            m_Yaw = yaw;
            m_Pitch = pitch;
            UpdateCameraVectors();
        }

        void SetView(const glm::dvec3& positionM, const glm::vec3& front, const glm::vec3& up)
        {
            m_Position = positionM;
            m_Front = glm::normalize(front);
            m_Right = glm::normalize(glm::cross(m_Front, up));
            m_Up = glm::normalize(glm::cross(m_Right, m_Front));
        }

        void SetClipPlanes(float nearPlane, float farPlane) noexcept
        {
            m_NearPlane = nearPlane;
            m_FarPlane = farPlane;
        }

        void ProcessKeyboard(float forward, float right, float up, float deltaTime, double speedMs)
        {
            const double distance = speedMs * static_cast<double>(deltaTime);
            m_Position += glm::dvec3(m_Front) * (static_cast<double>(forward) * distance);
            m_Position += glm::dvec3(m_Right) * (static_cast<double>(right) * distance);
            m_Position += glm::dvec3(m_Up) * (static_cast<double>(up) * distance);
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

        [[nodiscard]] glm::mat4 GetViewMatrix(const RenderFrame& frame) const
        {
            const glm::vec3 position = frame.ToRender(m_Position);
            return glm::lookAt(position, position + m_Front, m_Up);
        }

        [[nodiscard]] glm::mat4 GetProjectionMatrix() const
        {
            // glm's Y convention is OpenGL's (NDC +Y up); Vulkan's is the opposite
            glm::mat4 projection = glm::perspective(glm::radians(m_Fov), m_Aspect, m_NearPlane, m_FarPlane);
            projection[1][1] *= -1.0f;
            return projection;
        }

        [[nodiscard]] glm::mat4 GetViewProjectionMatrix(const RenderFrame& frame) const
        {
            return GetProjectionMatrix() * GetViewMatrix(frame);
        }

        // World position in metres (SI, double).
        [[nodiscard]] const glm::dvec3& GetPosition() const noexcept { return m_Position; }
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

        glm::dvec3 m_Position{0.0, 20.0 * GPP::Units::kSolarGravitationalRadius, 100.0 * GPP::Units::kSolarGravitationalRadius};
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
