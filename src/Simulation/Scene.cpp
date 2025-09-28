#include "Scene.h"

#include <filesystem>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <nfd.h>
#include <spdlog/spdlog.h>

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
    spdlog::info("Opening {} dialog", save ? "save" : "open");

    nfdu8char_t *outPath = nullptr;
    nfdresult_t result;

    nfdresult_t initResult = NFD_Init();
    if (initResult != NFD_OKAY) {
        spdlog::error("Failed to initialize NFD: {}", NFD_GetError());
        return {};
    }

    nfdu8filteritem_t filterItem[2] = {
        { "YAML Scene Files", "yaml" },
        { "All Files", "*" }
    };

    if (save) {
        result = NFD_SaveDialogU8(&outPath, filterItem, 2, nullptr, "scene.yaml");
    } else {
        spdlog::info("Calling NFD_OpenDialogU8");
        result = NFD_OpenDialogU8(&outPath, filterItem, 1, nullptr);
    }

    spdlog::info("Dialog result: {} (NFD_OKAY={}, NFD_CANCEL={}, NFD_ERROR={})",
                 (int)result, (int)NFD_OKAY, (int)NFD_CANCEL, (int)NFD_ERROR);

    std::filesystem::path path;
    if (result == NFD_OKAY) {
        if (outPath) {
            path = std::string(outPath);
            spdlog::info("Dialog succeeded, path: '{}'", path.string());
            NFD_FreePathU8(outPath);

            if (save && path.extension().empty()) {
                path += ".yaml";
                spdlog::info("Added .yaml extension, final path: '{}'", path.string());
            }
        } else {
            spdlog::error("Dialog returned NFD_OKAY but outPath is null");
        }
    } else if (result == NFD_CANCEL) {
        spdlog::info("Dialog was cancelled by user");
    } else if (result == NFD_ERROR) {
        const char* error = NFD_GetError();
        spdlog::error("File dialog error: {}", error ? error : "Unknown error");
    }

    NFD_Quit();
    return path;
}
