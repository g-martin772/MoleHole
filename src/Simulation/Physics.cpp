#include "Physics.h"

#include <algorithm>
#include <spdlog/spdlog.h>
#include "Renderer/Renderer.h"
#include "Renderer/GLTFMesh.h"

static PxFilterFlags BlackHoleFilterShader(
    PxFilterObjectAttributes attributes0, PxFilterData filterData0,
    PxFilterObjectAttributes attributes1, PxFilterData filterData1,
    PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
{
    if (PxFilterObjectIsTrigger(attributes0) || PxFilterObjectIsTrigger(attributes1)) {
        pairFlags = PxPairFlag::eTRIGGER_DEFAULT;
        return PxFilterFlag::eDEFAULT;
    }

    pairFlags = PxPairFlag::eCONTACT_DEFAULT;

    if ((filterData0.word0 == 2 && filterData1.word0 == 1) ||
        (filterData0.word0 == 1 && filterData1.word0 == 2)) {
        pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
    }

    return PxFilterFlag::eDEFAULT;
}

void Physics::Init() {
    spdlog::info("Initializing PhysX...");

    m_Foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_Allocator, m_ErrorCallback);
    if (!m_Foundation) {
        spdlog::error("PxCreateFoundation failed!");
        return;
    }

    m_pvd = PxCreatePvd(*m_Foundation);
    if (m_pvd) {
        PxPvdTransport *transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
        m_pvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
    }

    m_Physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_Foundation, PxTolerancesScale(), true, m_pvd);
    if (!m_Physics) {
        spdlog::error("PxCreatePhysics failed!");
        return;
    }

    m_Cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_Foundation, PxCookingParams(PxTolerancesScale()));
    if (!m_Cooking) {
        spdlog::error("PxCreateCooking failed!");
        return;
    }

    m_Dispatcher = PxDefaultCpuDispatcherCreate(2);

    PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, 0.0f, 0.0f);
    sceneDesc.cpuDispatcher = m_Dispatcher;
    sceneDesc.filterShader = BlackHoleFilterShader;
    sceneDesc.simulationEventCallback = this;

    m_Scene = m_Physics->createScene(sceneDesc);
    if (!m_Scene) {
        spdlog::error("createScene failed!");
        return;
    }

    m_Material = m_Physics->createMaterial(0.5f, 0.5f, 0.6f); // static friction, dynamic friction, restitution

    spdlog::info("PhysX initialized successfully");
}

void Physics::Shutdown() {
    if (m_Scene) {
        for (auto &body: m_Bodies) {
            if (body.actor) {
                m_Scene->removeActor(*body.actor);
                body.actor->release();
            }
        }
        m_Bodies.clear();

        for (auto &bh: m_BlackHoles) {
            if (bh.actor) {
                m_Scene->removeActor(*bh.actor);
                bh.actor->release();
            }
        }
        m_BlackHoles.clear();

        m_Scene->release();
        m_Scene = nullptr;
    }

    for (auto& [path, mesh] : m_MeshCache) {
        if (mesh) {
            mesh->release();
        }
    }
    m_MeshCache.clear();

    if (m_Material) {
        m_Material->release();
        m_Material = nullptr;
    }

    if (m_Dispatcher) {
        m_Dispatcher->release();
        m_Dispatcher = nullptr;
    }

    if (m_Cooking) {
        m_Cooking->release();
        m_Cooking = nullptr;
    }

    if (m_Physics) {
        m_Physics->release();
        m_Physics = nullptr;
    }

    if (m_pvd) {
        PxPvdTransport *transport = m_pvd->getTransport();
        m_pvd->release();
        if (transport) transport->release();
        m_pvd = nullptr;
    }

    if (m_Foundation) {
        m_Foundation->release();
        m_Foundation = nullptr;
    }

    spdlog::info("PhysX shutdown complete");
}

void Physics::SetScene(Scene *scene) {
    m_CurrentScene = scene;

    for (auto &body: m_Bodies) {
        if (body.actor) {
            m_Scene->removeActor(*body.actor);
            body.actor->release();
        }
    }
    m_Bodies.clear();

    for (auto &bh: m_BlackHoles) {
        if (bh.actor) {
            m_Scene->removeActor(*bh.actor);
            bh.actor->release();
        }
    }
    m_BlackHoles.clear();

    for (size_t i = 0; i < scene->blackHoles.size(); ++i) {
        auto &blackHole = scene->blackHoles[i];
        BlackHoleBodyData data;
        data.scenePosition = &blackHole.position;
        data.schwarzschildRadius = CalculateSchwarzchildRadius(blackHole.mass);
        data.sceneIndex = i;

        CreateBlackHoleBody(data);
    }

    for (size_t i = 0; i < scene->meshes.size(); ++i) {
        auto &mesh = scene->meshes[i];
        PhysicsBodyData data;
        data.scenePosition = &mesh.position;
        data.sceneRotation = &mesh.rotation;
        data.sceneScale = &mesh.scale;
        data.radius = 0.0f;
        data.isSphere = false;
        data.mass = mesh.massKg;
        data.meshPath = mesh.path;
        data.initialVelocity = mesh.velocity;
        data.sceneIndex = i;
        data.objectType = Scene::ObjectType::Mesh;

        CreatePhysicsBody(data);
    }

    for (size_t i = 0; i < scene->spheres.size(); ++i) {
        auto &sphere = scene->spheres[i];
        PhysicsBodyData data;
        data.scenePosition = &sphere.position;
        data.sceneRotation = &sphere.rotation;
        data.sceneScale = nullptr;
        data.radius = sphere.radius;
        data.isSphere = true;
        data.initialVelocity = sphere.velocity;
        data.mass = sphere.massKg;
        data.sceneIndex = i;
        data.objectType = Scene::ObjectType::Sphere;

        CreatePhysicsBody(data);
    }

    spdlog::info("Loaded {} black holes and {} physics bodies from scene", m_BlackHoles.size(), m_Bodies.size());
}

void Physics::Apply() {
    for (auto& body : m_Bodies) {
        if (!body.actor || !body.scenePosition || !body.sceneRotation) continue;

        PxTransform transform = body.actor->getGlobalPose();

        *body.scenePosition = glm::vec3(transform.p.x, transform.p.y, transform.p.z);
        *body.sceneRotation = glm::quat(transform.q.w, transform.q.x, transform.q.y, transform.q.z);
    }
}

void Physics::Update(float deltaTime) {
    ApplyGravitationalForces();
    UpdatePhysicsBodies();
    m_Scene->simulate(deltaTime);
    m_Scene->fetchResults(true);
    ProcessDeletedBodies();
}

void Physics::CreatePhysicsBody(PhysicsBodyData &data) {
    PxTransform transform(
        PxVec3(data.scenePosition->x, data.scenePosition->y, data.scenePosition->z),
        PxQuat(data.sceneRotation->x, data.sceneRotation->y, data.sceneRotation->z, data.sceneRotation->w)
    );

    PxRigidDynamic *body = m_Physics->createRigidDynamic(transform);
    if (!body) {
        spdlog::error("Failed to create rigid dynamic");
        return;
    }

    PxShape *shape = nullptr;
    if (data.isSphere) {
        shape = PxRigidActorExt::createExclusiveShape(*body, PxSphereGeometry(data.radius), *m_Material);
    } else if (!data.meshPath.empty()) {
        PxConvexMesh* convexMesh = LoadConvexMesh(data.meshPath);
        if (convexMesh) {
            glm::vec3 scale = data.sceneScale ? *data.sceneScale : glm::vec3(1.0f);
            PxMeshScale meshScale(PxVec3(scale.x, scale.y, scale.z));
            shape = PxRigidActorExt::createExclusiveShape(*body,
                                                          PxConvexMeshGeometry(convexMesh, meshScale),
                                                          *m_Material);
        } else {
            spdlog::warn("Failed to load convex mesh for '{}', using box collider", data.meshPath);
            glm::vec3 scale = data.sceneScale ? *data.sceneScale : glm::vec3(1.0f);
            shape = PxRigidActorExt::createExclusiveShape(*body,
                                                          PxBoxGeometry(scale.x * 0.5f, scale.y * 0.5f, scale.z * 0.5f),
                                                          *m_Material);
        }
    } else {
        glm::vec3 scale = data.sceneScale ? *data.sceneScale : glm::vec3(1.0f);
        shape = PxRigidActorExt::createExclusiveShape(*body,
                                                      PxBoxGeometry(scale.x * 0.5f, scale.y * 0.5f, scale.z * 0.5f),
                                                      *m_Material);
    }

    if (!shape) {
        spdlog::error("Failed to create shape");
        body->release();
        return;
    }

    shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
    shape->setSimulationFilterData(PxFilterData(1, 1, 0, 0));

    body->setLinearVelocity(PxVec3(data.initialVelocity.x, data.initialVelocity.y, data.initialVelocity.z));

    body->setLinearDamping(0.0f);
    body->setAngularDamping(0.0f);
    PxRigidBodyExt::setMassAndUpdateInertia(*body, data.mass);

    m_Scene->addActor(*body);
    data.actor = body;
    m_Bodies.push_back(data);
}


void Physics::ApplyGravitationalForces() {
    for (size_t i = 0; i < m_Bodies.size(); ++i) {
        if (!m_Bodies[i].actor) continue;

        PxVec3 totalForce(0.0f, 0.0f, 0.0f);
        PxTransform transform_i = m_Bodies[i].actor->getGlobalPose();
        float mass_i = m_Bodies[i].actor->getMass();

        for (size_t j = 0; j < m_Bodies.size(); ++j) {
            if (i == j || !m_Bodies[j].actor) continue;

            PxTransform transform_j = m_Bodies[j].actor->getGlobalPose();
            float mass_j = m_Bodies[j].actor->getMass();

            PxVec3 direction = transform_j.p - transform_i.p;
            float distSq = direction.magnitudeSquared();

            float minDist = (m_Bodies[i].radius + m_Bodies[j].radius) * 0.5f;
            if (distSq < minDist * minDist) {
                distSq = minDist * minDist;
            }

            direction = direction.getNormalized();

            // F = G * m1 * m2 / r^2
            float forceMagnitude = G * mass_i * mass_j / distSq;

            totalForce += direction * forceMagnitude;
        }

        for (size_t j = 0; j < m_BlackHoles.size(); ++j) {
            if (!m_BlackHoles[j].actor || !m_BlackHoles[j].scenePosition) continue;

            PxTransform bhTransform = m_BlackHoles[j].actor->getGlobalPose();

            PxVec3 direction = bhTransform.p - transform_i.p;
            float distSq = direction.magnitudeSquared();

            float minDist = m_BlackHoles[j].schwarzschildRadius;
            if (distSq < minDist * minDist) {
                distSq = minDist * minDist;
            }

            direction = direction.getNormalized();

            float bhMassKg = (m_BlackHoles[j].schwarzschildRadius * C * C) / (2.0f * G);
            float forceMagnitude = G * mass_i * bhMassKg / distSq;

            totalForce += direction * forceMagnitude;
        }

        m_Bodies[i].actor->addForce(totalForce, PxForceMode::eFORCE);
    }
}

void Physics::UpdatePhysicsBodies() {

}

PxConvexMesh* Physics::LoadConvexMesh(const std::string& path) {
    auto it = m_MeshCache.find(path);
    if (it != m_MeshCache.end()) {
        return it->second;
    }

    spdlog::debug("Creating convex mesh collision for: {}", path);

    auto gltfMesh = m_Renderer->m_meshCache[path];
    if (!gltfMesh || !gltfMesh->IsLoaded()) {
        spdlog::error("Mesh not loaded in renderer: {}", path);
        return nullptr;
    }

    auto geometry = gltfMesh->GetPhysicsGeometry();

    if (geometry.vertices.empty()) {
        spdlog::error("No valid vertex data in mesh: {}", path);
        return nullptr;
    }

    std::vector<PxVec3> pxVertices;
    pxVertices.reserve(geometry.vertices.size());
    for (const auto& v : geometry.vertices) {
        pxVertices.emplace_back(v.x, v.y, v.z);
    }

    PxConvexMeshDesc convexDesc;
    convexDesc.points.count = static_cast<PxU32>(pxVertices.size());
    convexDesc.points.stride = sizeof(PxVec3);
    convexDesc.points.data = pxVertices.data();
    convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX | PxConvexFlag::eQUANTIZE_INPUT;
    convexDesc.vertexLimit = 255;

    PxDefaultMemoryOutputStream writeBuffer;
    PxConvexMeshCookingResult::Enum result;
    bool success = m_Cooking->cookConvexMesh(convexDesc, writeBuffer, &result);

    if (!success || result != PxConvexMeshCookingResult::eSUCCESS) {
        spdlog::error("Failed to cook convex mesh for: {} (result: {})", path, static_cast<int>(result));
        return nullptr;
    }

    PxDefaultMemoryInputData readBuffer(writeBuffer.getData(), writeBuffer.getSize());
    PxConvexMesh* convexMesh = m_Physics->createConvexMesh(readBuffer);

    if (!convexMesh) {
        spdlog::error("Failed to create convex mesh for: {}", path);
        return nullptr;
    }

    m_MeshCache[path] = convexMesh;
    spdlog::info("Successfully created convex mesh collision: {} ({} input vertices -> {} hull vertices)",
                 path, pxVertices.size(), convexMesh->getNbVertices());

    return convexMesh;
}

void Physics::CreateBlackHoleBody(BlackHoleBodyData &data) {
    PxTransform transform(PxVec3(data.scenePosition->x, data.scenePosition->y, data.scenePosition->z));

    PxRigidStatic *blackHoleActor = m_Physics->createRigidStatic(transform);
    if (!blackHoleActor) {
        spdlog::error("Failed to create black hole static actor");
        return;
    }

    PxShape *shape = PxRigidActorExt::createExclusiveShape(*blackHoleActor,
                                                           PxSphereGeometry(data.schwarzschildRadius),
                                                           *m_Material);
    if (!shape) {
        spdlog::error("Failed to create black hole shape");
        blackHoleActor->release();
        return;
    }

    shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
    shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
    shape->setSimulationFilterData(PxFilterData(2, 2, 0, 0));

    blackHoleActor->userData = reinterpret_cast<void*>(data.sceneIndex);

    m_Scene->addActor(*blackHoleActor);
    data.actor = blackHoleActor;
    m_BlackHoles.push_back(data);

    spdlog::debug("Created black hole collision (index: {}, radius: {})", data.sceneIndex, data.schwarzschildRadius);
}

float Physics::CalculateSchwarzchildRadius(float solarMass) {
    float massKg = solarMass * SOLAR_MASS;
    return (2.0f * G * massKg) / (C * C);
}

void Physics::onContact(const PxContactPairHeader& pairHeader, const PxContactPair* pairs, PxU32 nbPairs) {
    for (PxU32 i = 0; i < nbPairs; i++) {
        const PxContactPair& contactPair = pairs[i];

        if (!(contactPair.events & PxPairFlag::eNOTIFY_TOUCH_FOUND)) {
            continue;
        }

        PxActor* actor0 = pairHeader.actors[0];
        PxActor* actor1 = pairHeader.actors[1];

        bool isBlackHole0 = actor0->is<PxRigidStatic>() != nullptr;
        bool isBlackHole1 = actor1->is<PxRigidStatic>() != nullptr;

        PxRigidDynamic* dynamicBody = nullptr;

        if (isBlackHole0 && !isBlackHole1) {
            dynamicBody = actor1->is<PxRigidDynamic>();
        } else if (isBlackHole1 && !isBlackHole0) {
            dynamicBody = actor0->is<PxRigidDynamic>();
        }

        if (dynamicBody) {
            for (size_t j = 0; j < m_Bodies.size(); ++j) {
                if (m_Bodies[j].actor == dynamicBody) {
                    m_BodiesToDelete.push_back(j);
                    spdlog::info("Object collided with black hole - marking for deletion");
                    break;
                }
            }
        }
    }
}

void Physics::ProcessDeletedBodies() {
    if (m_BodiesToDelete.empty() || !m_CurrentScene) return;

    std::sort(m_BodiesToDelete.rbegin(), m_BodiesToDelete.rend());

    for (size_t idx : m_BodiesToDelete) {
        if (idx >= m_Bodies.size()) continue;

        auto& body = m_Bodies[idx];

        if (body.actor) {
            m_Scene->removeActor(*body.actor);
            body.actor->release();
        }

        switch (body.objectType) {
            case Scene::ObjectType::Mesh:
                if (body.sceneIndex < m_CurrentScene->meshes.size()) {
                    m_CurrentScene->meshes.erase(m_CurrentScene->meshes.begin() + static_cast<std::ptrdiff_t>(body.sceneIndex));
                }
                break;
            case Scene::ObjectType::Sphere:
                if (body.sceneIndex < m_CurrentScene->spheres.size()) {
                    m_CurrentScene->spheres.erase(m_CurrentScene->spheres.begin() + static_cast<std::ptrdiff_t>(body.sceneIndex));
                }
                break;
            default:
                break;
        }

        m_Bodies.erase(m_Bodies.begin() + static_cast<std::ptrdiff_t>(idx));
    }

    m_BodiesToDelete.clear();

    for (size_t i = 0; i < m_Bodies.size(); ++i) {
        size_t newSceneIndex = 0;
        switch (m_Bodies[i].objectType) {
            case Scene::ObjectType::Mesh:
                for (size_t j = 0; j < i; ++j) {
                    if (m_Bodies[j].objectType == Scene::ObjectType::Mesh) {
                        newSceneIndex++;
                    }
                }
                m_Bodies[i].sceneIndex = newSceneIndex;
                break;
            case Scene::ObjectType::Sphere:
                for (size_t j = 0; j < i; ++j) {
                    if (m_Bodies[j].objectType == Scene::ObjectType::Sphere) {
                        newSceneIndex++;
                    }
                }
                m_Bodies[i].sceneIndex = newSceneIndex;
                break;
            default:
                break;
        }
    }
}
