#include "AppState.h"
#include <spdlog/spdlog.h>
#include <fstream>

void AppState::LoadFromFile(const std::filesystem::path& configPath) {
    m_configPath = configPath;

    std::filesystem::path backupPath = configPath;
    backupPath += ".backup";

    bool loadedFromBackup = false;

    if (std::filesystem::exists(configPath)) {
        try {
            std::ifstream file(configPath);
            if (file.is_open() && file.peek() != std::ifstream::traits_type::eof()) {
                YAML::Node config = YAML::LoadFile(configPath.string());
                if (config && !config.IsNull()) {
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
                YAML::Node config = YAML::LoadFile(backupPath.string());
                if (config && !config.IsNull()) {
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

bool AppState::HasProperty(const std::string& key) const {
    return customProperties.find(key) != customProperties.end();
}

void AppState::RemoveProperty(const std::string& key) {
    customProperties.erase(key);
}

void AppState::SetLastOpenScene(const std::string& scenePath) {
    app.lastOpenScene = scenePath;
}

std::string AppState::GetLastOpenScene() const {
    return app.lastOpenScene;
}

void AppState::UpdateWindowState(int width, int height, int posX, int posY, bool maximized) {
    window.width = width;
    window.height = height;
    window.posX = posX;
    window.posY = posY;
    window.maximized = maximized;
}

void AppState::UpdateCameraState(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up, float pitch, float yaw) {
    camera.position = position;
    camera.front = front;
    camera.up = up;
    camera.pitch = pitch;
    camera.yaw = yaw;
}

YAML::Node AppState::SerializeToYaml() const {
    YAML::Node config;

    config["window"]["width"] = window.width;
    config["window"]["height"] = window.height;
    config["window"]["posX"] = window.posX;
    config["window"]["posY"] = window.posY;
    config["window"]["maximized"] = window.maximized;
    config["window"]["vsync"] = window.vsync;

    config["application"]["lastOpenScene"] = app.lastOpenScene;
    config["application"]["recentScenes"] = app.recentScenes;
    config["application"]["showDemoWindow"] = app.showDemoWindow;
    config["application"]["debugMode"] = app.debugMode;
    config["application"]["useKerrDistortion"] = app.useKerrDistortion;
    config["application"]["cameraSpeed"] = app.cameraSpeed;
    config["application"]["mouseSensitivity"] = app.mouseSensitivity;

    config["rendering"]["fov"] = rendering.fov;
    config["rendering"]["maxRaySteps"] = rendering.maxRaySteps;
    config["rendering"]["rayStepSize"] = rendering.rayStepSize;
    config["rendering"]["enableDistortion"] = rendering.enableDistortion;
    config["rendering"]["debugMode"] = static_cast<int>(rendering.debugMode);
    config["rendering"]["kerrLutResolution"] = rendering.kerrLutResolution;
    config["rendering"]["kerrMaxDistance"] = rendering.kerrMaxDistance;

    config["camera"]["position"] = camera.position;
    config["camera"]["front"] = camera.front;
    config["camera"]["up"] = camera.up;
    config["camera"]["pitch"] = camera.pitch;
    config["camera"]["yaw"] = camera.yaw;

    for (const auto& [key, value] : customProperties) {
        config["custom"][key] = StateValueToYamlNode(value);
    }

    return config;
}

void AppState::DeserializeFromYaml(const YAML::Node& config) {
    // Window state
    if (config["window"]) {
        const auto& windowNode = config["window"];
        if (windowNode["width"]) window.width = windowNode["width"].as<int>();
        if (windowNode["height"]) window.height = windowNode["height"].as<int>();
        if (windowNode["posX"]) window.posX = windowNode["posX"].as<int>();
        if (windowNode["posY"]) window.posY = windowNode["posY"].as<int>();
        if (windowNode["maximized"]) window.maximized = windowNode["maximized"].as<bool>();
        if (windowNode["vsync"]) window.vsync = windowNode["vsync"].as<bool>();
    }

    if (config["application"]) {
        const auto& appNode = config["application"];
        if (appNode["lastOpenScene"]) app.lastOpenScene = appNode["lastOpenScene"].as<std::string>();
        if (appNode["recentScenes"]) app.recentScenes = appNode["recentScenes"].as<std::vector<std::string>>();
        if (appNode["showDemoWindow"]) app.showDemoWindow = appNode["showDemoWindow"].as<bool>();
        if (appNode["debugMode"]) app.debugMode = appNode["debugMode"].as<int>();
        if (appNode["useKerrDistortion"]) app.useKerrDistortion = appNode["useKerrDistortion"].as<bool>();
        if (appNode["cameraSpeed"]) app.cameraSpeed = appNode["cameraSpeed"].as<float>();
        if (appNode["mouseSensitivity"]) app.mouseSensitivity = appNode["mouseSensitivity"].as<float>();
    }

    if (config["rendering"]) {
        const auto& renderingNode = config["rendering"];
        if (renderingNode["fov"]) rendering.fov = renderingNode["fov"].as<float>();
        if (renderingNode["maxRaySteps"]) rendering.maxRaySteps = renderingNode["maxRaySteps"].as<int>();
        if (renderingNode["rayStepSize"]) rendering.rayStepSize = renderingNode["rayStepSize"].as<float>();
        if (renderingNode["enableDistortion"]) rendering.enableDistortion = renderingNode["enableDistortion"].as<bool>();
        if (renderingNode["debugMode"]) rendering.debugMode = static_cast<DebugMode>(renderingNode["debugMode"].as<int>());
        if (renderingNode["kerrLutResolution"]) rendering.kerrLutResolution = renderingNode["kerrLutResolution"].as<int>();
        if (renderingNode["kerrMaxDistance"]) rendering.kerrMaxDistance = renderingNode["kerrMaxDistance"].as<float>();
    }

    if (config["camera"]) {
        const auto& cameraNode = config["camera"];
        if (cameraNode["position"]) camera.position = cameraNode["position"].as<glm::vec3>();
        if (cameraNode["front"]) camera.front = cameraNode["front"].as<glm::vec3>();
        if (cameraNode["up"]) camera.up = cameraNode["up"].as<glm::vec3>();
        if (cameraNode["pitch"]) camera.pitch = cameraNode["pitch"].as<float>();
        if (cameraNode["yaw"]) camera.yaw = cameraNode["yaw"].as<float>();
    }

    if (config["custom"]) {
        const auto& customNode = config["custom"];
        customProperties.clear();
        for (const auto& pair : customNode) {
            std::string key = pair.first.as<std::string>();
            customProperties[key] = YamlNodeToStateValue(pair.second);
        }
    }
}

AppState::StateValue AppState::YamlNodeToStateValue(const YAML::Node& node) const {
    try {
        if (node.IsSequence()) {
            if (node.size() == 2) {
                return node.as<glm::vec2>();
            } else if (node.size() == 3) {
                return node.as<glm::vec3>();
            }
        }

        try { return node.as<bool>(); } catch (...) {}
        try { return node.as<int>(); } catch (...) {}
        try { return node.as<float>(); } catch (...) {}
        return node.as<std::string>();
    } catch (...) {
        return std::string("");
    }
}

YAML::Node AppState::StateValueToYamlNode(const StateValue& value) const {
    return std::visit([](const auto& v) -> YAML::Node {
        YAML::Node node;
        node = v;
        return node;
    }, value);
}
