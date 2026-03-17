#include "Physics.h"

#include <algorithm>
#include <spdlog/spdlog.h>

#include "Application/Parameters.h"
#include "Renderer/Renderer.h"
#include "../Renderer/Models/GLTFMesh.h"

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

    m_Material = m_Physics->createMaterial(0.5f, 0.5f, 0.6f);

    m_Scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, 1.0f);

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

    if (m_Cooking) {
        m_Cooking->release();
        m_Cooking = nullptr;
    }

    if (m_Dispatcher) {
        m_Dispatcher->release();
        m_Dispatcher = nullptr;
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

    if (!scene) {
        return;
    }

    for (size_t i = 0; i < scene->objects.size(); ++i) {
        auto &obj = scene->objects[i];

        if (obj.HasClass("BlackHole")) {
            PhysicsBodyData bodyData;
            bodyData.sceneIndex = i;
            bodyData.isSphere = false;

            auto posHandle = Field::Entity::Position;
            auto rotHandle = Field::Entity::Rotation;
            auto radiusHandle = Field::Sphere::Radius;
            auto massHandle = Field::Physics::Mass;
            auto velHandle = Field::Physics::Velocity;

            if (obj.HasParameter(posHandle)) {
                bodyData.position = std::get<glm::vec3>(obj.GetParameter(Field::Entity::Position));
            }
            if (obj.HasParameter(rotHandle)) {
                bodyData.rotation = std::get<glm::quat>(obj.GetParameter(Field::Entity::Rotation));
            }
            if (obj.HasParameter(radiusHandle)) {
                bodyData.radius = std::get<float>(obj.GetParameter(Field::Sphere::Radius));
            }
            if (obj.HasParameter(massHandle)) {
                bodyData.mass = std::get<float>(obj.GetParameter(Field::Physics::Mass));
            }
            if (obj.HasParameter(velHandle)) {
                bodyData.initialVelocity = std::get<glm::vec3>(obj.GetParameter(Field::Physics::Velocity));
            }

            CreatePhysicsBody(bodyData);
        }
        else if (obj.HasClass("Mesh")) {
            PhysicsBodyData bodyData;
            bodyData.sceneIndex = i;
            bodyData.isSphere = false;

            auto posHandle = Field::Entity::Position;
            auto rotHandle = Field::Entity::Rotation;
            auto scaleHandle = Field::Entity::Scale;
            auto massHandle = Field::Physics::Mass;
            auto pathHandle = Field::Mesh::FilePath;
            auto velHandle = Field::Physics::Velocity;

            if (obj.HasParameter(posHandle)) {
                bodyData.position = std::get<glm::vec3>(obj.GetParameter(posHandle));
            }
            if (obj.HasParameter(rotHandle)) {
                bodyData.rotation = std::get<glm::quat>(obj.GetParameter(rotHandle));
            }
            if (obj.HasParameter(scaleHandle)) {
                bodyData.scale = std::get<glm::vec3>(obj.GetParameter(scaleHandle));
            }
            if (obj.HasParameter(massHandle)) {
                bodyData.mass = std::get<float>(obj.GetParameter(massHandle));
            }
            if (obj.HasParameter(pathHandle)) {
                bodyData.meshPath = std::get<std::string>(obj.GetParameter(pathHandle));
            }
            if (obj.HasParameter(velHandle)) {
                bodyData.initialVelocity = std::get<glm::vec3>(obj.GetParameter(velHandle));
            }

            bodyData.radius = glm::length(bodyData.scale) * 0.5f;

            CreatePhysicsBody(bodyData);
        }
        else if (obj.HasClass("Sphere")) {
            PhysicsBodyData bodyData;
            bodyData.sceneIndex = i;
            bodyData.isSphere = true;

            auto posHandle = Field::Entity::Position;
            auto rotHandle = Field::Entity::Rotation;
            auto radiusHandle = Field::Sphere::Radius;
            auto massHandle = Field::Physics::Mass;
            auto velHandle = Field::Physics::Velocity;

            if (obj.HasParameter(posHandle)) {
                bodyData.position = std::get<glm::vec3>(obj.GetParameter(Field::Entity::Position));
            }
            if (obj.HasParameter(rotHandle)) {
                bodyData.rotation = std::get<glm::quat>(obj.GetParameter(Field::Entity::Rotation));
            }
            if (obj.HasParameter(radiusHandle)) {
                bodyData.radius = std::get<float>(obj.GetParameter(Field::Sphere::Radius));
            }
            if (obj.HasParameter(massHandle)) {
                bodyData.mass = std::get<float>(obj.GetParameter(Field::Physics::Mass));
            }
            if (obj.HasParameter(velHandle)) {
                bodyData.initialVelocity = std::get<glm::vec3>(obj.GetParameter(Field::Physics::Velocity));
            }

            bodyData.scale = glm::vec3(bodyData.radius * 2.0f);

            CreatePhysicsBody(bodyData);
        }
    }

    spdlog::info("Loaded {} black holes and {} physics bodies from scene", m_BlackHoles.size(), m_Bodies.size());
}

void Physics::Apply() {
    if (!m_CurrentScene) return;

    constexpr auto posHandle = Field::Entity::Position;
    constexpr auto rotHandle = Field::Entity::Rotation;

    for (auto& body : m_Bodies) {
        if (!body.actor) continue;

        PxTransform transform = body.actor->getGlobalPose();

        body.position = glm::vec3(transform.p.x, transform.p.y, transform.p.z);
        body.rotation = glm::quat(transform.q.w, transform.q.x, transform.q.y, transform.q.z);

        if (body.sceneIndex < m_CurrentScene->objects.size()) {
            auto& obj = m_CurrentScene->objects[body.sceneIndex];
            if (obj.HasParameter(posHandle)) {
                obj.SetParameter(posHandle, body.position);
            }
            if (obj.HasParameter(rotHandle)) {
                obj.SetParameter(rotHandle, body.rotation);
            }
        }
    }

    for (auto& bh : m_BlackHoles) {
        if (!bh.actor) continue;

        PxTransform transform = bh.actor->getGlobalPose();
        bh.position = glm::vec3(transform.p.x, transform.p.y, transform.p.z);

        if (bh.sceneIndex < m_CurrentScene->objects.size()) {
            auto& obj = m_CurrentScene->objects[bh.sceneIndex];
            if (obj.HasParameter(posHandle)) {
                obj.SetParameter(posHandle, bh.position);
            }
        }
    }
}

void Physics::Update(const float deltaTime, Scene* scene) {
    ApplyGravitationalForces(deltaTime, scene);
    m_Scene->simulate(deltaTime);
    m_Scene->fetchResults(true);
    ProcessDeletedBodies();
}

void Physics::CreatePhysicsBody(PhysicsBodyData &data) {
    PxTransform transform(
        PxVec3(data.position.x, data.position.y, data.position.z),
        PxQuat(data.rotation.x, data.rotation.y, data.rotation.z, data.rotation.w)
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
            PxMeshScale meshScale(PxVec3(data.scale.x, data.scale.y, data.scale.z));
            shape = PxRigidActorExt::createExclusiveShape(*body,
                                                          PxConvexMeshGeometry(convexMesh, meshScale),
                                                          *m_Material);
        } else {
            spdlog::warn("Failed to load convex mesh for '{}', using box collider", data.meshPath);
            shape = PxRigidActorExt::createExclusiveShape(*body,
                                                          PxBoxGeometry(data.scale.x * 0.5f, data.scale.y * 0.5f, data.scale.z * 0.5f),
                                                          *m_Material);
        }
    } else {
        shape = PxRigidActorExt::createExclusiveShape(*body,
                                                      PxBoxGeometry(data.scale.x * 0.5f, data.scale.y * 0.5f, data.scale.z * 0.5f),
                                                      *m_Material);
    }

    if (!shape) {
        spdlog::error("Failed to create shape");
        body->release();
        return;
    }

    shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
    shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
    shape->setSimulationFilterData(PxFilterData(1, 1, 0, 0));

    body->setLinearVelocity(PxVec3(data.initialVelocity.x, data.initialVelocity.y, data.initialVelocity.z));

    body->setLinearDamping(0.0f);
    body->setAngularDamping(0.0f);
    PxRigidBodyExt::setMassAndUpdateInertia(*body, data.mass);

    body->setActorFlag(PxActorFlag::eVISUALIZATION, true);

    m_Scene->addActor(*body);
    data.actor = body;
    m_Bodies.push_back(data);
}


void Physics::ApplyGravitationalForces(const float dt, Scene* scene) const
{
    for (size_t i = 0; i < m_Bodies.size(); ++i) {
        if (!m_Bodies[i].actor) continue;

        PxTransform p_i = m_Bodies[i].actor->getGlobalPose();
        PxVec3 v_i = m_Bodies[i].actor->getLinearVelocity();

        for (size_t j = 0; j < m_Bodies.size(); ++j) {
            if (i == j) continue;

            PxTransform p_j = m_Bodies[j].actor->getGlobalPose();
            const float m_j = m_Bodies[j].mass;

            PxVec3 a = G * m_j / (p_j.p - p_i.p).magnitudeSquared() * (p_j.p - p_i.p).getNormalized();

            // update velocity
            v_i = v_i + a * dt;
            m_Bodies[i].actor->setLinearVelocity(v_i);
            scene->objects[i].SetParameter(Field::Physics::Velocity, glm::vec3(v_i.x, v_i.y, v_i.z));

            // update position
            p_i.p = p_i.p + v_i * dt;
            m_Bodies[i].actor->setGlobalPose(p_i);
            scene->objects[i].SetParameter(Field::Entity::Position, glm::vec3(p_i.p.x, p_i.p.y, p_i.p.z));
        }
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

    /*auto geometry = gltfMesh->GetPhysicsGeometry();

    if (geometry.vertices.empty()) {
        spdlog::error("No valid vertex data in mesh: {}", path);
        return nullptr;
    }

    std::vector<PxVec3> pxVertices;
    pxVertices.reserve(geometry.vertices.size());
    for (const auto& v : geometry.vertices) {
        pxVertices.emplace_back(v.x, v.y, v.z);
    }*/

    // PxConvexMeshDesc convexDesc;
    // convexDesc.points.count = static_cast<PxU32>(pxVertices.size());
    // convexDesc.points.stride = sizeof(PxVec3);
    // convexDesc.points.data = pxVertices.data();
    // convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;

    PxDefaultMemoryOutputStream buf;
    PxConvexMeshCookingResult::Enum result;

    // if (!m_Cooking->cookConvexMesh(convexDesc, buf, &result)) {
    //     spdlog::error("Failed to cook convex mesh for: {} (result: {})", path, static_cast<int>(result));
    //     return nullptr;
    // }

    PxDefaultMemoryInputData input(buf.getData(), buf.getSize());
    PxConvexMesh* convexMesh = m_Physics->createConvexMesh(input);

    if (!convexMesh) {
        spdlog::error("Failed to create convex mesh from cooked data for: {}", path);
        return nullptr;
    }

    m_MeshCache[path] = convexMesh;
    // spdlog::info("Successfully created convex mesh collision: {} ({} input vertices -> {} hull vertices)",
    //              path, pxVertices.size(), convexMesh->getNbVertices());

    return convexMesh;
}

void Physics::CreateBlackHoleBody(BlackHoleBodyData &data) {
    PxTransform transform(PxVec3(data.position.x, data.position.y, data.position.z));

    PxRigidStatic *blackHoleActor = m_Physics->createRigidStatic(transform);
    if (!blackHoleActor) {
        spdlog::error("Failed to create black hole static actor");
        return;
    }

    PxShape *shape = PxRigidActorExt::createExclusiveShape(*blackHoleActor,
                                                           PxSphereGeometry(std::max(1000.0f, std::min(0.0f, data.schwarzschildRadius))),
                                                           *m_Material);
    if (!shape) {
        spdlog::error("Failed to create black hole shape");
        blackHoleActor->release();
        return;
    }

    shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, true);
    shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
    shape->setFlag(PxShapeFlag::eSCENE_QUERY_SHAPE, false);
    shape->setSimulationFilterData(PxFilterData(2, 2, 0, 0));

    blackHoleActor->userData = reinterpret_cast<void*>(data.sceneIndex);

    blackHoleActor->setActorFlag(PxActorFlag::eVISUALIZATION, true);

    m_Scene->addActor(*blackHoleActor);
    data.actor = blackHoleActor;
    m_BlackHoles.push_back(data);

    spdlog::debug("Created black hole collision (index: {}, radius: {})", data.sceneIndex, data.schwarzschildRadius);
}

float Physics::CalculateSchwarzschildRadius(float solarMass) {
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

        // switch (body.objectType) {
        //     case Scene::ObjectType::Mesh:
        //         if (body.sceneIndex < m_CurrentScene->meshes.size()) {
        //             m_CurrentScene->meshes.erase(m_CurrentScene->meshes.begin() + static_cast<std::ptrdiff_t>(body.sceneIndex));
        //         }
        //         break;
        //     case Scene::ObjectType::Sphere:
        //         if (body.sceneIndex < m_CurrentScene->spheres.size()) {
        //             m_CurrentScene->spheres.erase(m_CurrentScene->spheres.begin() + static_cast<std::ptrdiff_t>(body.sceneIndex));
        //         }
        //         break;
        //     default:
        //         break;
        // }

        m_Bodies.erase(m_Bodies.begin() + static_cast<std::ptrdiff_t>(idx));
    }

    m_BodiesToDelete.clear();

    for (size_t i = 0; i < m_Bodies.size(); ++i) {
        size_t newSceneIndex = 0;
        // switch (m_Bodies[i].objectType) {
        //     case Scene::ObjectType::Mesh:
        //         for (size_t j = 0; j < i; ++j) {
        //             if (m_Bodies[j].objectType == Scene::ObjectType::Mesh) {
        //                 newSceneIndex++;
        //             }
        //         }
        //         m_Bodies[i].sceneIndex = newSceneIndex;
        //         break;
        //     case Scene::ObjectType::Sphere:
        //         for (size_t j = 0; j < i; ++j) {
        //             if (m_Bodies[j].objectType == Scene::ObjectType::Sphere) {
        //                 newSceneIndex++;
        //             }
        //         }
        //         m_Bodies[i].sceneIndex = newSceneIndex;
        //         break;
        //     default:
        //         break;
        // }
    }
}

const PxRenderBuffer* Physics::GetDebugRenderBuffer() const {
    if (!m_Scene) {
        return nullptr;
    }
    return &m_Scene->getRenderBuffer();
}

void Physics::SetVisualizationParameter(PxVisualizationParameter::Enum param, float value) {
    if (m_Scene) {
        m_Scene->setVisualizationParameter(param, value);
    }
}

void Physics::SetVisualizationScale(float scale) {
    if (m_Scene) {
        m_Scene->setVisualizationParameter(PxVisualizationParameter::eSCALE, scale);
    }
}

void Physics::ApplyDebugVisualizationFlags(uint32_t flags, float scale) {
    SetVisualizationScale(scale);

    static constexpr PxVisualizationParameter::Enum k_FlagMap[] = {
        PxVisualizationParameter::eWORLD_AXES,
        PxVisualizationParameter::eBODY_AXES,
        PxVisualizationParameter::eBODY_MASS_AXES,
        PxVisualizationParameter::eBODY_LIN_VELOCITY,
        PxVisualizationParameter::eBODY_ANG_VELOCITY,
        PxVisualizationParameter::eCONTACT_POINT,
        PxVisualizationParameter::eCONTACT_NORMAL,
        PxVisualizationParameter::eCONTACT_ERROR,
        PxVisualizationParameter::eCONTACT_FORCE,
        PxVisualizationParameter::eACTOR_AXES,
        PxVisualizationParameter::eCOLLISION_AABBS,
        PxVisualizationParameter::eCOLLISION_SHAPES,
        PxVisualizationParameter::eCOLLISION_AXES,
        PxVisualizationParameter::eCOLLISION_COMPOUNDS,
        PxVisualizationParameter::eCOLLISION_FNORMALS,
        PxVisualizationParameter::eCOLLISION_EDGES,
        PxVisualizationParameter::eCOLLISION_STATIC,
        PxVisualizationParameter::eCOLLISION_DYNAMIC,
    };

    for (uint32_t i = 0; i < std::size(k_FlagMap); ++i) {
        SetVisualizationParameter(k_FlagMap[i], (flags & (1u << i)) ? 1.0f : 0.0f);
    }
}

