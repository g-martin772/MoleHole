#pragma once

#include <string>
#include <filesystem>
#include <array>
#include <variant>
#include <vector>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <yaml-cpp/yaml.h>

/*
TODO
automate the SerializeToYaml and DeserializeFromYaml to use the specified groups.
also, for defining config fields i only want to add them somewhere once by defining
their metadata and the rest of the system just picks it up.
so also create functions to draw the imgui editor ui for single parameters
and entire groups and use these helper methods so new config values get automatically
picked up as long as they belong to an existing section. consider the showInUI and isReadOnly flags.
when provided also show the scaling values, things like mass might have units like kg,
earth masses and solar masses or speed in km/h or multiples of c and so on that should be
available via an additional dropdown. use macros where applicable but prefer
c++ templates where possible. instead of InitializeDefaults like this the default value should come from the metadata
 */

enum class DebugMode {
    Normal = 0,
    InfluenceZones = 1,
    DeflectionMagnitude = 2,
    GravitationalField = 3,
    SphericalShape = 4,
    DebugLUT = 5,
    GravityGrid = 6
};

enum class ParameterGroup {
    Window,
    Camera,
    Rendering,
    Physics,
    Debug,
    Simulation,
    Application,
    Export
};

enum class ParameterType {
    Bool,
    Int,
    Float,
    String,
    Vec2,
    Vec3,
    Vec4,
    Enum,
    StringVector
};

enum class StateParameter {
    WindowWidth,
    WindowHeight,
    WindowPosX,
    WindowPosY,
    WindowMaximized,
    WindowVSync,

    CameraPositionX,
    CameraPositionY,
    CameraPositionZ,
    CameraFrontX,
    CameraFrontY,
    CameraFrontZ,
    CameraUpX,
    CameraUpY,
    CameraUpZ,
    CameraPitch,
    CameraYaw,

    RenderingFov,
    RenderingMaxRaySteps,
    RenderingRayStepSize,
    RenderingDebugMode,
    RenderingPhysicsDebugEnabled,
    RenderingPhysicsDebugDepthTest,
    RenderingPhysicsDebugScale,
    RenderingPhysicsDebugFlags,

    RenderingBlackHolesEnabled,
    RenderingGravitationalLensingEnabled,
    RenderingGravitationalRedshiftEnabled,
    RenderingAccretionDiskEnabled,
    RenderingAccDiskHeight,
    RenderingAccDiskNoiseScale,
    RenderingAccDiskNoiseLOD,
    RenderingAccDiskSpeed,
    RenderingDopplerBeamingEnabled,
    RenderingBloomEnabled,
    RenderingBloomThreshold,
    RenderingBloomBlurPasses,
    RenderingBloomIntensity,
    RenderingBloomDebug,

    AppLastOpenScene,
    AppRecentScenes,
    AppShowDemoWindow,
    AppDebugMode,
    AppUseKerrDistortion,
    AppCameraSpeed,
    AppMouseSensitivity,

    COUNT
};

struct ParameterMetadata {
    const char* name;
    const char* displayName;
    const char* tooltip;
    ParameterType type;
    ParameterGroup group;

    float minValue = 0.0f;
    float maxValue = 0.0f;
    float dragSpeed = 1.0f;

    std::vector<std::string> scaleValueNames;
    std::vector<float> scaleValues;
    std::vector<std::string> enumValues;

    bool isReadOnly = false;
    bool showInUI = true;
};

class AppState {
public:
    using StateValue = std::variant<bool, int, float, std::string, glm::vec3, std::vector<std::string>>;

    void LoadFromFile(const std::filesystem::path &configPath = "config.yaml");
    void SaveToFile(const std::filesystem::path &configPath = "config.yaml");

    template<typename T>
    void Set(StateParameter param, const T &value);

    template<typename T>
    T Get(StateParameter param) const;

    bool GetBool(StateParameter param) const;
    int GetInt(StateParameter param) const;
    float GetFloat(StateParameter param) const;
    const std::string& GetString(StateParameter param) const;
    glm::vec3 GetVec3(StateParameter param) const;
    const std::vector<std::string>& GetStringVector(StateParameter param) const;

    void SetBool(StateParameter param, bool value);
    void SetInt(StateParameter param, int value);
    void SetFloat(StateParameter param, float value);
    void SetString(StateParameter param, const std::string &value);
    void SetVec3(StateParameter param, const glm::vec3 &value);
    void SetStringVector(StateParameter param, const std::vector<std::string> &value);

    glm::vec3 GetCameraPosition() const;
    glm::vec3 GetCameraFront() const;
    glm::vec3 GetCameraUp() const;
    void SetCameraPosition(const glm::vec3 &position);
    void SetCameraFront(const glm::vec3 &front);
    void SetCameraUp(const glm::vec3 &up);

    void UpdateWindowState(int width, int height, int posX, int posY, bool maximized);
    void UpdateCameraState(const glm::vec3 &position, const glm::vec3 &front, const glm::vec3 &up, float pitch, float yaw);

    static const ParameterMetadata& GetMetadata(StateParameter param);

private:
    std::array<StateValue, static_cast<size_t>(StateParameter::COUNT)> m_values;
    std::filesystem::path m_configPath;

    void InitializeDefaults();
    YAML::Node SerializeToYaml() const;
    void DeserializeFromYaml(const YAML::Node &node);
};

template<typename T>
void AppState::Set(StateParameter param, const T &value) {
    m_values[static_cast<size_t>(param)] = value;
}

template<typename T>
T AppState::Get(StateParameter param) const {
    return std::get<T>(m_values[static_cast<size_t>(param)]);
}
