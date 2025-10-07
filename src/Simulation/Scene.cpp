#include "Scene.h"

#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <nfd.h>
#include <spdlog/spdlog.h>
#include <limits>
#include <cmath>

#include "Application/Application.h"

void Scene::Serialize(const std::filesystem::path& path) {
    currentPath = path;
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << name;
    out << YAML::Key << "black_holes" << YAML::Value << YAML::BeginSeq;
    for (const auto& bh : blackHoles) {
        out << YAML::BeginMap;
        out << YAML::Key << "mass" << YAML::Value << bh.mass;
        out << YAML::Key << "position" << YAML::Value << YAML::Flow << YAML::BeginSeq << bh.position.x << bh.position.y << bh.position.z << YAML::EndSeq;
        out << YAML::Key << "show_accretion_disk" << YAML::Value << bh.showAccretionDisk;
        out << YAML::Key << "accretion_disk_density" << YAML::Value << bh.accretionDiskDensity;
        out << YAML::Key << "accretion_disk_size" << YAML::Value << bh.accretionDiskSize;
        out << YAML::Key << "accretion_disk_color" << YAML::Value << YAML::Flow << YAML::BeginSeq << bh.accretionDiskColor.r << bh.accretionDiskColor.g << bh.accretionDiskColor.b << YAML::EndSeq;
        out << YAML::Key << "spin" << YAML::Value << bh.spin;
        out << YAML::Key << "spin_axis" << YAML::Value << YAML::Flow << YAML::BeginSeq << bh.spinAxis.x << bh.spinAxis.y << bh.spinAxis.z << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    Application::Instance().GetUI().GetAnimationGraph()->Serialize(out);

    out << YAML::EndMap;
    std::ofstream fout(path);
    fout << out.c_str();
}

void Scene::Deserialize(const std::filesystem::path& path, bool setCurrentPath) {
    if (setCurrentPath) {
        currentPath = path;
    }
    blackHoles.clear();
    YAML::Node root = YAML::LoadFile(path.string());
    if (root["name"]) {
        name = root["name"].as<std::string>();
    } else {
        name = "";
    }
    if (!root["black_holes"]) return;
    for (const auto& node : root["black_holes"]) {
        BlackHole bh{};
        bh.mass = node["mass"].as<float>();
        auto pos = node["position"];
        bh.position = glm::vec3(pos[0].as<float>(), pos[1].as<float>(), pos[2].as<float>());
        bh.showAccretionDisk = node["show_accretion_disk"].as<bool>();
        bh.accretionDiskDensity = node["accretion_disk_density"].as<float>();
        bh.accretionDiskSize = node["accretion_disk_size"].as<float>();
        auto color = node["accretion_disk_color"];
        bh.accretionDiskColor = glm::vec3(color[0].as<float>(), color[1].as<float>(), color[2].as<float>());

        if (node["spin"]) {
            bh.spin = node["spin"].as<float>();
        } else {
            bh.spin = 0.0f;
        }

        if (node["spin_axis"]) {
            auto spinAxis = node["spin_axis"];
            bh.spinAxis = glm::vec3(spinAxis[0].as<float>(), spinAxis[1].as<float>(), spinAxis[2].as<float>());
        } else {
            bh.spinAxis = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        blackHoles.push_back(bh);
    }

    Application::Instance().GetUI().GetAnimationGraph()->Deserialize(root);
}

std::filesystem::path Scene::ShowFileDialog(bool save) {
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItem = { "YAML", "yaml" };
    nfdresult_t result;

    if (save) {
        result = NFD_SaveDialog(&outPath, &filterItem, 1, nullptr, nullptr);
    } else {
        result = NFD_OpenDialog(&outPath, &filterItem, 1, nullptr);
    }

    if (result == NFD_OKAY && outPath) {
        std::filesystem::path path(outPath);
        free(outPath);
        return path;
    }

    return {};
}

void Scene::SelectObject(ObjectType type, size_t index) {
    if (type == ObjectType::BlackHole && index < blackHoles.size()) {
        selectedObject = SelectedObject{type, index};
    } else {
        ClearSelection();
    }
}

void Scene::ClearSelection() {
    selectedObject.reset();
}

glm::vec3* Scene::GetSelectedObjectPosition() {
    if (!selectedObject.has_value()) return nullptr;

    switch (selectedObject->type) {
        case ObjectType::BlackHole:
            if (selectedObject->index < blackHoles.size()) {
                return &blackHoles[selectedObject->index].position;
            }
            break;
    }
    return nullptr;
}

std::string Scene::GetSelectedObjectName() const {
    if (!selectedObject.has_value()) return "";

    switch (selectedObject->type) {
        case ObjectType::BlackHole:
            if (selectedObject->index < blackHoles.size()) {
                return "Black Hole #" + std::to_string(selectedObject->index + 1);
            }
            break;
    }
    return "";
}

std::optional<Scene::SelectedObject> Scene::PickObject(const glm::vec3& rayOrigin, const glm::vec3& rayDirection) const {
    float closestDistance = std::numeric_limits<float>::max();
    std::optional<SelectedObject> closestObject;

    for (size_t i = 0; i < blackHoles.size(); ++i) {
        const auto& bh = blackHoles[i];

        float schwarzschildRadius = 2.0f * bh.mass;
        float pickingRadius = std::max(schwarzschildRadius, 1.0f);

        glm::vec3 oc = rayOrigin - bh.position;
        float a = glm::dot(rayDirection, rayDirection);
        float b = 2.0f * glm::dot(oc, rayDirection);
        float c = glm::dot(oc, oc) - pickingRadius * pickingRadius;
        float discriminant = b * b - 4 * a * c;

        if (discriminant >= 0) {
            float t1 = (-b - sqrt(discriminant)) / (2.0f * a);
            float t2 = (-b + sqrt(discriminant)) / (2.0f * a);
            float t = (t1 > 0) ? t1 : t2;

            if (t > 0 && t < closestDistance) {
                closestDistance = t;
                closestObject = SelectedObject{ObjectType::BlackHole, i};
            }
        }
    }

    return closestObject;
}
