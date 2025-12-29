#include "ParameterRegistry.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <fstream>

ParameterRegistry &ParameterRegistry::Instance() noexcept {
    static ParameterRegistry s_Instance;
    return s_Instance;
}

ParameterType ParameterRegistry::ParseType(const std::string &typeStr) {
    if (typeStr == "bool") return ParameterType::Bool;
    if (typeStr == "int") return ParameterType::Int;
    if (typeStr == "float") return ParameterType::Float;
    if (typeStr == "string") return ParameterType::String;
    if (typeStr == "vec2") return ParameterType::Vec2;
    if (typeStr == "vec3") return ParameterType::Vec3;
    if (typeStr == "vec4") return ParameterType::Vec4;
    if (typeStr == "enum") return ParameterType::Enum;
    if (typeStr == "string_vector") return ParameterType::StringVector;
    throw std::runtime_error("Unknown parameter type: " + typeStr);
}

ParameterGroup ParameterRegistry::ParseGroup(const std::string &groupStr) {
    if (groupStr == "Window") return ParameterGroup::Window;
    if (groupStr == "Camera") return ParameterGroup::Camera;
    if (groupStr == "Rendering") return ParameterGroup::Rendering;
    if (groupStr == "Physics") return ParameterGroup::Physics;
    if (groupStr == "Debug") return ParameterGroup::Debug;
    if (groupStr == "Simulation") return ParameterGroup::Simulation;
    if (groupStr == "Application") return ParameterGroup::Application;
    if (groupStr == "Export") return ParameterGroup::Export;
    if (groupStr == "GeneralRelativity") return ParameterGroup::GeneralRelativity;
    throw std::runtime_error("Unknown parameter group: " + groupStr);
}

ParameterValue ParameterRegistry::ParseValueNode(const YAML::Node &node, ParameterType type) {
    switch (type) {
        case ParameterType::Bool: return node.as<bool>();
        case ParameterType::Int: return node.as<int>();
        case ParameterType::Float: return node.as<float>();
        case ParameterType::String: return node.as<std::string>();
        case ParameterType::Vec3:
            if (!node.IsSequence() || node.size() != 3) throw std::runtime_error("vec3 must be sequence of 3");
            return glm::vec3(node[0].as<float>(), node[1].as<float>(), node[2].as<float>());
        case ParameterType::StringVector: {
            std::vector<std::string> vec;
            for (const auto &n: node) vec.push_back(n.as<std::string>());
            return vec;
        }
        case ParameterType::Vec2:
        case ParameterType::Vec4:
        case ParameterType::Enum:
            throw std::runtime_error("Type not yet implemented in ParseValueNode");
    }
    throw std::runtime_error("Unhandled type in ParseValueNode");
}

void ParameterRegistry::LoadDefinitionsFromYaml(const std::filesystem::path &path) {
    std::lock_guard lock(m_Mutex);
    if (!std::filesystem::exists(path)) {
        spdlog::warn("Parameter definitions YAML not found: {}", path.string());
        return;
    }

    try {
        YAML::Node root = YAML::LoadFile(path.string());
        if (!root["parameters"]) {
            spdlog::warn("No 'parameters' section in {}", path.string());
            return;
        }

        for (const auto &entry: root["parameters"]) {
            auto name = entry["name"].as<std::string>();
            std::uint64_t id = RuntimeFnv1a(name);

            ParameterMetadata meta;
            meta.id = id;
            meta.name = name;
            meta.displayName = entry["displayName"] ? entry["displayName"].as<std::string>() : name;
            meta.tooltip = entry["tooltip"] ? entry["tooltip"].as<std::string>() : "";
            meta.type = ParseType(entry["type"].as<std::string>());
            meta.group = entry["group"] ? ParseGroup(entry["group"].as<std::string>()) : ParameterGroup::Application;

            if (entry["defaultValue"]) {
                meta.defaultValue = ParseValueNode(entry["defaultValue"], meta.type);
                m_Values[id] = meta.defaultValue;
            }

            if (entry["minValue"]) meta.minValue = entry["minValue"].as<float>();
            if (entry["maxValue"]) meta.maxValue = entry["maxValue"].as<float>();
            if (entry["dragSpeed"]) meta.dragSpeed = entry["dragSpeed"].as<float>();
            if (entry["showInUI"]) meta.showInUI = entry["showInUI"].as<bool>();
            if (entry["isReadOnly"]) meta.isReadOnly = entry["isReadOnly"].as<bool>();

            if (entry["scaleValueNames"]) {
                for (const auto &n: entry["scaleValueNames"]) {
                    meta.scaleValueNames.push_back(n.as<std::string>());
                }
            }
            if (entry["scaleValues"]) {
                for (const auto &n: entry["scaleValues"]) {
                    meta.scaleValues.push_back(n.as<float>());
                }
            }
            if (entry["enumValues"]) {
                for (const auto &n: entry["enumValues"]) {
                    meta.enumValues.push_back(n.as<std::string>());
                }
            }

            m_Meta[id] = meta;
        }

        spdlog::info("Loaded {} parameter definitions from {}", m_Meta.size(), path.string());
    } catch (const std::exception &e) {
        spdlog::error("Failed to load parameter definitions from {}: {}", path.string(), e.what());
    }
}

void ParameterRegistry::LoadValuesFromYaml(const std::filesystem::path &path) {
    std::lock_guard lock(m_Mutex);
    if (!std::filesystem::exists(path)) {
        spdlog::info("Parameter values file not found: {}, using defaults", path.string());
        return;
    }

    try {
        YAML::Node root = YAML::LoadFile(path.string());
        if (!root["parameters"]) {
            spdlog::warn("No 'parameters' section in {}", path.string());
            return;
        }

        int loadedCount = 0;
        for (const auto &kv : root["parameters"]) {
            auto name = kv.first.as<std::string>();
            std::uint64_t id = RuntimeFnv1a(name);

            auto metaIt = m_Meta.find(id);
            if (metaIt == m_Meta.end()) {
                spdlog::debug("Skipping unknown parameter: {}", name);
                continue;
            }

            try {
                m_Values[id] = ParseValueNode(kv.second, metaIt->second.type);
                loadedCount++;
            } catch (const std::exception &e) {
                spdlog::warn("Failed to parse value for parameter {}: {}", name, e.what());
            }
        }

        spdlog::info("Loaded {} parameter values from {}", loadedCount, path.string());
    } catch (const std::exception &e) {
        spdlog::error("Failed to load parameter values from {}: {}", path.string(), e.what());
    }
}

YAML::Node ParameterRegistry::ValueToYamlNode(const ParameterValue &value) {
    YAML::Node node;

    if (std::holds_alternative<bool>(value)) {
        node = std::get<bool>(value);
    } else if (std::holds_alternative<int>(value)) {
        node = std::get<int>(value);
    } else if (std::holds_alternative<float>(value)) {
        node = std::get<float>(value);
    } else if (std::holds_alternative<std::string>(value)) {
        node = std::get<std::string>(value);
    } else if (std::holds_alternative<glm::vec3>(value)) {
        const auto &v = std::get<glm::vec3>(value);
        node.push_back(v.x);
        node.push_back(v.y);
        node.push_back(v.z);
    } else if (std::holds_alternative<std::vector<std::string>>(value)) {
        const auto &vec = std::get<std::vector<std::string>>(value);
        for (const auto &s : vec) {
            node.push_back(s);
        }
    }

    return node;
}

void ParameterRegistry::SaveValuesToYaml(const std::filesystem::path &path) const {
    std::lock_guard lock(m_Mutex);

    try {
        YAML::Node root;
        YAML::Node params;

        for (const auto &[id, value] : m_Values) {
            auto metaIt = m_Meta.find(id);
            if (metaIt == m_Meta.end()) continue;

            params[metaIt->second.name] = ValueToYamlNode(value);
        }

        root["parameters"] = params;

        std::filesystem::path tempPath = path;
        tempPath += ".tmp";

        std::ofstream file(tempPath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing");
        }

        file << "# MoleHole Application Parameters\n";
        file << "# This file is auto-generated and stores runtime parameter values\n\n";
        file << root;
        file.flush();

        if (file.fail()) {
            throw std::runtime_error("Failed to write to file");
        }
        file.close();

        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }
        std::filesystem::rename(tempPath, path);

        spdlog::info("Saved {} parameter values to {}", m_Values.size(), path.string());
    } catch (const std::exception &e) {
        spdlog::error("Failed to save parameter values to {}: {}", path.string(), e.what());
    }
}

const std::unordered_map<uint64_t, ParameterMetadata>& ParameterRegistry::GetAllMetadata() const noexcept {
    return m_Meta;
}

void ParameterRegistry::RegisterParameter(const ParameterMetadata &meta) {
    std::lock_guard lock(m_Mutex);
    if (!m_Meta.contains(meta.id)) {
        m_Meta[meta.id] = meta;
        m_Values[meta.id] = meta.defaultValue;
    } else {
        spdlog::warn("Parameter id {} already registered, skipping", meta.id);
    }
}

bool ParameterRegistry::Has(const ParameterHandle &handle) const noexcept {
    std::lock_guard lock(m_Mutex);
    return m_Values.contains(handle.m_Id);
}

template<typename T>
void ParameterRegistry::Set(const ParameterHandle &handle, const T &value) {
    std::lock_guard lock(m_Mutex);
    m_Values[handle.m_Id] = value;
}

template void ParameterRegistry::Set<bool>(const ParameterHandle &, const bool &);

template void ParameterRegistry::Set<int>(const ParameterHandle &, const int &);

template void ParameterRegistry::Set<float>(const ParameterHandle &, const float &);

template void ParameterRegistry::Set<std::string>(const ParameterHandle &, const std::string &);

template void ParameterRegistry::Set<glm::vec3>(const ParameterHandle &, const glm::vec3 &);

template void ParameterRegistry::Set<std::vector<std::string> >(const ParameterHandle &,
                                                                const std::vector<std::string> &);

template<typename T>
T ParameterRegistry::Get(const ParameterHandle &handle, const T &fallback) const {
    std::lock_guard lock(m_Mutex);
    auto it = m_Values.find(handle.m_Id);
    if (it == m_Values.end()) return fallback;
    return std::get<T>(it->second);
}

template bool ParameterRegistry::Get<bool>(const ParameterHandle &, const bool &) const;

template int ParameterRegistry::Get<int>(const ParameterHandle &, const int &) const;

template float ParameterRegistry::Get<float>(const ParameterHandle &, const float &) const;

template std::string ParameterRegistry::Get<std::string>(const ParameterHandle &, const std::string &) const;

template glm::vec3 ParameterRegistry::Get<glm::vec3>(const ParameterHandle &, const glm::vec3 &) const;

template std::vector<std::string> ParameterRegistry::Get<std::vector<std::string> >(
    const ParameterHandle &, const std::vector<std::string> &) const;

std::optional<ParameterMetadata> ParameterRegistry::GetMetadata(const ParameterHandle &handle) const {
    std::lock_guard lock(m_Mutex);
    auto it = m_Meta.find(handle.m_Id);
    if (it == m_Meta.end()) return std::nullopt;
    return it->second;
}

namespace ParameterIds {
    constexpr std::uint64_t WindowWidth = ConstexprFnv1a("Window.Width");
    constexpr std::uint64_t WindowHeight = ConstexprFnv1a("Window.Height");
    constexpr std::uint64_t WindowPosX = ConstexprFnv1a("Window.PosX");
    constexpr std::uint64_t WindowPosY = ConstexprFnv1a("Window.PosY");
    constexpr std::uint64_t WindowMaximized = ConstexprFnv1a("Window.Maximized");
    constexpr std::uint64_t WindowVSync = ConstexprFnv1a("Window.VSync");
}
