#pragma once
#include "Scene.h"
#include <vector>
#include <unordered_map>

#define _DEBUG
#include <PxPhysicsAPI.h>

using namespace physx;

struct PhysicsBodyData {
    float mass = 0.0f;
    PxRigidDynamic* actor = nullptr;
    glm::vec3* scenePosition = nullptr;
    glm::quat* sceneRotation = nullptr;
    glm::vec3* sceneScale = nullptr;
    float radius = 1.0f;
    bool isSphere = false;
};

class Physics {
public:
    void Init();
    void Update(float deltaTime);
    void Shutdown();

    void SetScene(Scene* scene);
    void Apply();
private:
    PxDefaultAllocator m_Allocator;
    PxDefaultErrorCallback m_ErrorCallback;
    PxFoundation* m_Foundation = nullptr;
    PxPhysics* m_Physics = nullptr;
    PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
    PxScene* m_Scene = nullptr;
    PxMaterial* m_Material = nullptr;
    PxPvd* m_pvd = nullptr;
    
    std::vector<PhysicsBodyData> m_Bodies;
    
    void CreatePhysicsBody(PhysicsBodyData& data);
    void ApplyGravitationalForces();
    void UpdatePhysicsBodies();
    
    static constexpr float G = 6.67430e-11f;
    static constexpr float SOLAR_MASS = 1.989e30f;
};