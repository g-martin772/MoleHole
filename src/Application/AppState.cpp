#include "AppState.h"
#include <spdlog/spdlog.h>
#include <fstream>

namespace YAML {
    template<>
    struct convert<glm::vec3> {
        static Node encode(const glm::vec3 &rhs) {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            return node;
        }

        static bool decode(const Node &node, glm::vec3 &rhs) {
            if (!node.IsSequence() || node.size() != 3) {
                return false;
            }
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            return true;
        }
    };
}

void AppState::InitializeDefaults() {
    Set(StateParameter::WindowWidth, 800);
    Set(StateParameter::WindowHeight, 600);
    Set(StateParameter::WindowPosX, -1);
    Set(StateParameter::WindowPosY, -1);
    Set(StateParameter::WindowMaximized, false);
    Set(StateParameter::WindowVSync, true);

    Set(StateParameter::CameraPositionX, 0.0f);
    Set(StateParameter::CameraPositionY, 0.0f);
    Set(StateParameter::CameraPositionZ, 5.0f);
    Set(StateParameter::CameraFrontX, 0.0f);
    Set(StateParameter::CameraFrontY, 0.0f);
    Set(StateParameter::CameraFrontZ, -1.0f);
    Set(StateParameter::CameraUpX, 0.0f);
    Set(StateParameter::CameraUpY, 1.0f);
    Set(StateParameter::CameraUpZ, 0.0f);
    Set(StateParameter::CameraPitch, 0.0f);
    Set(StateParameter::CameraYaw, -90.0f);

    Set(StateParameter::RenderingFov, 45.0f);
    Set(StateParameter::RenderingMaxRaySteps, 1000);
    Set(StateParameter::RenderingRayStepSize, 0.01f);
    Set(StateParameter::RenderingDebugMode, static_cast<int>(DebugMode::Normal));
    Set(StateParameter::RenderingPhysicsDebugEnabled, false);
    Set(StateParameter::RenderingPhysicsDebugDepthTest, true);
    Set(StateParameter::RenderingPhysicsDebugScale, 1.0f);
    Set(StateParameter::RenderingPhysicsDebugFlags, 0);

    Set(StateParameter::RenderingBlackHolesEnabled, 1);
    Set(StateParameter::RenderingGravitationalLensingEnabled, 1);
    Set(StateParameter::RenderingGravitationalRedshiftEnabled, 1);
    Set(StateParameter::RenderingAccretionDiskEnabled, 1);
    Set(StateParameter::RenderingAccDiskHeight, 0.2f);
    Set(StateParameter::RenderingAccDiskNoiseScale, 1.0f);
    Set(StateParameter::RenderingAccDiskNoiseLOD, 5.0f);
    Set(StateParameter::RenderingAccDiskSpeed, 0.5f);
    Set(StateParameter::RenderingDopplerBeamingEnabled, 1);
    Set(StateParameter::RenderingBloomEnabled, 1);
    Set(StateParameter::RenderingBloomThreshold, 1.0f);
    Set(StateParameter::RenderingBloomBlurPasses, 4);
    Set(StateParameter::RenderingBloomIntensity, 1.0f);
    Set(StateParameter::RenderingBloomDebug, 0);

    Set(StateParameter::AppLastOpenScene, std::string(""));
    Set(StateParameter::AppRecentScenes, std::vector<std::string>());
    Set(StateParameter::AppShowDemoWindow, false);
    Set(StateParameter::AppDebugMode, 0);
    Set(StateParameter::AppUseKerrDistortion, true);
    Set(StateParameter::AppCameraSpeed, 5.0f);
    Set(StateParameter::AppMouseSensitivity, 0.1f);
}

void AppState::LoadFromFile(const std::filesystem::path& configPath) {
    m_configPath = configPath;
    InitializeDefaults();

    std::filesystem::path backupPath = configPath;
    backupPath += ".backup";

    bool loadedFromBackup = false;

    if (std::filesystem::exists(configPath)) {
        try {
            std::ifstream file(configPath);
            if (file.is_open() && file.peek() != std::ifstream::traits_type::eof()) {
                if (YAML::Node config = YAML::LoadFile(configPath.string()); config && !config.IsNull()) {
                    DeserializeFromYaml(config);
                    spdlog::info("Loaded application state from {}", configPath.string());
                    return;
                }
            }
            file.close();
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load config file {}: {}", configPath.string(), e.what());
        }
    }

    if (std::filesystem::exists(backupPath)) {
        try {
            std::ifstream file(backupPath);
            if (file.is_open() && file.peek() != std::ifstream::traits_type::eof()) {
                if (YAML::Node config = YAML::LoadFile(backupPath.string()); config && !config.IsNull()) {
                    DeserializeFromYaml(config);
                    std::filesystem::copy_file(backupPath, configPath, std::filesystem::copy_options::overwrite_existing);
                    loadedFromBackup = true;
                    spdlog::info("Loaded application state from backup {}", backupPath.string());
                    return;
                }
            }
            file.close();
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load backup config file {}: {}", backupPath.string(), e.what());
        }
    }

    if (!loadedFromBackup) {
        spdlog::info("No valid config found, using defaults");
    }
}

void AppState::SaveToFile(const std::filesystem::path& configPath) {
    if (!configPath.empty()) {
        m_configPath = configPath;
    }

    std::filesystem::path backupPath = m_configPath;
    backupPath += ".backup";

    try {
        if (std::filesystem::exists(m_configPath)) {
            if (std::filesystem::exists(backupPath)) {
                std::filesystem::remove(backupPath);
            }
            std::filesystem::copy_file(m_configPath, backupPath);
        }

        YAML::Node config = SerializeToYaml();

        std::filesystem::path tempPath = m_configPath;
        tempPath += ".tmp";

        if (std::filesystem::exists(tempPath)) {
            std::filesystem::remove(tempPath);
        }

        {
            std::ofstream file(tempPath);
            if (!file.is_open()) {
                throw std::runtime_error("Failed to open temporary file for writing");
            }
            file << config;
            file.flush();
            if (file.fail()) {
                throw std::runtime_error("Failed to write to temporary file");
            }
        }

        if (std::filesystem::exists(m_configPath)) {
            std::filesystem::remove(m_configPath);
        }
        std::filesystem::rename(tempPath, m_configPath);
        spdlog::info("Saved application state to {}", m_configPath.string());

    } catch (const std::exception& e) {
        spdlog::error("Failed to save config file {}: {}", m_configPath.string(), e.what());

        if (std::filesystem::exists(backupPath)) {
            try {
                if (std::filesystem::exists(m_configPath)) {
                    std::filesystem::remove(m_configPath);
                }
                std::filesystem::copy_file(backupPath, m_configPath);
                spdlog::info("Restored config from backup after save failure");
            } catch (const std::exception& restoreError) {
                spdlog::error("Failed to restore backup: {}", restoreError.what());
            }
        }
    }
}

bool AppState::GetBool(StateParameter param) const {
    return std::get<bool>(m_values[static_cast<size_t>(param)]);
}

int AppState::GetInt(StateParameter param) const {
    return std::get<int>(m_values[static_cast<size_t>(param)]);
}

float AppState::GetFloat(StateParameter param) const {
    return std::get<float>(m_values[static_cast<size_t>(param)]);
}

const std::string& AppState::GetString(StateParameter param) const {
    return std::get<std::string>(m_values[static_cast<size_t>(param)]);
}

glm::vec3 AppState::GetVec3(StateParameter param) const {
    return std::get<glm::vec3>(m_values[static_cast<size_t>(param)]);
}

const std::vector<std::string>& AppState::GetStringVector(StateParameter param) const {
    return std::get<std::vector<std::string>>(m_values[static_cast<size_t>(param)]);
}

void AppState::SetBool(StateParameter param, bool value) {
    m_values[static_cast<size_t>(param)] = value;
}

void AppState::SetInt(StateParameter param, int value) {
    m_values[static_cast<size_t>(param)] = value;
}

void AppState::SetFloat(StateParameter param, float value) {
    m_values[static_cast<size_t>(param)] = value;
}

void AppState::SetString(StateParameter param, const std::string &value) {
    m_values[static_cast<size_t>(param)] = value;
}

void AppState::SetVec3(StateParameter param, const glm::vec3 &value) {
    m_values[static_cast<size_t>(param)] = value;
}

void AppState::SetStringVector(StateParameter param, const std::vector<std::string> &value) {
    m_values[static_cast<size_t>(param)] = value;
}

glm::vec3 AppState::GetCameraPosition() const {
    return glm::vec3(
        GetFloat(StateParameter::CameraPositionX),
        GetFloat(StateParameter::CameraPositionY),
        GetFloat(StateParameter::CameraPositionZ)
    );
}

glm::vec3 AppState::GetCameraFront() const {
    return glm::vec3(
        GetFloat(StateParameter::CameraFrontX),
        GetFloat(StateParameter::CameraFrontY),
        GetFloat(StateParameter::CameraFrontZ)
    );
}

glm::vec3 AppState::GetCameraUp() const {
    return glm::vec3(
        GetFloat(StateParameter::CameraUpX),
        GetFloat(StateParameter::CameraUpY),
        GetFloat(StateParameter::CameraUpZ)
    );
}

void AppState::SetCameraPosition(const glm::vec3 &position) {
    SetFloat(StateParameter::CameraPositionX, position.x);
    SetFloat(StateParameter::CameraPositionY, position.y);
    SetFloat(StateParameter::CameraPositionZ, position.z);
}

void AppState::SetCameraFront(const glm::vec3 &front) {
    SetFloat(StateParameter::CameraFrontX, front.x);
    SetFloat(StateParameter::CameraFrontY, front.y);
    SetFloat(StateParameter::CameraFrontZ, front.z);
}

void AppState::SetCameraUp(const glm::vec3 &up) {
    SetFloat(StateParameter::CameraUpX, up.x);
    SetFloat(StateParameter::CameraUpY, up.y);
    SetFloat(StateParameter::CameraUpZ, up.z);
}

void AppState::UpdateWindowState(int width, int height, int posX, int posY, bool maximized) {
    SetInt(StateParameter::WindowWidth, width);
    SetInt(StateParameter::WindowHeight, height);
    SetInt(StateParameter::WindowPosX, posX);
    SetInt(StateParameter::WindowPosY, posY);
    SetBool(StateParameter::WindowMaximized, maximized);
}

void AppState::UpdateCameraState(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up, float pitch, float yaw) {
    SetCameraPosition(position);
    SetCameraFront(front);
    SetCameraUp(up);
    SetFloat(StateParameter::CameraPitch, pitch);
    SetFloat(StateParameter::CameraYaw, yaw);
}

YAML::Node AppState::SerializeToYaml() const {
    YAML::Node config;

    config["window"]["width"] = GetInt(StateParameter::WindowWidth);
    config["window"]["height"] = GetInt(StateParameter::WindowHeight);
    config["window"]["posX"] = GetInt(StateParameter::WindowPosX);
    config["window"]["posY"] = GetInt(StateParameter::WindowPosY);
    config["window"]["maximized"] = GetBool(StateParameter::WindowMaximized);
    config["window"]["vsync"] = GetBool(StateParameter::WindowVSync);

    config["application"]["lastOpenScene"] = GetString(StateParameter::AppLastOpenScene);
    config["application"]["recentScenes"] = GetStringVector(StateParameter::AppRecentScenes);
    config["application"]["showDemoWindow"] = GetBool(StateParameter::AppShowDemoWindow);
    config["application"]["debugMode"] = GetInt(StateParameter::AppDebugMode);
    config["application"]["useKerrDistortion"] = GetBool(StateParameter::AppUseKerrDistortion);
    config["application"]["cameraSpeed"] = GetFloat(StateParameter::AppCameraSpeed);
    config["application"]["mouseSensitivity"] = GetFloat(StateParameter::AppMouseSensitivity);

    config["rendering"]["fov"] = GetFloat(StateParameter::RenderingFov);
    config["rendering"]["maxRaySteps"] = GetInt(StateParameter::RenderingMaxRaySteps);
    config["rendering"]["rayStepSize"] = GetFloat(StateParameter::RenderingRayStepSize);
    config["rendering"]["debugMode"] = GetInt(StateParameter::RenderingDebugMode);
    config["rendering"]["physicsDebugEnabled"] = GetBool(StateParameter::RenderingPhysicsDebugEnabled);
    config["rendering"]["physicsDebugDepthTest"] = GetBool(StateParameter::RenderingPhysicsDebugDepthTest);
    config["rendering"]["physicsDebugScale"] = GetFloat(StateParameter::RenderingPhysicsDebugScale);
    config["rendering"]["physicsDebugFlags"] = GetInt(StateParameter::RenderingPhysicsDebugFlags);

    config["camera"]["position"] = GetCameraPosition();
    config["camera"]["front"] = GetCameraFront();
    config["camera"]["up"] = GetCameraUp();
    config["camera"]["pitch"] = GetFloat(StateParameter::CameraPitch);
    config["camera"]["yaw"] = GetFloat(StateParameter::CameraYaw);

    return config;
}

void AppState::DeserializeFromYaml(const YAML::Node& config) {
    if (config["window"]) {
        const auto& windowNode = config["window"];
        if (windowNode["width"]) SetInt(StateParameter::WindowWidth, windowNode["width"].as<int>());
        if (windowNode["height"]) SetInt(StateParameter::WindowHeight, windowNode["height"].as<int>());
        if (windowNode["posX"]) SetInt(StateParameter::WindowPosX, windowNode["posX"].as<int>());
        if (windowNode["posY"]) SetInt(StateParameter::WindowPosY, windowNode["posY"].as<int>());
        if (windowNode["maximized"]) SetBool(StateParameter::WindowMaximized, windowNode["maximized"].as<bool>());
        if (windowNode["vsync"]) SetBool(StateParameter::WindowVSync, windowNode["vsync"].as<bool>());
    }

    if (config["application"]) {
        const auto& appNode = config["application"];
        if (appNode["lastOpenScene"]) SetString(StateParameter::AppLastOpenScene, appNode["lastOpenScene"].as<std::string>());
        if (appNode["recentScenes"]) SetStringVector(StateParameter::AppRecentScenes, appNode["recentScenes"].as<std::vector<std::string>>());
        if (appNode["showDemoWindow"]) SetBool(StateParameter::AppShowDemoWindow, appNode["showDemoWindow"].as<bool>());
        if (appNode["debugMode"]) SetInt(StateParameter::AppDebugMode, appNode["debugMode"].as<int>());
        if (appNode["useKerrDistortion"]) SetBool(StateParameter::AppUseKerrDistortion, appNode["useKerrDistortion"].as<bool>());
        if (appNode["cameraSpeed"]) SetFloat(StateParameter::AppCameraSpeed, appNode["cameraSpeed"].as<float>());
        if (appNode["mouseSensitivity"]) SetFloat(StateParameter::AppMouseSensitivity, appNode["mouseSensitivity"].as<float>());
    }

    if (config["rendering"]) {
        const auto& renderingNode = config["rendering"];
        if (renderingNode["fov"]) SetFloat(StateParameter::RenderingFov, renderingNode["fov"].as<float>());
        if (renderingNode["maxRaySteps"]) SetInt(StateParameter::RenderingMaxRaySteps, renderingNode["maxRaySteps"].as<int>());
        if (renderingNode["rayStepSize"]) SetFloat(StateParameter::RenderingRayStepSize, renderingNode["rayStepSize"].as<float>());
        if (renderingNode["debugMode"]) SetInt(StateParameter::RenderingDebugMode, renderingNode["debugMode"].as<int>());
        if (renderingNode["physicsDebugEnabled"]) SetBool(StateParameter::RenderingPhysicsDebugEnabled, renderingNode["physicsDebugEnabled"].as<bool>());
        if (renderingNode["physicsDebugDepthTest"]) SetBool(StateParameter::RenderingPhysicsDebugDepthTest, renderingNode["physicsDebugDepthTest"].as<bool>());
        if (renderingNode["physicsDebugScale"]) SetFloat(StateParameter::RenderingPhysicsDebugScale, renderingNode["physicsDebugScale"].as<float>());
        if (renderingNode["physicsDebugFlags"]) SetInt(StateParameter::RenderingPhysicsDebugFlags, renderingNode["physicsDebugFlags"].as<int>());
    }

    if (config["camera"]) {
        const auto& cameraNode = config["camera"];
        if (cameraNode["position"]) SetCameraPosition(cameraNode["position"].as<glm::vec3>());
        if (cameraNode["front"]) SetCameraFront(cameraNode["front"].as<glm::vec3>());
        if (cameraNode["up"]) SetCameraUp(cameraNode["up"].as<glm::vec3>());
        if (cameraNode["pitch"]) SetFloat(StateParameter::CameraPitch, cameraNode["pitch"].as<float>());
        if (cameraNode["yaw"]) SetFloat(StateParameter::CameraYaw, cameraNode["yaw"].as<float>());
    }
}

const ParameterMetadata& AppState::GetMetadata(StateParameter param) {
    static const std::array<ParameterMetadata, static_cast<size_t>(StateParameter::COUNT)> metadata = {{
        {"window.width", "Window Width", "Width of the application window", ParameterType::Int, ParameterGroup::Window, 640.0f, 7680.0f},
        {"window.height", "Window Height", "Height of the application window", ParameterType::Int, ParameterGroup::Window, 480.0f, 4320.0f},
        {"window.posX", "Window X", "X position of the application window", ParameterType::Int, ParameterGroup::Window},
        {"window.posY", "Window Y", "Y position of the application window", ParameterType::Int, ParameterGroup::Window},
        {"window.maximized", "Maximized", "Whether the window is maximized", ParameterType::Bool, ParameterGroup::Window},
        {"window.vsync", "VSync", "Enable vertical synchronization", ParameterType::Bool, ParameterGroup::Window},

        {"camera.position.x", "Camera Position X", "Camera X coordinate", ParameterType::Float, ParameterGroup::Camera},
        {"camera.position.y", "Camera Position Y", "Camera Y coordinate", ParameterType::Float, ParameterGroup::Camera},
        {"camera.position.z", "Camera Position Z", "Camera Z coordinate", ParameterType::Float, ParameterGroup::Camera},
        {"camera.front.x", "Camera Front X", "Camera front direction X", ParameterType::Float, ParameterGroup::Camera},
        {"camera.front.y", "Camera Front Y", "Camera front direction Y", ParameterType::Float, ParameterGroup::Camera},
        {"camera.front.z", "Camera Front Z", "Camera front direction Z", ParameterType::Float, ParameterGroup::Camera},
        {"camera.up.x", "Camera Up X", "Camera up direction X", ParameterType::Float, ParameterGroup::Camera},
        {"camera.up.y", "Camera Up Y", "Camera up direction Y", ParameterType::Float, ParameterGroup::Camera},
        {"camera.up.z", "Camera Up Z", "Camera up direction Z", ParameterType::Float, ParameterGroup::Camera},
        {"camera.pitch", "Camera Pitch", "Camera pitch angle", ParameterType::Float, ParameterGroup::Camera, -89.0f, 89.0f},
        {"camera.yaw", "Camera Yaw", "Camera yaw angle", ParameterType::Float, ParameterGroup::Camera},

        {"rendering.fov", "Field of View", "Camera field of view in degrees", ParameterType::Float, ParameterGroup::Rendering, 1.0f, 120.0f, 0.1f},
        {"rendering.maxRaySteps", "Max Ray Steps", "Maximum raytracing steps", ParameterType::Int, ParameterGroup::Rendering, 100.0f, 10000.0f},
        {"rendering.rayStepSize", "Ray Step Size", "Size of each raytracing step", ParameterType::Float, ParameterGroup::Rendering, 0.001f, 1.0f, 0.001f},
        {"rendering.debugMode", "Debug Mode", "Rendering debug visualization mode", ParameterType::Enum, ParameterGroup::Debug},
        {"rendering.physicsDebugEnabled", "Physics Debug", "Enable physics debug visualization", ParameterType::Bool, ParameterGroup::Debug},
        {"rendering.physicsDebugDepthTest", "Physics Debug Depth", "Enable depth testing for physics debug", ParameterType::Bool, ParameterGroup::Debug},
        {"rendering.physicsDebugScale", "Physics Debug Scale", "Scale of physics debug visualization", ParameterType::Float, ParameterGroup::Debug, 0.1f, 10.0f, 0.1f},
        {"rendering.physicsDebugFlags", "Physics Debug Flags", "Flags for physics debug rendering", ParameterType::Int, ParameterGroup::Debug},

        {"rendering.blackHolesEnabled", "Render Black Holes", "Enable black hole rendering", ParameterType::Bool, ParameterGroup::Rendering},
        {"rendering.gravitationalLensingEnabled", "Gravitational Lensing", "Enable gravitational lensing effect", ParameterType::Bool, ParameterGroup::Rendering},
        {"rendering.gravitationalRedshiftEnabled", "Gravitational Redshift", "Enable gravitational redshift effect", ParameterType::Bool, ParameterGroup::Rendering},
        {"rendering.accretionDiskEnabled", "Accretion Disk", "Enable accretion disk rendering", ParameterType::Bool, ParameterGroup::Rendering},
        {"rendering.accDiskHeight", "Disk Height", "Height of the accretion disk", ParameterType::Float, ParameterGroup::Rendering, 0.01f, 5.0f, 0.01f},
        {"rendering.accDiskNoiseScale", "Disk Noise Scale", "Scale of noise texture on disk", ParameterType::Float, ParameterGroup::Rendering, 0.1f, 10.0f, 0.1f},
        {"rendering.accDiskNoiseLOD", "Disk Noise LOD", "Level of detail for disk noise", ParameterType::Float, ParameterGroup::Rendering, 1.0f, 10.0f, 0.1f},
        {"rendering.accDiskSpeed", "Disk Rotation Speed", "Rotation speed of accretion disk", ParameterType::Float, ParameterGroup::Rendering, 0.0f, 5.0f, 0.1f},
        {"rendering.dopplerBeamingEnabled", "Doppler Beaming", "Enable relativistic Doppler beaming", ParameterType::Bool, ParameterGroup::Rendering},
        {"rendering.bloomEnabled", "Bloom Effect", "Enable bloom post-processing", ParameterType::Bool, ParameterGroup::Rendering},
        {"rendering.bloomThreshold", "Bloom Threshold", "Brightness threshold for bloom", ParameterType::Float, ParameterGroup::Rendering, 0.0f, 5.0f, 0.1f},
        {"rendering.bloomBlurPasses", "Bloom Blur Passes", "Number of blur passes for bloom", ParameterType::Int, ParameterGroup::Rendering, 1.0f, 10.0f},
        {"rendering.bloomIntensity", "Bloom Intensity", "Intensity of bloom effect", ParameterType::Float, ParameterGroup::Rendering, 0.0f, 5.0f, 0.1f},
        {"rendering.bloomDebug", "Bloom Debug", "Debug bloom rendering", ParameterType::Bool, ParameterGroup::Debug},

        {"app.lastOpenScene", "Last Scene", "Path to the last opened scene", ParameterType::String, ParameterGroup::Application},
        {"app.recentScenes", "Recent Scenes", "List of recently opened scenes", ParameterType::StringVector, ParameterGroup::Application},
        {"app.showDemoWindow", "Show Demo", "Show ImGui demo window", ParameterType::Bool, ParameterGroup::Application},
        {"app.debugMode", "Debug Mode", "Application debug mode", ParameterType::Int, ParameterGroup::Debug},
        {"app.useKerrDistortion", "Kerr Distortion", "Use Kerr metric for black hole distortion", ParameterType::Bool, ParameterGroup::Application},
        {"app.cameraSpeed", "Camera Speed", "Camera movement speed", ParameterType::Float, ParameterGroup::Camera, 0.1f, 100.0f, 0.1f},
        {"app.mouseSensitivity", "Mouse Sensitivity", "Mouse look sensitivity", ParameterType::Float, ParameterGroup::Camera, 0.01f, 1.0f, 0.01f},
    }};

    return metadata[static_cast<size_t>(param)];
}
