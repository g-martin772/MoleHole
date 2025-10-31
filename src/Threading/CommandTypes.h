#pragma once
#include <string>
#include <functional>
#include <glm/glm.hpp>

// Forward declarations
class Camera;

namespace Threading {

// Viewport modes (copied from Renderer to avoid header dependency)
enum class ViewportMode {
    Demo1 = 0,
    Rays2D = 1,
    Simulation3D = 2,
    SimulationVisual = 3,
};

// Command from Main → Viewport Render Thread
struct RenderCommand {
    ViewportMode mode;
    float deltaTime;
    glm::vec3 cameraPosition;
    glm::vec3 cameraForward;
    glm::vec3 cameraUp;
    float cameraFov;
    float cameraNear;
    float cameraFar;
    bool debugMode;
    
    RenderCommand() = default;
    RenderCommand(ViewportMode m, float dt, const glm::vec3& pos, const glm::vec3& fwd, const glm::vec3& up, float fov)
        : mode(m)
        , deltaTime(dt)
        , cameraPosition(pos)
        , cameraForward(fwd)
        , cameraUp(up)
        , cameraFov(fov)
        , cameraNear(0.1f)
        , cameraFar(1000.0f)
        , debugMode(false)
    {}
};

// Command from Main → Simulation Thread
struct SimulationCommand {
    enum class Action {
        Start,
        Stop,
        Pause,
        Step,
        Continue
    };
    
    Action action;
    float deltaTime;
    bool executeStartEvent;
    bool executeTickEvent;
    
    SimulationCommand()
        : action(Action::Stop)
        , deltaTime(0.0f)
        , executeStartEvent(false)
        , executeTickEvent(false)
    {}
    
    SimulationCommand(Action a, float dt = 0.0f, bool start = false, bool tick = false)
        : action(a)
        , deltaTime(dt)
        , executeStartEvent(start)
        , executeTickEvent(tick)
    {}
};

// Command from Main → Resource Loader Thread
struct LoadRequest {
    enum class Type {
        Mesh,
        Texture,
        Scene
    };
    
    Type type;
    std::string path;
    std::function<void(void*)> callback;
    int priority; // Higher = more urgent
    
    LoadRequest()
        : type(Type::Mesh)
        , priority(0)
    {}
    
    LoadRequest(Type t, const std::string& p, std::function<void(void*)> cb, int prio = 0)
        : type(t)
        , path(p)
        , callback(std::move(cb))
        , priority(prio)
    {}
    
    bool operator<(const LoadRequest& other) const {
        return priority < other.priority; // Lower priority = higher in queue
    }
};

} // namespace Threading
