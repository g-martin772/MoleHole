#pragma once
#include <vector>
#include <filesystem>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <optional>
#include <unordered_map>
#include "Application/AnimationGraph.h"
#include "Application/ParameterRegistry.h"

class Camera;

struct ObjectClass {
    std::string name;
    std::vector<std::string> availableParameterKeys;
    std::vector<std::string> requiredParameterKeys;
    std::unordered_map<uint64_t, ParameterMetadata> meta;
};

struct SceneObjectDefinition {
    std::string name;
    std::vector<ObjectClass*> objectClasses;
    std::unordered_map<uint64_t, ParameterMetadata> meta;
};

class SceneObject {
public:
    SceneObject();
    explicit SceneObject(const std::vector<std::string>& classNames);
    virtual ~SceneObject() = default;

    void AddClass(const std::string& className);
    bool HasClass(const std::string& className) const;
    const std::vector<ObjectClass*>& GetClasses() const { return m_Classes; }

    bool HasParameter(const ParameterHandle& handle) const;
    ParameterValue GetParameter(const ParameterHandle& handle) const;
    void SetParameter(const ParameterHandle& handle, const ParameterValue& value);

    const std::unordered_map<uint64_t, ParameterValue>& GetAllParameters() const { return m_Parameters; }
    const std::unordered_map<uint64_t, ParameterMetadata>& GetAllMetadata() const { return m_AggregatedMeta; }

    void SerializeToYAML(YAML::Emitter& out) const;
    void DeserializeFromYAML(const YAML::Node& node);

private:
    std::vector<ObjectClass*> m_Classes;
    std::unordered_map<uint64_t, ParameterValue> m_Parameters;
    std::unordered_map<uint64_t, ParameterMetadata> m_AggregatedMeta;

    void RebuildMetadata();
};

enum class ObjectType { BlackHole, Mesh, Sphere, DynamicObject };

struct Scene {
    std::string name;
    std::vector<SceneObject> objects;

    std::filesystem::path currentPath;
    Camera* camera = nullptr;

    bool reloadSkybox = false;

    struct SelectedObject {
        ObjectType type;
        size_t index;

        bool operator==(const SelectedObject& other) const {
            return type == other.type && index == other.index;
        }
    };
    std::optional<SelectedObject> selectedObject;

    void Serialize(const std::filesystem::path& path);
    void Deserialize(const std::filesystem::path& path, bool setCurrentPath = true);
    static std::filesystem::path ShowFileDialog(bool save);

    void SelectObject(ObjectType type, size_t index);
    void ClearSelection();
    bool HasSelection() const { return selectedObject.has_value(); }
    glm::vec3* GetSelectedObjectPosition();
    glm::quat* GetSelectedObjectRotation();
    glm::vec3* GetSelectedObjectScale();
    std::string GetSelectedObjectName() const;

    std::optional<SelectedObject> PickObject(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
};
