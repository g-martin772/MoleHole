#include "Simulation.h"
#include "GraphExecutor.h"
#include "Application/AnimationGraph.h"
#include "Application/Application.h"
#include "Application/Parameters.h"

Simulation::Simulation() : m_State(State::Stopped), m_SimulationTime(0.0f), m_AnimationGraph(nullptr), m_StartEventExecuted(false) {
    m_Scene = std::make_unique<Scene>();
    m_SavedScene = std::make_unique<Scene>();
    m_Physics = std::make_unique<Physics>();
    m_Physics->Init();
}

Simulation::~Simulation() {
    m_Physics->Shutdown();
    m_Physics.reset();
    m_Scene.reset();
    m_SavedScene.reset();
};

void Simulation::Initialize() {
    spdlog::info("Initializing Simulation");

    LoadObjectClasses("../assets/config/classes.yaml");
    LoadObjectDefinitions("../assets/config/base_objects.yaml");

    auto additionalClassSources = Application::Params().Get(
        Params::AdditionalObjectClassSources, std::vector<std::string>());
    for (const auto& source : additionalClassSources) {
        LoadObjectClasses(source);
    }

    auto additionalDefSources = Application::Params().Get(
        Params::AdditionalSceneObjectDefinitionSources, std::vector<std::string>());
    for (const auto& source : additionalDefSources) {
        LoadObjectDefinitions(source);
    }

    spdlog::info("Simulation initialized successfully");
}

void Simulation::Update(float deltaTime) {
    if (m_State == State::Running) {
        UpdateSimulation(deltaTime);
        m_SimulationTime += deltaTime;
    }
}

void Simulation::Start() {
    if (m_State == State::Stopped) {
        SaveSceneState();
        m_SimulationTime = 0.0f;
        m_StartEventExecuted = false;

        if (m_AnimationGraph) {
            m_GraphExecutor = std::make_unique<GraphExecutor>(m_AnimationGraph, m_Scene.get());
            m_GraphExecutor->ExecuteStartEvent();
            m_StartEventExecuted = true;
        }

        spdlog::info("Simulation started");
    } else if (m_State == State::Paused) {
        spdlog::info("Simulation resumed from pause");
    }
    m_State = State::Running;
    m_Physics->SetScene(m_Scene.get());
}

void Simulation::Stop() {
    if (m_State != State::Stopped) {
        RestoreSceneState();
        m_SimulationTime = 0.0f;
        m_State = State::Stopped;
        m_StartEventExecuted = false;
        m_GraphExecutor.reset();
        
        auto& renderer = Application::GetRenderer();
        if (auto* pathsRenderer = renderer.GetObjectPathsRenderer()) {
            //pathsRenderer->ClearHistories();
        }
        
        spdlog::info("Simulation stopped and reset to initial state");
    }
}

void Simulation::Pause() {
    if (m_State == State::Running) {
        m_State = State::Paused;
        spdlog::info("Simulation paused at time: {:.2f}s", m_SimulationTime);
    }
}

void Simulation::Reset() {
    RestoreSceneState();
    m_SimulationTime = 0.0f;
    if (m_State != State::Stopped) {
        m_State = State::Stopped;
        m_StartEventExecuted = false;
        m_GraphExecutor.reset();
        
        auto& renderer = Application::GetRenderer();
        if (auto* pathsRenderer = renderer.GetObjectPathsRenderer()) {
            //pathsRenderer->ClearHistories();
        }
        
        spdlog::info("Simulation reset to initial state");
    }
}

Scene* Simulation::GetScene() const {
    return m_Scene.get();
}

void Simulation::LoadScene(const std::filesystem::path& path) {
    try {
        if (m_Scene && std::filesystem::exists(path)) {
            m_Scene->Deserialize(path);
            Application::Params().Set(Params::AppLastOpenScene, path.string());
            spdlog::info("Loaded scene: {}", path.string());
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to load scene {}: {}", path.string(), e.what());
    }
}

void Simulation::SaveScene(const std::filesystem::path& path) {
    try {
        if (m_Scene) {
            m_Scene->Serialize(path);
            Application::Params().Set(Params::AppLastOpenScene, path.string());
            spdlog::info("Saved scene: {}", path.string());
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to save scene {}: {}", path.string(), e.what());
    }
}

void Simulation::NewScene() {
    if (m_Scene) {
        m_Scene->objects.clear();
        m_Scene->name = "New Scene";
        m_Scene->currentPath.clear();
        Application::Params().Set(Params::AppLastOpenScene, std::string(""));
        spdlog::info("Created new scene");
    }
}

void Simulation::SaveSceneState() const {
    *m_SavedScene = *m_Scene;
    spdlog::debug("Scene state saved");
}

void Simulation::RestoreSceneState() const {
    *m_Scene = *m_SavedScene;
    spdlog::debug("Scene state restored");
}

void Simulation::SetAnimationGraph(AnimationGraph* graph) {
    m_AnimationGraph = graph;
    if (m_State == State::Running && m_AnimationGraph) {
        m_GraphExecutor = std::make_unique<GraphExecutor>(m_AnimationGraph, m_Scene.get());
        if (!m_StartEventExecuted) {
            m_GraphExecutor->ExecuteStartEvent();
            m_StartEventExecuted = true;
        }
    }
}

void Simulation::UpdateSimulation(float deltaTime) const {
    if (m_GraphExecutor && m_AnimationGraph) {
        m_GraphExecutor->ExecuteTickEvent(deltaTime);
    }
    m_Physics->Update(deltaTime, m_Scene.get());
    m_Physics->Apply();
    
    auto& renderer = Application::GetRenderer();
    if (auto* pathsRenderer = renderer.GetObjectPathsRenderer()) {
        //pathsRenderer->RecordCurrentPositions(m_Scene.get());
    }
}

void Simulation::LoadObjectClasses(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        spdlog::warn("Object classes file not found: {}", path.string());
        return;
    }

    try {
        YAML::Node root = YAML::LoadFile(path.string());

        if (!root["classes"]) {
            spdlog::warn("No 'classes' key found in {}", path.string());
            return;
        }

        int loadedCount = 0;
        for (const auto& classNode : root["classes"]) {
            ObjectClass objClass;
            objClass.name = classNode["name"].as<std::string>();

            if (classNode["available_parameters"]) {
                for (const auto& param : classNode["available_parameters"]) {
                    objClass.availableParameterKeys.push_back(param.as<std::string>());
                }
            }

            if (classNode["required_parameters"]) {
                for (const auto& param : classNode["required_parameters"]) {
                    objClass.requiredParameterKeys.push_back(param.as<std::string>());
                }
            }

            auto& paramRegistry = ParameterRegistry::Instance();
            for (const auto& paramKey : objClass.availableParameterKeys) {
                ParameterHandle handle(paramKey.c_str());
                auto metadata = paramRegistry.GetMetadata(handle);

                if (metadata.has_value()) {
                    objClass.meta[handle.m_Id] = metadata.value();
                } else {
                    spdlog::warn("Class '{}' references unknown parameter '{}' - not found in parameters.yaml",
                                 objClass.name, paramKey);
                }
            }

            m_ObjectClasses.push_back(std::move(objClass));
            loadedCount++;
        }

        spdlog::info("Loaded {} object classes from {}", loadedCount, path.string());
    } catch (const std::exception& e) {
        spdlog::error("Failed to load object classes from {}: {}", path.string(), e.what());
    }
}

void Simulation::LoadObjectDefinitions(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) {
        spdlog::warn("Object definitions file not found: {}", path.string());
        return;
    }

    try {
        YAML::Node root = YAML::LoadFile(path.string());

        if (!root["base_objects"]) {
            spdlog::warn("No 'base_objects' key found in {}", path.string());
            return;
        }

        int loadedCount = 0;
        for (const auto& defNode : root["base_objects"]) {
            SceneObjectDefinition objDef;
            std::string defName = defNode["name"] ? defNode["name"].as<std::string>() : "Unknown";
            objDef.name = defName;

            if (!defNode["classes"]) {
                spdlog::warn("Object definition '{}' has no 'classes' list", defName);
                continue;
            }

            for (const auto& classNameNode : defNode["classes"]) {
                std::string className = classNameNode.as<std::string>();

                auto classIt = std::find_if(m_ObjectClasses.begin(), m_ObjectClasses.end(),
                    [&className](const ObjectClass& cls) { return cls.name == className; });

                if (classIt != m_ObjectClasses.end()) {
                    objDef.objectClasses.push_back(&(*classIt));

                    for (const auto& [metaId, metadata] : classIt->meta) {
                        auto existingMeta = objDef.meta.find(metaId);
                        if (existingMeta != objDef.meta.end()) {
                            spdlog::debug("Object definition '{}': parameter '{}' defined in multiple classes, using first definition",
                                         defName, metadata.name);
                        } else {
                            objDef.meta[metaId] = metadata;
                        }
                    }
                } else {
                    spdlog::warn("Object definition '{}' references unknown class '{}'", defName, className);
                }
            }

            // Validate: check if all available parameters from all classes have metadata
            for (const auto* objClass : objDef.objectClasses) {
                for (const auto& paramKey : objClass->availableParameterKeys) {
                    ParameterHandle handle(paramKey.c_str());
                    if (objDef.meta.find(handle.m_Id) == objDef.meta.end()) {
                        spdlog::warn("Object definition '{}' missing metadata for available parameter '{}' from class '{}'",
                                     defName, paramKey, objClass->name);
                    }
                }
            }

            m_ObjectDefinitions.push_back(std::move(objDef));
            loadedCount++;
        }

        spdlog::info("Loaded {} object definitions from {}", loadedCount, path.string());
    } catch (const std::exception& e) {
        spdlog::error("Failed to load object definitions from {}: {}", path.string(), e.what());
    }
}

