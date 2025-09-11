#pragma once
#include <GLFW/glfw3.h>

class Input {
public:
    Input(GLFWwindow* window);
    void Update();
    bool IsKeyDown(int key) const;
    bool IsMouseButtonDown(int button) const;
    void GetMouseDelta(double& x, double& y) const;
    void GetMousePos(double& x, double& y) const;
    void SetCursorEnabled(bool enabled) const;
    bool IsViewportFocused() const;
    void SetViewportHovered(bool hovered);
    void SetViewportFocused(bool focused);
private:
    GLFWwindow* m_Window;
    double m_LastX, m_LastY;
    double m_DeltaX, m_DeltaY;
    bool m_FirstMouse;
    bool m_ViewportHovered;
    bool m_ViewportFocused;
};
