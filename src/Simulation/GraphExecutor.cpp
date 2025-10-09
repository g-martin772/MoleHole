#include "GraphExecutor.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

GraphExecutor::GraphExecutor(AnimationGraph* graph, Scene* scene)
    : m_pGraph(graph), m_pScene(scene) {
}

void GraphExecutor::ExecuteStartEvent() {
    if (!m_pGraph) return;

    for (auto& node : m_pGraph->GetNodes()) {
        if (node.Type == AnimationGraph::NodeType::Event &&
            node.SubType == AnimationGraph::NodeSubType::Start) {
            if (!node.Outputs.empty()) {
                ExecuteFlowFromPin(node.Outputs[0].Id, 0.0f);
            }
        }
    }
}

void GraphExecutor::ExecuteTickEvent(float deltaTime) {
    if (!m_pGraph) return;

    m_PinValues.clear();

    spdlog::debug("[GraphExecutor] ExecuteTickEvent called with deltaTime={}", deltaTime);

    for (auto& node : m_pGraph->GetNodes()) {
        if (node.Type == AnimationGraph::NodeType::Event &&
            node.SubType == AnimationGraph::NodeSubType::Tick) {
            if (!node.Outputs.empty()) {
                m_PinValues[node.Outputs[1].Id.Get()] = deltaTime;
                spdlog::debug("[GraphExecutor] Found Tick node, storing deltaTime in pin {}", node.Outputs[1].Id.Get());
                ExecuteFlowFromPin(node.Outputs[0].Id, deltaTime);
            }
        }
    }
}

void GraphExecutor::ExecuteFlowFromPin(ax::NodeEditor::PinId pinId, float deltaTime) {
    for (const auto& link : m_pGraph->GetLinks()) {
        if (link.StartPinId == pinId) {
            auto* targetNode = FindNodeByInputPin(link.EndPinId);
            if (targetNode) {
                ExecuteNode(targetNode, link.EndPinId, deltaTime);
            }
        }
    }
}

void GraphExecutor::ExecuteNode(AnimationGraph::Node* node, ax::NodeEditor::PinId entryPin, float deltaTime) {
    switch (node->Type) {
        case AnimationGraph::NodeType::Print:
            ExecutePrint(node, deltaTime);
            if (node->Outputs.size() > 0) {
                ExecuteFlowFromPin(node->Outputs[0].Id, deltaTime);
            }
            break;
        case AnimationGraph::NodeType::Control:
            ExecuteControlFlow(node, entryPin, deltaTime);
            break;
        case AnimationGraph::NodeType::Setter:
            ExecuteSetter(node, deltaTime);
            if (node->Outputs.size() > 0) {
                ExecuteFlowFromPin(node->Outputs[0].Id, deltaTime);
            }
            break;
        case AnimationGraph::NodeType::Variable:
            if (node->SubType == AnimationGraph::NodeSubType::VariableSet) {
                ExecuteVariableSet(node, deltaTime);
                if (node->Outputs.size() > 0) {
                    ExecuteFlowFromPin(node->Outputs[0].Id, deltaTime);
                }
            }
            break;
        default:
            break;
    }
}

GraphExecutor::Value GraphExecutor::EvaluatePinValue(ax::NodeEditor::PinId pinId, float deltaTime) {
    spdlog::debug("[GraphExecutor] EvaluatePinValue for pin {}", pinId.Get());

    if (m_PinValues.find(pinId.Get()) != m_PinValues.end()) {
        auto& val = m_PinValues[pinId.Get()];
        spdlog::debug("[GraphExecutor] Pin {} found in cache: {}", pinId.Get(), ValueToString(val));
        return val;
    }

    auto outputPin = GetConnectedOutputPin(pinId);
    if (outputPin.Get() == 0) {
        spdlog::debug("[GraphExecutor] Pin {} has no connection", pinId.Get());
        return std::monostate{};
    }

    spdlog::debug("[GraphExecutor] Pin {} connected to output pin {}", pinId.Get(), outputPin.Get());

    if (m_PinValues.find(outputPin.Get()) != m_PinValues.end()) {
        auto& val = m_PinValues[outputPin.Get()];
        spdlog::debug("[GraphExecutor] Output pin {} found in cache: {}", outputPin.Get(), ValueToString(val));
        return val;
    }

    auto* sourceNode = FindNodeByOutputPin(outputPin);
    if (!sourceNode) {
        spdlog::debug("[GraphExecutor] No source node found for output pin {}", outputPin.Get());
        return std::monostate{};
    }

    spdlog::debug("[GraphExecutor] Source node: {}", sourceNode->Name);

    if (sourceNode->Type == AnimationGraph::NodeType::Decomposer) {
        spdlog::debug("[GraphExecutor] Executing decomposer node");
        ExecuteDecomposer(sourceNode, deltaTime);
        if (m_PinValues.find(outputPin.Get()) != m_PinValues.end()) {
            auto& val = m_PinValues[outputPin.Get()];
            spdlog::debug("[GraphExecutor] Decomposer result for pin {}: {}", outputPin.Get(), ValueToString(val));
            return val;
        }
        spdlog::debug("[GraphExecutor] Decomposer did not produce value for pin {}", outputPin.Get());
        return std::monostate{};
    }

    auto result = EvaluateNode(sourceNode, deltaTime);
    m_PinValues[outputPin.Get()] = result;
    spdlog::debug("[GraphExecutor] Evaluated node result for pin {}: {}", outputPin.Get(), ValueToString(result));
    return result;
}

GraphExecutor::Value GraphExecutor::EvaluateNode(AnimationGraph::Node* node, float deltaTime) {
    switch (node->Type) {
        case AnimationGraph::NodeType::Constant:
            return ExecuteConstant(node);
        case AnimationGraph::NodeType::Function:
            return ExecuteMathOperation(node, deltaTime);
        case AnimationGraph::NodeType::Decomposer:
            return ExecuteDecomposer(node, deltaTime);
        case AnimationGraph::NodeType::Other:
            return ExecuteSceneGetter(node);
        case AnimationGraph::NodeType::Variable:
            if (node->SubType == AnimationGraph::NodeSubType::VariableGet) {
                return ExecuteVariableGet(node);
            }
            break;
        default:
            break;
    }
    return std::monostate{};
}

AnimationGraph::Node* GraphExecutor::FindNodeByOutputPin(ax::NodeEditor::PinId pinId) {
    for (auto& node : m_pGraph->GetNodes()) {
        for (const auto& pin : node.Outputs) {
            if (pin.Id == pinId) {
                return &node;
            }
        }
    }
    return nullptr;
}

AnimationGraph::Node* GraphExecutor::FindNodeByInputPin(ax::NodeEditor::PinId pinId) {
    for (auto& node : m_pGraph->GetNodes()) {
        for (const auto& pin : node.Inputs) {
            if (pin.Id == pinId) {
                return &node;
            }
        }
    }
    return nullptr;
}

ax::NodeEditor::PinId GraphExecutor::GetConnectedOutputPin(ax::NodeEditor::PinId inputPinId) {
    for (const auto& link : m_pGraph->GetLinks()) {
        if (link.EndPinId == inputPinId) {
            return link.StartPinId;
        }
    }
    return ax::NodeEditor::PinId(0);
}

GraphExecutor::Value GraphExecutor::ExecuteConstant(AnimationGraph::Node* node) {
    if (std::holds_alternative<std::string>(node->Value)) {
        return std::get<std::string>(node->Value);
    }
    if (std::holds_alternative<float>(node->Value)) {
        return std::get<float>(node->Value);
    }
    if (std::holds_alternative<int>(node->Value)) {
        return std::get<int>(node->Value);
    }
    if (std::holds_alternative<glm::vec2>(node->Value)) {
        return std::get<glm::vec2>(node->Value);
    }
    if (std::holds_alternative<glm::vec3>(node->Value)) {
        return std::get<glm::vec3>(node->Value);
    }
    if (std::holds_alternative<glm::vec4>(node->Value)) {
        return std::get<glm::vec4>(node->Value);
    }
    return std::monostate{};
}

GraphExecutor::Value GraphExecutor::ExecuteMathOperation(AnimationGraph::Node* node, float deltaTime) {
    switch (node->SubType) {
        case AnimationGraph::NodeSubType::Add: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) {
                return std::get<float>(a) + std::get<float>(b);
            }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) {
                return std::get<glm::vec2>(a) + std::get<glm::vec2>(b);
            }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
                return std::get<glm::vec3>(a) + std::get<glm::vec3>(b);
            }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
                return std::get<glm::vec4>(a) + std::get<glm::vec4>(b);
            }
            break;
        }
        case AnimationGraph::NodeSubType::Sub: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) {
                return std::get<float>(a) - std::get<float>(b);
            }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) {
                return std::get<glm::vec2>(a) - std::get<glm::vec2>(b);
            }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
                return std::get<glm::vec3>(a) - std::get<glm::vec3>(b);
            }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
                return std::get<glm::vec4>(a) - std::get<glm::vec4>(b);
            }
            break;
        }
        case AnimationGraph::NodeSubType::Mul: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) {
                return std::get<float>(a) * std::get<float>(b);
            }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) {
                return std::get<glm::vec2>(a) * std::get<glm::vec2>(b);
            }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
                return std::get<glm::vec3>(a) * std::get<glm::vec3>(b);
            }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
                return std::get<glm::vec4>(a) * std::get<glm::vec4>(b);
            }
            break;
        }
        case AnimationGraph::NodeSubType::Div: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) {
                float divisor = std::get<float>(b);
                return divisor != 0.0f ? std::get<float>(a) / divisor : 0.0f;
            }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) {
                return std::get<glm::vec2>(a) / std::get<glm::vec2>(b);
            }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
                return std::get<glm::vec3>(a) / std::get<glm::vec3>(b);
            }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
                return std::get<glm::vec4>(a) / std::get<glm::vec4>(b);
            }
            break;
        }
        case AnimationGraph::NodeSubType::Min: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) {
                return std::min(std::get<float>(a), std::get<float>(b));
            }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) {
                return glm::min(std::get<glm::vec2>(a), std::get<glm::vec2>(b));
            }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
                return glm::min(std::get<glm::vec3>(a), std::get<glm::vec3>(b));
            }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
                return glm::min(std::get<glm::vec4>(a), std::get<glm::vec4>(b));
            }
            break;
        }
        case AnimationGraph::NodeSubType::Max: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) {
                return std::max(std::get<float>(a), std::get<float>(b));
            }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) {
                return glm::max(std::get<glm::vec2>(a), std::get<glm::vec2>(b));
            }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
                return glm::max(std::get<glm::vec3>(a), std::get<glm::vec3>(b));
            }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
                return glm::max(std::get<glm::vec4>(a), std::get<glm::vec4>(b));
            }
            break;
        }
        case AnimationGraph::NodeSubType::Sin: {
            auto val = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            if (std::holds_alternative<float>(val)) {
                return std::sin(std::get<float>(val));
            }
            break;
        }
        case AnimationGraph::NodeSubType::Cos: {
            auto val = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            if (std::holds_alternative<float>(val)) {
                return std::cos(std::get<float>(val));
            }
            break;
        }
        case AnimationGraph::NodeSubType::Tan: {
            auto val = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            if (std::holds_alternative<float>(val)) {
                return std::tan(std::get<float>(val));
            }
            break;
        }
        case AnimationGraph::NodeSubType::Sqrt: {
            auto val = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            if (std::holds_alternative<float>(val)) {
                return std::sqrt(std::max(0.0f, std::get<float>(val)));
            }
            if (std::holds_alternative<glm::vec2>(val)) {
                return glm::sqrt(std::get<glm::vec2>(val));
            }
            if (std::holds_alternative<glm::vec3>(val)) {
                return glm::sqrt(std::get<glm::vec3>(val));
            }
            if (std::holds_alternative<glm::vec4>(val)) {
                return glm::sqrt(std::get<glm::vec4>(val));
            }
            break;
        }
        case AnimationGraph::NodeSubType::Negate: {
            auto val = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            if (std::holds_alternative<float>(val)) {
                return -std::get<float>(val);
            }
            if (std::holds_alternative<glm::vec2>(val)) {
                return -std::get<glm::vec2>(val);
            }
            if (std::holds_alternative<glm::vec3>(val)) {
                return -std::get<glm::vec3>(val);
            }
            if (std::holds_alternative<glm::vec4>(val)) {
                return -std::get<glm::vec4>(val);
            }
            break;
        }
        case AnimationGraph::NodeSubType::Length: {
            auto val = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            if (std::holds_alternative<glm::vec2>(val)) {
                return glm::length(std::get<glm::vec2>(val));
            }
            if (std::holds_alternative<glm::vec3>(val)) {
                return glm::length(std::get<glm::vec3>(val));
            }
            if (std::holds_alternative<glm::vec4>(val)) {
                return glm::length(std::get<glm::vec4>(val));
            }
            break;
        }
        case AnimationGraph::NodeSubType::Distance: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) {
                return glm::distance(std::get<glm::vec2>(a), std::get<glm::vec2>(b));
            }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
                return glm::distance(std::get<glm::vec3>(a), std::get<glm::vec3>(b));
            }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
                return glm::distance(std::get<glm::vec4>(a), std::get<glm::vec4>(b));
            }
            break;
        }
        case AnimationGraph::NodeSubType::Lerp: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            auto t = EvaluatePinValue(node->Inputs[2].Id, deltaTime);
            float tVal = GetValueAs<float>(t, 0.0f);
            if (std::holds_alternative<float>(a) && std::holds_alternative<float>(b)) {
                return glm::mix(std::get<float>(a), std::get<float>(b), tVal);
            }
            if (std::holds_alternative<glm::vec2>(a) && std::holds_alternative<glm::vec2>(b)) {
                return glm::mix(std::get<glm::vec2>(a), std::get<glm::vec2>(b), tVal);
            }
            if (std::holds_alternative<glm::vec3>(a) && std::holds_alternative<glm::vec3>(b)) {
                return glm::mix(std::get<glm::vec3>(a), std::get<glm::vec3>(b), tVal);
            }
            if (std::holds_alternative<glm::vec4>(a) && std::holds_alternative<glm::vec4>(b)) {
                return glm::mix(std::get<glm::vec4>(a), std::get<glm::vec4>(b), tVal);
            }
            break;
        }
        case AnimationGraph::NodeSubType::Clamp: {
            auto val = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto minVal = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            auto maxVal = EvaluatePinValue(node->Inputs[2].Id, deltaTime);
            if (std::holds_alternative<float>(val) && std::holds_alternative<float>(minVal) && std::holds_alternative<float>(maxVal)) {
                return glm::clamp(std::get<float>(val), std::get<float>(minVal), std::get<float>(maxVal));
            }
            if (std::holds_alternative<glm::vec2>(val) && std::holds_alternative<glm::vec2>(minVal) && std::holds_alternative<glm::vec2>(maxVal)) {
                return glm::clamp(std::get<glm::vec2>(val), std::get<glm::vec2>(minVal), std::get<glm::vec2>(maxVal));
            }
            if (std::holds_alternative<glm::vec3>(val) && std::holds_alternative<glm::vec3>(minVal) && std::holds_alternative<glm::vec3>(maxVal)) {
                return glm::clamp(std::get<glm::vec3>(val), std::get<glm::vec3>(minVal), std::get<glm::vec3>(maxVal));
            }
            if (std::holds_alternative<glm::vec4>(val) && std::holds_alternative<glm::vec4>(minVal) && std::holds_alternative<glm::vec4>(maxVal)) {
                return glm::clamp(std::get<glm::vec4>(val), std::get<glm::vec4>(minVal), std::get<glm::vec4>(maxVal));
            }
            break;
        }
        case AnimationGraph::NodeSubType::And: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            return GetValueAs<bool>(a, false) && GetValueAs<bool>(b, false);
        }
        case AnimationGraph::NodeSubType::Or: {
            auto a = EvaluatePinValue(node->Inputs[0].Id, deltaTime);
            auto b = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
            return GetValueAs<bool>(a, false) || GetValueAs<bool>(b, false);
        }
        default:
            break;
    }
    return std::monostate{};
}

GraphExecutor::Value GraphExecutor::ExecuteDecomposer(AnimationGraph::Node* node, float deltaTime) {
    auto inputVal = EvaluatePinValue(node->Inputs[0].Id, deltaTime);

    spdlog::debug("[GraphExecutor] ExecuteDecomposer for node: {}", node->Name);

    if (node->SubType == AnimationGraph::NodeSubType::Blackhole && std::holds_alternative<BlackHole*>(inputVal)) {
        BlackHole* bh = std::get<BlackHole*>(inputVal);
        spdlog::debug("[GraphExecutor] Decomposing BlackHole - mass={}, position=({},{},{})",
                     bh->mass, bh->position.x, bh->position.y, bh->position.z);
        for (size_t i = 0; i < node->Outputs.size(); ++i) {
            switch (i) {
                case 0: m_PinValues[node->Outputs[i].Id.Get()] = bh->mass;
                        spdlog::debug("[GraphExecutor] Stored mass {} in pin {}", bh->mass, node->Outputs[i].Id.Get());
                        break;
                case 1: m_PinValues[node->Outputs[i].Id.Get()] = bh->position; break;
                case 2: m_PinValues[node->Outputs[i].Id.Get()] = bh->showAccretionDisk; break;
                case 3: m_PinValues[node->Outputs[i].Id.Get()] = bh->accretionDiskDensity; break;
                case 4: m_PinValues[node->Outputs[i].Id.Get()] = bh->accretionDiskSize; break;
                case 5: m_PinValues[node->Outputs[i].Id.Get()] = bh->accretionDiskColor; break;
                case 6: m_PinValues[node->Outputs[i].Id.Get()] = bh->spin; break;
                case 7: m_PinValues[node->Outputs[i].Id.Get()] = bh->spinAxis; break;
            }
        }
    } else {
        spdlog::debug("[GraphExecutor] Decomposer input is not a valid BlackHole pointer");
    }

    return std::monostate{};
}

GraphExecutor::Value GraphExecutor::ExecuteSceneGetter(AnimationGraph::Node* node) {
    if (node->SubType == AnimationGraph::NodeSubType::Blackhole) {
        if (node->SceneObjectIndex >= 1 && node->SceneObjectIndex < static_cast<int>(m_pScene->blackHoles.size())) {
            return &m_pScene->blackHoles[node->SceneObjectIndex - 1];
        }
    }
    return std::monostate{};
}

void GraphExecutor::ExecuteSetter(AnimationGraph::Node* node, float deltaTime) {
    spdlog::debug("[GraphExecutor] ExecuteSetter for node: {}", node->Name);

    if (node->SubType == AnimationGraph::NodeSubType::Blackhole) {
        auto bhVal = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
        if (!std::holds_alternative<BlackHole*>(bhVal)) {
            spdlog::debug("[GraphExecutor] ExecuteSetter: BlackHole input is not valid");
            return;
        }

        BlackHole* bh = std::get<BlackHole*>(bhVal);
        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting properties on BlackHole at {}", (void*)bh);

        for (size_t i = 2; i < node->Inputs.size(); ++i) {
            auto val = EvaluatePinValue(node->Inputs[i].Id, deltaTime);
            if (std::holds_alternative<std::monostate>(val)) {
                spdlog::debug("[GraphExecutor] ExecuteSetter: Input {} has no value (not connected)", i);
                continue;
            }

            switch (i) {
                case 2:
                    {
                        float newMass = GetValueAs<float>(val, bh->mass);
                        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting mass from {} to {}", bh->mass, newMass);
                        bh->mass = newMass;
                    }
                    break;
                case 3:
                    {
                        glm::vec3 newPos = GetValueAs<glm::vec3>(val, bh->position);
                        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting position from ({},{},{}) to ({},{},{})",
                                     bh->position.x, bh->position.y, bh->position.z, newPos.x, newPos.y, newPos.z);
                        bh->position = newPos;
                    }
                    break;
                case 4:
                    {
                        bool newShowDisk = GetValueAs<bool>(val, bh->showAccretionDisk);
                        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting showAccretionDisk from {} to {}",
                                     bh->showAccretionDisk, newShowDisk);
                        bh->showAccretionDisk = newShowDisk;
                    }
                    break;
                case 5:
                    {
                        float newDiskDensity = GetValueAs<float>(val, bh->accretionDiskDensity);
                        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting accretionDiskDensity from {} to {}",
                                     bh->accretionDiskDensity, newDiskDensity);
                        bh->accretionDiskDensity = newDiskDensity;
                    }
                    break;
                case 6:
                    {
                        float newDiskSize = GetValueAs<float>(val, bh->accretionDiskSize);
                        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting accretionDiskSize from {} to {}",
                                     bh->accretionDiskSize, newDiskSize);
                        bh->accretionDiskSize = newDiskSize;
                    }
                    break;
                case 7:
                    {
                        glm::vec3 newDiskColor = GetValueAs<glm::vec3>(val, bh->accretionDiskColor);
                        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting accretionDiskColor from ({},{},{}) to ({},{},{})",
                                     bh->accretionDiskColor.r, bh->accretionDiskColor.g, bh->accretionDiskColor.b,
                                     newDiskColor.r, newDiskColor.g, newDiskColor.b);
                        bh->accretionDiskColor = newDiskColor;
                    }
                    break;
                case 8:
                    {
                        float newSpin = GetValueAs<float>(val, bh->spin);
                        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting spin from {} to {}",
                                     bh->spin, newSpin);
                        bh->spin = newSpin;
                    }
                    break;
                case 9:
                    {
                        glm::vec3 newSpinAxis = GetValueAs<glm::vec3>(val, bh->spinAxis);
                        spdlog::debug("[GraphExecutor] ExecuteSetter: Setting spinAxis from ({},{},{}) to ({},{},{})",
                                     bh->spinAxis.x, bh->spinAxis.y, bh->spinAxis.z,
                                     newSpinAxis.x, newSpinAxis.y, newSpinAxis.z);
                        bh->spinAxis = newSpinAxis;
                    }
                    break;
            }
        }

        if (node->Outputs.size() > 1) {
            m_PinValues[node->Outputs[1].Id.Get()] = bh;
        }
    }
}

void GraphExecutor::ExecuteControlFlow(AnimationGraph::Node* node, ax::NodeEditor::PinId entryPin, float deltaTime) {
    if (node->SubType == AnimationGraph::NodeSubType::Branch || node->SubType == AnimationGraph::NodeSubType::If) {
        auto condition = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
        bool condResult = GetValueAs<bool>(condition, false);

        if (condResult && node->Outputs.size() > 0) {
            ExecuteFlowFromPin(node->Outputs[0].Id, deltaTime);
        } else if (!condResult && node->Outputs.size() > 1) {
            ExecuteFlowFromPin(node->Outputs[1].Id, deltaTime);
        }
    } else if (node->SubType == AnimationGraph::NodeSubType::For) {
        auto startVal = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
        auto endVal = EvaluatePinValue(node->Inputs[2].Id, deltaTime);

        int start = GetValueAs<int>(startVal, 0);
        int end = GetValueAs<int>(endVal, 0);

        for (int i = start; i < end; ++i) {
            if (node->Outputs.size() > 1) {
                m_PinValues[node->Outputs[1].Id.Get()] = i;
            }
            if (node->Outputs.size() > 0) {
                ExecuteFlowFromPin(node->Outputs[0].Id, deltaTime);
            }
        }

        if (node->Outputs.size() > 2) {
            ExecuteFlowFromPin(node->Outputs[2].Id, deltaTime);
        }
    }
}

void GraphExecutor::ExecutePrint(AnimationGraph::Node* node, float deltaTime) {
    auto val = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
    std::string output = ValueToString(val);
    spdlog::info("[Graph Print] {}", output);
}

GraphExecutor::Value GraphExecutor::ExecuteVariableGet(AnimationGraph::Node* node) {
    auto it = m_Variables.find(node->VariableName);
    if (it != m_Variables.end()) {
        return it->second;
    }
    return std::monostate{};
}

void GraphExecutor::ExecuteVariableSet(AnimationGraph::Node* node, float deltaTime) {
    auto val = EvaluatePinValue(node->Inputs[1].Id, deltaTime);
    m_Variables[node->VariableName] = val;
}

std::string GraphExecutor::ValueToString(const Value& val) {
    if (std::holds_alternative<std::string>(val)) {
        return std::get<std::string>(val);
    }
    if (std::holds_alternative<float>(val)) {
        return std::to_string(std::get<float>(val));
    }
    if (std::holds_alternative<int>(val)) {
        return std::to_string(std::get<int>(val));
    }
    if (std::holds_alternative<bool>(val)) {
        return std::get<bool>(val) ? "true" : "false";
    }
    if (std::holds_alternative<glm::vec2>(val)) {
        auto v = std::get<glm::vec2>(val);
        return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
    }
    if (std::holds_alternative<glm::vec3>(val)) {
        auto v = std::get<glm::vec3>(val);
        return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
    }
    if (std::holds_alternative<glm::vec4>(val)) {
        auto v = std::get<glm::vec4>(val);
        return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ", " + std::to_string(v.w) + ")";
    }
    return "<empty>";
}

template<typename T>
T GraphExecutor::GetValueAs(const Value& val, T defaultValue) {
    if (std::holds_alternative<T>(val)) {
        return std::get<T>(val);
    }
    if constexpr (std::is_same_v<T, bool>) {
        if (std::holds_alternative<int>(val)) return std::get<int>(val) != 0;
        if (std::holds_alternative<float>(val)) return std::get<float>(val) != 0.0f;
    }
    if constexpr (std::is_same_v<T, float>) {
        if (std::holds_alternative<int>(val)) return static_cast<float>(std::get<int>(val));
    }
    if constexpr (std::is_same_v<T, int>) {
        if (std::holds_alternative<float>(val)) return static_cast<int>(std::get<float>(val));
    }
    return defaultValue;
}
