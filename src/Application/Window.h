#pragma once

struct GLFWwindow;

struct WindowProperties {
    std::string title = "MoleHole";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool visible = true;
    bool resizable = true;
    bool decorated = true;
    bool vsync = true;

    int contextVersionMajor = 4;
    int contextVersionMinor = 6;
    bool coreProfile = true;
};

class Window {
public:
    using ResizeCallback = std::function<void(uint32_t width, uint32_t height)>;
    using PositionCallback = std::function<void(int x, int y)>;
    using MaximizeCallback = std::function<void(bool maximized)>;
    using CloseCallback = std::function<void()>;
    using FocusCallback = std::function<void(bool focused)>;

    Window() = default;
    virtual ~Window() = default;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    static Window* Create(const WindowProperties& props = WindowProperties());

    virtual bool Initialize(const WindowProperties& props) = 0;
    virtual void Shutdown() = 0;
    virtual void PollEvents() = 0;
    virtual void SwapBuffers() = 0;
    virtual void MakeContextCurrent() = 0;

    virtual bool ShouldClose() const = 0;
    virtual void SetShouldClose(bool shouldClose) = 0;
    virtual bool IsMinimized() const = 0;
    virtual bool IsMaximized() const = 0;
    virtual bool IsFocused() const = 0;

    virtual uint32_t GetWidth() const = 0;
    virtual uint32_t GetHeight() const = 0;
    virtual void GetFramebufferSize(int& width, int& height) const = 0;
    virtual void GetPosition(int& x, int& y) const = 0;

    virtual void SetSize(uint32_t width, uint32_t height) = 0;
    virtual void SetPosition(int x, int y) = 0;
    virtual void SetTitle(const std::string& title) = 0;
    virtual void Maximize() = 0;
    virtual void Minimize() = 0;
    virtual void Restore() = 0;
    virtual void SetVSync(bool enabled) = 0;

    virtual void Show() = 0;
    virtual void Hide() = 0;

    virtual void SetResizeCallback(ResizeCallback callback) = 0;
    virtual void SetPositionCallback(PositionCallback callback) = 0;
    virtual void SetMaximizeCallback(MaximizeCallback callback) = 0;
    virtual void SetCloseCallback(CloseCallback callback) = 0;
    virtual void SetFocusCallback(FocusCallback callback) = 0;

    virtual void SetUserPointer(void* ptr) = 0;
    virtual void* GetUserPointer() const = 0;

    virtual GLFWwindow* GetNativeWindow() const = 0;
    virtual void* GetProcAddress(const char* name) const = 0;
};

void TerminateWindowSystem();

