#include "Scene.h"

#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <nfd.h>

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
    out << YAML::EndMap;
    std::ofstream fout(path);
    fout << out.c_str();
}

void Scene::Deserialize(const std::filesystem::path& path) {
    currentPath = path;
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
}

std::filesystem::path Scene::ShowFileDialog(bool save) {
    NFD_Init();
    nfdu8char_t *outPath = nullptr;
    nfdresult_t result;
    if (save) {
        result = NFD_SaveDialogU8(&outPath, nullptr, 0, ".", "scene.yaml");
    } else {
        result = NFD_OpenDialogU8(&outPath, nullptr, 0, nullptr);
    }
    std::filesystem::path path;
    if (result == NFD_OKAY && outPath) {
        path = outPath;
        NFD_FreePathU8(outPath);
    }
    NFD_Quit();
    return path;
}
