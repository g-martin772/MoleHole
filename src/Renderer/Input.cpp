#include "Input.h"

#include <cmath>
#include <GLFW/glfw3.h>

Input::Input(GLFWwindow* window)
    : m_Window(window), m_LastX(0.0), m_LastY(0.0), m_DeltaX(0.0), m_DeltaY(0.0),
      m_FirstMouse(true), m_ViewportHovered(false), m_ViewportFocused(false),
      m_CursorDisabled(false), m_CenterX(0.0), m_CenterY(0.0) {
    int width, height;
    glfwGetWindowSize(m_Window, &width, &height);
    m_CenterX = width * 0.5;
    m_CenterY = height * 0.5;
}

void Input::Update() {
    double xpos, ypos;
    glfwGetCursorPos(m_Window, &xpos, &ypos);

    if (m_FirstMouse || !m_CursorDisabled) {
        m_LastX = xpos;
        m_LastY = ypos;
        m_FirstMouse = false;
        m_DeltaX = 0.0;
        m_DeltaY = 0.0;
        return;
    }

    if (m_CursorDisabled) {
        double deltaX = xpos - m_LastX;
        double deltaY = ypos - m_LastY;

        int width, height;
        glfwGetWindowSize(m_Window, &width, &height);
        double threshold = std::min(width, height) * 0.3;

        if (std::abs(deltaX) > threshold || std::abs(deltaY) > threshold) {
            m_DeltaX = 0.0;
            m_DeltaY = 0.0;
        } else {
            m_DeltaX = deltaX;
            m_DeltaY = -deltaY;
        }

        m_LastX = xpos;
        m_LastY = ypos;
    } else {
        m_DeltaX = xpos - m_LastX;
        m_DeltaY = m_LastY - ypos;
        m_LastX = xpos;
        m_LastY = ypos;
    }
}

bool Input::IsKeyDown(int key) const {
    return glfwGetKey(m_Window, key) == GLFW_PRESS;
}

bool Input::IsMouseButtonDown(int button) const {
    return glfwGetMouseButton(m_Window, button) == GLFW_PRESS;
}

void Input::GetMouseDelta(double& x, double& y) const {
    x = m_DeltaX;
    y = m_DeltaY;
}

void Input::GetMousePos(double& x, double& y) const {
    glfwGetCursorPos(m_Window, &x, &y);
}

void Input::SetCursorEnabled(bool enabled) {
    if (enabled != !m_CursorDisabled) {
        m_CursorDisabled = !enabled;
        glfwSetInputMode(m_Window, GLFW_CURSOR, enabled ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);

        if (!enabled) {
            int width, height;
            glfwGetWindowSize(m_Window, &width, &height);
            m_CenterX = width * 0.5;
            m_CenterY = height * 0.5;
            glfwSetCursorPos(m_Window, m_CenterX, m_CenterY);
            m_LastX = m_CenterX;
            m_LastY = m_CenterY;
            m_FirstMouse = true;
        }
    }
}

bool Input::IsViewportFocused() const {
    return m_ViewportFocused;
}

void Input::SetViewportHovered(bool hovered) {
    m_ViewportHovered = hovered;
}

void Input::SetViewportFocused(bool focused) {
    m_ViewportFocused = focused;
}
