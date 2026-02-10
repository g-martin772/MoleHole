#pragma once
#include <vector>
#include <filesystem>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <optional>
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

struct SceneObject {
    SceneObject();
    virtual ~SceneObject() = default;

    bool HasField(const ParameterHandle& handle) const;
    ParameterValue GetField(const ParameterHandle& handle) const;
    void SetField(const ParameterHandle& handle, const ParameterValue& value);
protected:
    SceneObjectDefinition* definition;
    std::vector<ParameterValue> values;
};



struct BlackHole {
    float mass;
    glm::vec3 position;
    glm::vec3 velocity;

    float spin = 0.0f;  // 0.0 to 1.0
    glm::vec3 spinAxis = glm::vec3(0.0f, 1.0f, 0.0f);

    bool operator==(const BlackHole& other) const {
        return mass == other.mass &&
               position == other.position &&
               velocity == other.velocity &&
               spin == other.spin &&
               spinAxis == other.spinAxis;
    }
};

struct MeshObject {
    float massKg;
    std::string name;
    std::string path;
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 comOffset;
    glm::quat rotation;
    glm::vec3 scale;

    MeshObject() : massKg(1000.0f), position(0.0f), velocity(0.0f), comOffset(0.0f), rotation(1.0f, 0.0f, 0.0f, 0.0f), scale(1.0f) {}
};

struct Sphere {
    float massKg;
    std::string name;
    std::string texturePath;
    glm::vec3 velocity;
    glm::vec3 position;
    glm::quat rotation;
    glm::vec4 color;
    float spin, radius;
    Sphere() : massKg(1000.0f), position(0.0f), velocity(0.0f), rotation(1.0f, 0.0f, 0.0f, 0.0f), color(0.5f, 0.5f, 0.5f, 1.0f), spin(0.0f), radius(1.0f) {}
};



enum class ObjectType { BlackHole, Mesh, Sphere };



struct Scene {
    std::string name;
    std::vector<BlackHole> blackHoles;
    std::vector<MeshObject> meshes;
    std::vector<Sphere> spheres;
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
