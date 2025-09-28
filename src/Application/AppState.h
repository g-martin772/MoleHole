#pragma once

#include <string>
#include <filesystem>
#include <unordered_map>
#include <variant>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <yaml-cpp/yaml.h>

enum class DebugMode {
    Normal = 0,
    InfluenceZones = 1,
    DeflectionMagnitude = 2,
    GravitationalField = 3,
    SphericalShape = 4,
    DebugLUT = 5
};

class AppState {
public:
    using StateValue = std::variant<bool, int, float, std::string, glm::vec2, glm::vec3>;

    struct WindowState {
        int width = 800;
        int height = 600;
        int posX = -1; // -1 means centered
        int posY = -1; // -1 means centered
        bool maximized = false;
        bool vsync = true;
    } window;

    struct ApplicationState {
        std::string lastOpenScene = "";
        bool showDemoWindow = false;
        int debugMode = 0;
        bool useKerrDistortion = true;
        float cameraSpeed = 5.0f;
        float mouseSensitivity = 0.1f;
    } app;

    struct RenderingState {
        float fov = 45.0f;
        int maxRaySteps = 1000;
        float rayStepSize = 0.01f;
        bool enableDistortion = true;
        DebugMode debugMode = DebugMode::Normal;
        int kerrLutResolution = 64;
        float kerrMaxDistance = 100.0f;
    } rendering;

    struct CameraState {
        glm::vec3 position = glm::vec3(0.0f, 0.0f, 5.0f);
        glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        float pitch = 0.0f;
        float yaw = -90.0f;
    } camera;

    std::unordered_map<std::string, StateValue> customProperties;

    void LoadFromFile(const std::filesystem::path &configPath = "config.yaml");

    void SaveToFile(const std::filesystem::path &configPath = "config.yaml");

    template<typename T>
    void SetProperty(const std::string &key, const T &value);

    template<typename T>
    T GetProperty(const std::string &key, const T &defaultValue) const;

    bool HasProperty(const std::string &key) const;

    void RemoveProperty(const std::string &key);

    void SetLastOpenScene(const std::string &scenePath);

    std::string GetLastOpenScene() const;

    void UpdateWindowState(int width, int height, int posX, int posY, bool maximized);

    void UpdateCameraState(const glm::vec3 &position, const glm::vec3 &front, const glm::vec3 &up, float pitch,
                           float yaw);


    std::filesystem::path m_configPath;

    YAML::Node SerializeToYaml() const;

    void DeserializeFromYaml(const YAML::Node &node);

    StateValue YamlNodeToStateValue(const YAML::Node &node) const;

    YAML::Node StateValueToYamlNode(const StateValue &value) const;
};

template<typename T>
void AppState::SetProperty(const std::string &key, const T &value) {
    customProperties[key] = value;
}

template<typename T>
T AppState::GetProperty(const std::string &key, const T &defaultValue) const {
    auto it = customProperties.find(key);
    if (it != customProperties.end()) {
        try {
            return std::get<T>(it->second);
        } catch (const std::bad_variant_access &) {
            return defaultValue;
        }
    }
    return defaultValue;
}

namespace YAML {
    template<>
    struct convert<glm::vec2> {
        static Node encode(const glm::vec2 &rhs) {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            return node;
        }

        static bool decode(const Node &node, glm::vec2 &rhs) {
            if (!node.IsSequence() || node.size() != 2) {
                return false;
            }
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            return true;
        }
    };

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
