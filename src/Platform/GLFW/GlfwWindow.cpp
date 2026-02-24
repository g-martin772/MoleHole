#include "Application/Window.h"
#include <GLFW/glfw3.h>

class GLFWWindow : public Window {
public:
    GLFWWindow() : m_window(nullptr), m_vsyncEnabled(true) {}

    ~GLFWWindow() override {
        GLFWWindow::Shutdown();
    }

    bool Initialize(const WindowProperties& props) override {
        if (!s_glfwInitialized) {
            if (!glfwInit()) {
                spdlog::error("Failed to initialize GLFW");
                return false;
            }
            s_glfwInitialized = true;
            spdlog::info("GLFW initialized");

            glfwSetErrorCallback([](int error, const char* description) {
                spdlog::error("GLFW Error ({}): {}", error, description);
            });
        }

        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, props.contextVersionMajor);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, props.contextVersionMinor);
        glfwWindowHint(GLFW_OPENGL_PROFILE, props.coreProfile ? GLFW_OPENGL_CORE_PROFILE : GLFW_OPENGL_COMPAT_PROFILE);
        glfwWindowHint(GLFW_VISIBLE, props.visible ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, props.resizable ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_DECORATED, props.decorated ? GLFW_TRUE : GLFW_FALSE);

        m_window = glfwCreateWindow(
            static_cast<int>(props.width),
            static_cast<int>(props.height),
            props.title.c_str(),
            nullptr,
            nullptr
        );

        if (!m_window) {
            spdlog::error("Failed to create GLFW window");
            return false;
        }

        glfwMakeContextCurrent(m_window);

        SetVSync(props.vsync);

        glfwSetWindowUserPointer(m_window, this);

        glfwSetWindowSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
            auto* win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            if (win && win->m_resizeCallback) {
                win->m_resizeCallback(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            }
        });

        glfwSetWindowPosCallback(m_window, [](GLFWwindow* window, int x, int y) {
            auto* win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            if (win && win->m_positionCallback) {
                win->m_positionCallback(x, y);
            }
        });

        glfwSetWindowMaximizeCallback(m_window, [](GLFWwindow* window, int maximized) {
            auto* win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            if (win && win->m_maximizeCallback) {
                win->m_maximizeCallback(maximized == GLFW_TRUE);
            }
        });

        glfwSetWindowCloseCallback(m_window, [](GLFWwindow* window) {
            auto* win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            if (win && win->m_closeCallback) {
                win->m_closeCallback();
            }
        });

        glfwSetWindowFocusCallback(m_window, [](GLFWwindow* window, int focused) {
            auto* win = static_cast<GLFWWindow*>(glfwGetWindowUserPointer(window));
            if (win && win->m_focusCallback) {
                win->m_focusCallback(focused == GLFW_TRUE);
            }
        });

        spdlog::info("Window created: {}x{} '{}'", props.width, props.height, props.title);
        return true;
    }

    void Shutdown() override {
        if (m_window) {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
            spdlog::debug("Window destroyed");
        }
    }

    void PollEvents() override {
        glfwPollEvents();
    }

    void SwapBuffers() override {
        if (m_window) {
            glfwSwapBuffers(m_window);
        }
    }

    void MakeContextCurrent() override {
        if (m_window) {
            glfwMakeContextCurrent(m_window);
        }
    }

    bool ShouldClose() const override {
        return m_window ? glfwWindowShouldClose(m_window) : true;
    }

    void SetShouldClose(bool shouldClose) override {
        if (m_window) {
            glfwSetWindowShouldClose(m_window, shouldClose ? GLFW_TRUE : GLFW_FALSE);
        }
    }

    bool IsMinimized() const override {
        return m_window ? glfwGetWindowAttrib(m_window, GLFW_ICONIFIED) == GLFW_TRUE : false;
    }

    bool IsMaximized() const override {
        return m_window ? glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED) == GLFW_TRUE : false;
    }

    bool IsFocused() const override {
        return m_window ? glfwGetWindowAttrib(m_window, GLFW_FOCUSED) == GLFW_TRUE : false;
    }

    uint32_t GetWidth() const override {
        if (!m_window) return 0;
        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        return static_cast<uint32_t>(width);
    }

    uint32_t GetHeight() const override {
        if (!m_window) return 0;
        int width, height;
        glfwGetWindowSize(m_window, &width, &height);
        return static_cast<uint32_t>(height);
    }

    void GetFramebufferSize(int& width, int& height) const override {
        if (m_window) {
            glfwGetFramebufferSize(m_window, &width, &height);
        } else {
            width = height = 0;
        }
    }

    void GetPosition(int& x, int& y) const override {
        if (m_window) {
            glfwGetWindowPos(m_window, &x, &y);
        } else {
            x = y = 0;
        }
    }

    void SetSize(uint32_t width, uint32_t height) override {
        if (m_window) {
            glfwSetWindowSize(m_window, static_cast<int>(width), static_cast<int>(height));
        }
    }

    void SetPosition(int x, int y) override {
        if (m_window) {
            glfwSetWindowPos(m_window, x, y);
        }
    }

    void SetTitle(const std::string& title) override {
        if (m_window) {
            glfwSetWindowTitle(m_window, title.c_str());
        }
    }

    void Maximize() override {
        if (m_window) {
            glfwMaximizeWindow(m_window);
        }
    }

    void Minimize() override {
        if (m_window) {
            glfwIconifyWindow(m_window);
        }
    }

    void Restore() override {
        if (m_window) {
            glfwRestoreWindow(m_window);
        }
    }

    void SetVSync(bool enabled) override {
        m_vsyncEnabled = enabled;
        glfwSwapInterval(enabled ? 1 : 0);
    }

    void Show() override {
        if (m_window) {
            glfwShowWindow(m_window);
        }
    }

    void Hide() override {
        if (m_window) {
            glfwHideWindow(m_window);
        }
    }

    void SetResizeCallback(ResizeCallback callback) override {
        m_resizeCallback = std::move(callback);
    }

    void SetPositionCallback(PositionCallback callback) override {
        m_positionCallback = std::move(callback);
    }

    void SetMaximizeCallback(MaximizeCallback callback) override {
        m_maximizeCallback = std::move(callback);
    }

    void SetCloseCallback(CloseCallback callback) override {
        m_closeCallback = std::move(callback);
    }

    void SetFocusCallback(FocusCallback callback) override {
        m_focusCallback = std::move(callback);
    }

    void SetUserPointer(void* ptr) override {
        m_userPointer = ptr;
    }

    void* GetUserPointer() const override {
        return m_userPointer;
    }

    GLFWwindow* GetNativeWindow() const override {
        return m_window;
    }

    void* GetProcAddress(const char* name) const override {
        return reinterpret_cast<void*>(glfwGetProcAddress(name));
    }

    double GetTime() const override {
        return glfwGetTime();
    }

private:
    GLFWwindow* m_window;
    bool m_vsyncEnabled;
    void* m_userPointer = nullptr;

    ResizeCallback m_resizeCallback;
    PositionCallback m_positionCallback;
    MaximizeCallback m_maximizeCallback;
    CloseCallback m_closeCallback;
    FocusCallback m_focusCallback;

    static inline bool s_glfwInitialized = false;
};

Window* Window::Create(const WindowProperties& props) {
    auto* window = new GLFWWindow();
    if (!window->Initialize(props)) {
        delete window;
        return nullptr;
    }
    return window;
}

void TerminateWindowSystem() {
    glfwTerminate();
    spdlog::info("GLFW terminated");
}






