#pragma once
#include <vector>
#include <filesystem>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <string>
#include <optional>
#include "Application/AnimationGraph.h"

class Camera;

struct BlackHole {
    float mass;
    glm::vec3 position;
    bool showAccretionDisk;
    float accretionDiskDensity;
    float accretionDiskSize;
    glm::vec3 accretionDiskColor;

    float spin = 0.0f;  // 0.0 to 1.0
    glm::vec3 spinAxis = glm::vec3(0.0f, 1.0f, 0.0f);

    bool operator==(const BlackHole& other) const {
        return mass == other.mass &&
               position == other.position &&
               showAccretionDisk == other.showAccretionDisk &&
               accretionDiskDensity == other.accretionDiskDensity &&
               accretionDiskSize == other.accretionDiskSize &&
               accretionDiskColor == other.accretionDiskColor &&
               spin == other.spin &&
               spinAxis == other.spinAxis;
    }
};

struct BlackHoleHash {
    std::size_t operator()(const BlackHole& bh) const {
        std::size_t h1 = std::hash<float>{}(bh.position.x);
        std::size_t h2 = std::hash<float>{}(bh.position.y);
        std::size_t h3 = std::hash<float>{}(bh.position.z);
        std::size_t h4 = std::hash<float>{}(bh.mass);
        std::size_t h5 = std::hash<float>{}(bh.spin);
        std::size_t h6 = std::hash<float>{}(bh.spinAxis.x);
        std::size_t h7 = std::hash<float>{}(bh.spinAxis.y);
        std::size_t h8 = std::hash<float>{}(bh.spinAxis.z);

        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^
               (h5 << 4) ^ (h6 << 5) ^ (h7 << 6) ^ (h8 << 7);
    }
};

struct Scene {
    std::string name;
    std::vector<BlackHole> blackHoles;
    std::filesystem::path currentPath;
    Camera* camera = nullptr;

    enum class ObjectType { BlackHole };
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
    std::string GetSelectedObjectName() const;

    std::optional<SelectedObject> PickObject(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const;
};
