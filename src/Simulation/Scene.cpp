#include "Scene.h"

#include <nfd.h>

#include "Application/Application.h"
#include "Simulation.h"

SceneObject::SceneObject() = default;

SceneObject::SceneObject(const std::vector<std::string>& classNames) {
    for (const auto& className : classNames) {
        AddClass(className);
    }
}

void SceneObject::AddClass(const std::string& className) {
    auto& simulation = Application::Instance().GetSimulation();
    const auto& objectClasses = simulation.GetObjectClasses();

    for (auto& objClass : objectClasses) {
        if (objClass.name == className) {
            m_Classes.push_back(const_cast<ObjectClass*>(&objClass));
            RebuildMetadata();
            return;
        }
    }

    spdlog::warn("SceneObject: Unknown class '{}'", className);
}

bool SceneObject::HasClass(const std::string& className) const {
    for (const auto* objClass : m_Classes) {
        if (objClass->name == className) {
            return true;
        }
    }
    return false;
}

void SceneObject::RebuildMetadata() {
    m_AggregatedMeta.clear();

    for (const auto* objClass : m_Classes) {
        for (const auto& [id, metadata] : objClass->meta) {
            if (!m_AggregatedMeta.contains(id)) {
                m_AggregatedMeta[id] = metadata;
            }
        }
    }
}

bool SceneObject::HasParameter(const ParameterHandle& handle) const {
    return m_AggregatedMeta.contains(handle.m_Id);
}

ParameterValue SceneObject::GetParameter(const ParameterHandle& handle) const {
    if (auto it = m_Parameters.find(handle.m_Id); it != m_Parameters.end()) {
        return it->second;
    }

    if (auto metaIt = m_AggregatedMeta.find(handle.m_Id); metaIt != m_AggregatedMeta.end()) {
        return metaIt->second.defaultValue;
    }

    spdlog::warn("SceneObject::GetParameter: Parameter '{}' not available in this object's classes", handle.m_Id);
    return 0.0f;
}

void SceneObject::SetParameter(const ParameterHandle& handle, const ParameterValue& value) {
    if (!HasParameter(handle)) {
        spdlog::warn("SceneObject::SetParameter: Parameter '{}' not available in this object's classes", handle.m_Id);
        return;
    }

    m_Parameters[handle.m_Id] = value;
}

void SceneObject::SerializeToYAML(YAML::Emitter& out) const {
    out << YAML::BeginMap;

    out << YAML::Key << "classes" << YAML::Value << YAML::BeginSeq;
    for (const auto* objClass : m_Classes) {
        out << objClass->name;
    }
    out << YAML::EndSeq;

    bool hasNonDefaultParams = false;
    for (const auto& [id, value] : m_Parameters) {
        auto metaIt = m_AggregatedMeta.find(id);
        if (metaIt == m_AggregatedMeta.end()) {
            continue;
        }

        const auto& meta = metaIt->second;

        bool isDefault = false;
        if (std::holds_alternative<bool>(value) && std::holds_alternative<bool>(meta.defaultValue)) {
            isDefault = std::get<bool>(value) == std::get<bool>(meta.defaultValue);
        } else if (std::holds_alternative<int>(value) && std::holds_alternative<int>(meta.defaultValue)) {
            isDefault = std::get<int>(value) == std::get<int>(meta.defaultValue);
        } else if (std::holds_alternative<float>(value) && std::holds_alternative<float>(meta.defaultValue)) {
            isDefault = std::abs(std::get<float>(value) - std::get<float>(meta.defaultValue)) < 0.0001f;
        } else if (std::holds_alternative<std::string>(value) && std::holds_alternative<std::string>(meta.defaultValue)) {
            isDefault = std::get<std::string>(value) == std::get<std::string>(meta.defaultValue);
        } else if (std::holds_alternative<glm::vec3>(value) && std::holds_alternative<glm::vec3>(meta.defaultValue)) {
            auto v1 = std::get<glm::vec3>(value);
            auto v2 = std::get<glm::vec3>(meta.defaultValue);
            isDefault = glm::length(v1 - v2) < 0.0001f;
        }

        if (!isDefault) {
            hasNonDefaultParams = true;
            break;
        }
    }

    if (hasNonDefaultParams) {
        out << YAML::Key << "parameters" << YAML::Value << YAML::BeginMap;

        for (const auto& [id, value] : m_Parameters) {
            auto metaIt = m_AggregatedMeta.find(id);
            if (metaIt == m_AggregatedMeta.end()) {
                continue;
            }

            const auto& meta = metaIt->second;

            bool isDefault = false;
            if (std::holds_alternative<bool>(value) && std::holds_alternative<bool>(meta.defaultValue)) {
                isDefault = std::get<bool>(value) == std::get<bool>(meta.defaultValue);
            } else if (std::holds_alternative<int>(value) && std::holds_alternative<int>(meta.defaultValue)) {
                isDefault = std::get<int>(value) == std::get<int>(meta.defaultValue);
            } else if (std::holds_alternative<float>(value) && std::holds_alternative<float>(meta.defaultValue)) {
                isDefault = std::abs(std::get<float>(value) - std::get<float>(meta.defaultValue)) < 0.0001f;
            } else if (std::holds_alternative<std::string>(value) && std::holds_alternative<std::string>(meta.defaultValue)) {
                isDefault = std::get<std::string>(value) == std::get<std::string>(meta.defaultValue);
            } else if (std::holds_alternative<glm::vec3>(value) && std::holds_alternative<glm::vec3>(meta.defaultValue)) {
                auto v1 = std::get<glm::vec3>(value);
                auto v2 = std::get<glm::vec3>(meta.defaultValue);
                isDefault = glm::length(v1 - v2) < 0.0001f;
            }

            if (isDefault) {
                continue;
            }

            out << YAML::Key << meta.name << YAML::Value;

            if (std::holds_alternative<bool>(value)) {
                out << std::get<bool>(value);
            } else if (std::holds_alternative<int>(value)) {
                out << std::get<int>(value);
            } else if (std::holds_alternative<float>(value)) {
                out << std::get<float>(value);
            } else if (std::holds_alternative<std::string>(value)) {
                out << std::get<std::string>(value);
            } else if (std::holds_alternative<glm::vec3>(value)) {
                auto v = std::get<glm::vec3>(value);
                out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
            } else if (std::holds_alternative<std::vector<std::string>>(value)) {
                auto vec = std::get<std::vector<std::string>>(value);
                out << YAML::BeginSeq;
                for (const auto& str : vec) {
                    out << str;
                }
                out << YAML::EndSeq;
            }
        }

        out << YAML::EndMap;
    }

    out << YAML::EndMap;
}

void SceneObject::DeserializeFromYAML(const YAML::Node& node) {
    m_Classes.clear();
    m_Parameters.clear();

    if (node["classes"]) {
        for (const auto& classNode : node["classes"]) {
            AddClass(classNode.as<std::string>());
        }
    }

    if (node["parameters"]) {
        for (const auto& param : node["parameters"]) {
            std::string paramName = param.first.as<std::string>();
            ParameterHandle handle(paramName.c_str());

            auto metaIt = m_AggregatedMeta.find(handle.m_Id);
            if (metaIt == m_AggregatedMeta.end()) {
                spdlog::warn("SceneObject: Parameter '{}' not available in object's classes, skipping", paramName);
                continue;
            }

            const auto& meta = metaIt->second;

            try {
                ParameterValue value = ParameterRegistry::ParseValueNode(param.second, meta.type);
                m_Parameters[handle.m_Id] = value;
            } catch (const std::exception& e) {
                spdlog::warn("SceneObject: Failed to parse parameter '{}': {}", paramName, e.what());
            }
        }
    }
}

void Scene::Serialize(const std::filesystem::path &path) {
    currentPath = path;
    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "name" << YAML::Value << name;

    if (!objects.empty()) {
        out << YAML::Key << "objects" << YAML::Value << YAML::BeginSeq;
        for (const auto &obj: objects) {
            obj.SerializeToYAML(out);
        }
        out << YAML::EndSeq;
    }

    Application::Instance().GetUI().GetAnimationGraph()->Serialize(out);

    out << YAML::EndMap;
    std::ofstream fout(path);
    fout << out.c_str();
}

void Scene::Deserialize(const std::filesystem::path &path, bool setCurrentPath) {
    if (setCurrentPath) {
        currentPath = path;
    }

    objects.clear();

    YAML::Node root = YAML::LoadFile(path.string());

    if (root["name"]) {
        name = root["name"].as<std::string>();
    } else {
        name = "";
    }

    if (root["objects"]) {
        for (const auto &objNode: root["objects"]) {
            SceneObject obj;
            obj.DeserializeFromYAML(objNode);
            objects.push_back(std::move(obj));
        }
        spdlog::info("Loaded {} dynamic objects from scene", objects.size());
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
    // TODO
    ClearSelection();
}

void Scene::ClearSelection() {
    selectedObject.reset();
}

glm::vec3 *Scene::GetSelectedObjectPosition() {
    if (!selectedObject.has_value()) return nullptr;
    // TODO
    return nullptr;
}

glm::quat *Scene::GetSelectedObjectRotation() {
    if (!selectedObject.has_value()) return nullptr;
    // TODO
    return nullptr;
}

glm::vec3 *Scene::GetSelectedObjectScale() {
    if (!selectedObject.has_value()) return nullptr;
    // TODO
    return nullptr;
}

std::string Scene::GetSelectedObjectName() const {
    if (!selectedObject.has_value()) return "";
    // TODO
    return "";
}

std::optional<Scene::SelectedObject>
Scene::PickObject(const glm::vec3 &rayOrigin, const glm::vec3 &rayDirection) const {
    float closestDistance = std::numeric_limits<float>::max();
    std::optional<SelectedObject> closestObject;
   // TODO
    return closestObject;
}
