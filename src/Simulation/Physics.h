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
    bool isBlackHole = false;
    std::string meshPath;
    size_t sceneIndex = 0;
    Scene::ObjectType objectType;
};

struct BlackHoleBodyData {
    PxRigidStatic* actor = nullptr;
    glm::vec3* scenePosition = nullptr;
    float schwarzschildRadius = 0.0f;
    size_t sceneIndex = 0;
};

class Physics : public PxSimulationEventCallback {
public:
    void Init();
    void Update(float deltaTime);
    void Shutdown();

    void SetScene(Scene* scene);
    void Apply();
    void SetRenderer(Renderer* renderer) { m_Renderer = renderer; }

    void onConstraintBreak(PxConstraintInfo* constraints, PxU32 count) override {}
    void onWake(PxActor** actors, PxU32 count) override {}
    void onSleep(PxActor** actors, PxU32 count) override {}
    void onContact(const PxContactPairHeader& pairHeader, const PxContactPair* pairs, PxU32 nbPairs) override;
    void onTrigger(PxTriggerPair* pairs, PxU32 count) override {}
    void onAdvance(const PxRigidBody*const* bodyBuffer, const PxTransform* poseBuffer, const PxU32 count) override {}

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
    std::vector<BlackHoleBodyData> m_BlackHoles;
    std::unordered_map<std::string, PxConvexMesh*> m_MeshCache;
    Scene* m_CurrentScene = nullptr;
    std::vector<size_t> m_BodiesToDelete;

    void CreatePhysicsBody(PhysicsBodyData& data);
    void CreateBlackHoleBody(BlackHoleBodyData& data);
    void ApplyGravitationalForces();
    void UpdatePhysicsBodies();
    void ProcessDeletedBodies();
    PxConvexMesh* LoadConvexMesh(const std::string& path);
    float CalculateSchwarzchildRadius(float solarMass);

    static constexpr float G = 6.67430e-11f;
    static constexpr float SOLAR_MASS = 1.989e30f;
    static constexpr float C = 299792458.0f;
};