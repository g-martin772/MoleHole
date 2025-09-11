#include "Input.h"
#include <GLFW/glfw3.h>

Input::Input(GLFWwindow* window)
    : m_Window(window), m_LastX(0.0), m_LastY(0.0), m_DeltaX(0.0), m_DeltaY(0.0),
      m_FirstMouse(true), m_ViewportHovered(false), m_ViewportFocused(false) {}

void Input::Update() {
    double xpos, ypos;
    glfwGetCursorPos(m_Window, &xpos, &ypos);
    if (m_FirstMouse) {
        m_LastX = xpos;
        m_LastY = ypos;
        m_FirstMouse = false;
    }
    m_DeltaX = xpos - m_LastX;
    m_DeltaY = m_LastY - ypos; // Invert Y for typical FPS camera
    m_LastX = xpos;
    m_LastY = ypos;
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

void Input::SetCursorEnabled(bool enabled) const {
    glfwSetInputMode(m_Window, GLFW_CURSOR, enabled ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
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
