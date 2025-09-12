#pragma once
#include <vector>
#include <filesystem>
#include <glm/vec3.hpp>
#include <string>

struct BlackHole {
    float mass;
    glm::vec3 position;
    bool showAccretionDisk;
    float accretionDiskDensity;
    float accretionDiskSize;
    glm::vec3 accretionDiskColor;
};

struct Scene {
    std::string name;
    std::vector<BlackHole> blackHoles;
    std::filesystem::path currentPath;
    void Serialize(const std::filesystem::path& path);
    void Deserialize(const std::filesystem::path& path);
    static std::filesystem::path ShowFileDialog(bool save);
};
