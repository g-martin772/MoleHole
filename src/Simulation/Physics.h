#pragma once
#include "Scene.h"
#include <vector>
#include <unordered_map>

#define _DEBUG
#include <PxPhysicsAPI.h>

using namespace physx;

class Renderer;

struct PhysicsBodyData {
    float mass = 0.0f;
    PxRigidDynamic* actor = nullptr;
    glm::vec3* scenePosition = nullptr;
    glm::quat* sceneRotation = nullptr;
    glm::vec3* sceneScale = nullptr;
    glm::vec3 initialVelocity = glm::vec3(0.0f);
    float radius = 1.0f;
    bool isSphere = false;
    std::string meshPath;
};

class Physics {
public:
    void Init();
    void Update(float deltaTime);
    void Shutdown();

    void SetScene(Scene* scene);
    void Apply();
    void SetRenderer(Renderer* renderer) { m_Renderer = renderer; }
private:
    PxDefaultAllocator m_Allocator;
    PxDefaultErrorCallback m_ErrorCallback;
    PxFoundation* m_Foundation = nullptr;
    PxPhysics* m_Physics = nullptr;
    PxDefaultCpuDispatcher* m_Dispatcher = nullptr;
    PxScene* m_Scene = nullptr;
    PxMaterial* m_Material = nullptr;
    PxPvd* m_pvd = nullptr;
    PxCooking* m_Cooking = nullptr;
    Renderer* m_Renderer = nullptr;

    std::vector<PhysicsBodyData> m_Bodies;
    std::unordered_map<std::string, PxConvexMesh*> m_MeshCache;

    void CreatePhysicsBody(PhysicsBodyData& data);
    void ApplyGravitationalForces();
    void UpdatePhysicsBodies();
    PxConvexMesh* LoadConvexMesh(const std::string& path);

    static constexpr float G = 6.67430e-11f;
    static constexpr float SOLAR_MASS = 1.989e30f;
};