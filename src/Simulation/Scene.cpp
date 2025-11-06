#include "Scene.h"

#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <nfd.h>
#include <spdlog/spdlog.h>
#include <limits>
#include <cmath>

#include "Application/Application.h"

void Scene::Serialize(const std::filesystem::path &path) {
    currentPath = path;
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << name;
    out << YAML::Key << "black_holes" << YAML::Value << YAML::BeginSeq;
    for (const auto &bh: blackHoles) {
        out << YAML::BeginMap;
        out << YAML::Key << "mass" << YAML::Value << bh.mass;
        out << YAML::Key << "position" << YAML::Value << YAML::Flow << YAML::BeginSeq << bh.position.x << bh.position.y
                << bh.position.z << YAML::EndSeq;
        out << YAML::Key << "show_accretion_disk" << YAML::Value << bh.showAccretionDisk;
        out << YAML::Key << "accretion_disk_density" << YAML::Value << bh.accretionDiskDensity;
        out << YAML::Key << "accretion_disk_size" << YAML::Value << bh.accretionDiskSize;
        out << YAML::Key << "accretion_disk_color" << YAML::Value << YAML::Flow << YAML::BeginSeq << bh.
                accretionDiskColor.r << bh.accretionDiskColor.g << bh.accretionDiskColor.b << YAML::EndSeq;
        out << YAML::Key << "spin" << YAML::Value << bh.spin;
        out << YAML::Key << "spin_axis" << YAML::Value << YAML::Flow << YAML::BeginSeq << bh.spinAxis.x << bh.spinAxis.y
                << bh.spinAxis.z << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "meshes" << YAML::Value << YAML::BeginSeq;
    for (const auto &mesh: meshes) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << mesh.name;
        out << YAML::Key << "path" << YAML::Value << mesh.path;
        out << YAML::Key << "mass" << YAML::Value << mesh.massKg;
        out << YAML::Key << "position" << YAML::Value << YAML::Flow << YAML::BeginSeq << mesh.position.x << mesh.
                position.y << mesh.position.z << YAML::EndSeq;
        out << YAML::Key << "velocity" << YAML::Value << YAML::Flow << YAML::BeginSeq << mesh.velocity.x << mesh.
                velocity.y << mesh.velocity.z << YAML::EndSeq;
        out << YAML::Key << "com_offset" << YAML::Value << YAML::Flow << YAML::BeginSeq << mesh.comOffset.x << mesh.
                comOffset.y << mesh.comOffset.z << YAML::EndSeq;
        out << YAML::Key << "rotation" << YAML::Value << YAML::Flow << YAML::BeginSeq << mesh.rotation.w << mesh.
                rotation.x << mesh.rotation.y << mesh.rotation.z << YAML::EndSeq;
        out << YAML::Key << "scale" << YAML::Value << YAML::Flow << YAML::BeginSeq << mesh.scale.x << mesh.scale.y <<
                mesh.scale.z << YAML::EndSeq;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    out << YAML::Key << "spheres" << YAML::Value << YAML::BeginSeq;
    for (const auto &sphere: spheres) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << sphere.name;
        out << YAML::Key << "mass" << YAML::Value << sphere.massKg;
        out << YAML::Key << "texture_path" << YAML::Value << sphere.texturePath;
        out << YAML::Key << "velocity" << YAML::Value << YAML::Flow << YAML::BeginSeq << sphere.velocity.x << sphere.
                velocity.y << sphere.velocity.z << YAML::EndSeq;
        out << YAML::Key << "position" << YAML::Value << YAML::Flow << YAML::BeginSeq << sphere.position.x << sphere.
                position.y << sphere.position.z << YAML::EndSeq;
        out << YAML::Key << "rotation" << YAML::Value << YAML::Flow << YAML::BeginSeq << sphere.rotation.w << sphere.
                rotation.x << sphere.rotation.y << sphere.rotation.z << YAML::EndSeq;
        out << YAML::Key << "color" << YAML::Value << YAML::Flow << YAML::BeginSeq << sphere.color.r << sphere.color.g
                << sphere.color.b << sphere.color.a << YAML::EndSeq;
        out << YAML::Key << "spin" << YAML::Value << sphere.spin;
        out << YAML::Key << "radius" << YAML::Value << sphere.radius;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    Application::Instance().GetUI().GetAnimationGraph()->Serialize(out);

    out << YAML::EndMap;
    std::ofstream fout(path);
    fout << out.c_str();
}

void Scene::Deserialize(const std::filesystem::path &path, bool setCurrentPath) {
    if (setCurrentPath) {
        currentPath = path;
    }
    blackHoles.clear();
    meshes.clear();
    YAML::Node root = YAML::LoadFile(path.string());
    if (root["name"]) {
        name = root["name"].as<std::string>();
    } else {
        name = "";
    }
    if (root["black_holes"]) {
        for (const auto &node: root["black_holes"]) {
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
    }

    if (root["meshes"]) {
        for (const auto &node: root["meshes"]) {
            MeshObject mesh;
            mesh.name = node["name"].as<std::string>();
            mesh.path = node["path"].as<std::string>();
            mesh.massKg = node["mass"].as<float>();
            auto pos = node["position"];
            mesh.position = glm::vec3(pos[0].as<float>(), pos[1].as<float>(), pos[2].as<float>());
            auto vel = node["velocity"];
            mesh.velocity = glm::vec3(vel[0].as<float>(), vel[1].as<float>(), vel[2].as<float>());
            auto comOffset = node["com_offset"];
            mesh.comOffset = glm::vec3(comOffset[0].as<float>(), comOffset[1].as<float>(), comOffset[2].as<float>());
            auto rot = node["rotation"];
            mesh.rotation = glm::quat(rot[0].as<float>(), rot[1].as<float>(), rot[2].as<float>(), rot[3].as<float>());
            auto scale = node["scale"];
            mesh.scale = glm::vec3(scale[0].as<float>(), scale[1].as<float>(), scale[2].as<float>());
            meshes.push_back(mesh);
        }
    }

    if (root["spheres"]) {
        for (const auto &node: root["spheres"]) {
            Sphere sphere;
            sphere.name = node["name"].as<std::string>();
            sphere.texturePath = node["texture_path"].as<std::string>();
            sphere.massKg = node["mass"].as<float>();
            auto pos = node["position"];
            sphere.position = glm::vec3(pos[0].as<float>(), pos[1].as<float>(), pos[2].as<float>());
            auto vel = node["velocity"];
            sphere.velocity = glm::vec3(vel[0].as<float>(), vel[1].as<float>(), vel[2].as<float>());
            auto rot = node["rotation"];
            sphere.rotation = glm::quat(rot[0].as<float>(), rot[1].as<float>(), rot[2].as<float>(), rot[3].as<float>());
            auto color = node["color"];
            sphere.color = glm::vec4(color[0].as<float>(), color[1].as<float>(), color[2].as<float>(),
                                     color[3].as<float>());
            sphere.spin = node["spin"].as<float>();
            sphere.radius = node["radius"].as<float>();
            spheres.push_back(sphere);
        }
    }

    Application::Instance().GetUI().GetAnimationGraph()->Deserialize(root);
}

std::filesystem::path Scene::ShowFileDialog(bool save) {
    nfdchar_t *outPath = nullptr;
    nfdfilteritem_t filterItem = {"YAML", "yaml"};
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
    } else if (type == ObjectType::Mesh && index < meshes.size()) {
        selectedObject = SelectedObject{type, index};
    } else if (type == ObjectType::Sphere && index < spheres.size()) {
        selectedObject = SelectedObject{type, index};
    } else {
        ClearSelection();
    }
}

void Scene::ClearSelection() {
    selectedObject.reset();
}

glm::vec3 *Scene::GetSelectedObjectPosition() {
    if (!selectedObject.has_value()) return nullptr;

    switch (selectedObject->type) {
        case ObjectType::BlackHole:
            if (selectedObject->index < blackHoles.size()) {
                return &blackHoles[selectedObject->index].position;
            }
            break;
        case ObjectType::Mesh:
            if (selectedObject->index < meshes.size()) {
                return &meshes[selectedObject->index].position;
            }
            break;
        case ObjectType::Sphere:
            if (selectedObject->index < spheres.size()) {
                return &spheres[selectedObject->index].position;
            }
    }
    return nullptr;
}

glm::quat *Scene::GetSelectedObjectRotation() {
    if (!selectedObject.has_value()) return nullptr;

    switch (selectedObject->type) {
        case ObjectType::Mesh:
            if (selectedObject->index < meshes.size()) {
                return &meshes[selectedObject->index].rotation;
            }
            break;
        case ObjectType::Sphere:
            if (selectedObject->index < spheres.size()) {
                return &spheres[selectedObject->index].rotation;
            }
        default:
            break;
    }
    return nullptr;
}

glm::vec3 *Scene::GetSelectedObjectScale() {
    if (!selectedObject.has_value()) return nullptr;

    switch (selectedObject->type) {
        case ObjectType::Mesh:
            if (selectedObject->index < meshes.size()) {
                return &meshes[selectedObject->index].scale;
            }
            break;
        default:
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
        case ObjectType::Mesh:
            if (selectedObject->index < meshes.size()) {
                return meshes[selectedObject->index].name;
            }
            break;
        case ObjectType::Sphere:
            if (selectedObject->index < spheres.size()) {
                return spheres[selectedObject->index].name;
            }
            break;
    }
    return "";
}

std::optional<Scene::SelectedObject>
Scene::PickObject(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection) const {
    float closestDistance = std::numeric_limits<float>::max();
    std::optional<SelectedObject> closestObject;

    for (size_t i = 0; i < blackHoles.size(); ++i) {
        const auto &bh = blackHoles[i];

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

    for (size_t i = 0; i < meshes.size(); ++i) {
    }

    for (size_t i = 0; i < spheres.size(); ++i) {
        const auto &sphere = spheres[i];
        float pickingRadius = sphere.radius;
        glm::vec3 oc = rayOrigin - sphere.position;
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
                closestObject = SelectedObject{ObjectType::Sphere, i};
            }
        }
    }

    return closestObject;
}
