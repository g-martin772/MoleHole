#include "Physics.h"

#include <spdlog/spdlog.h>

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

    m_Dispatcher = PxDefaultCpuDispatcherCreate(2);

    PxSceneDesc sceneDesc(m_Physics->getTolerancesScale());
    sceneDesc.gravity = PxVec3(0.0f, 0.0f, 0.0f);
    sceneDesc.cpuDispatcher = m_Dispatcher;
    sceneDesc.filterShader = PxDefaultSimulationFilterShader;

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

        m_Scene->release();
        m_Scene = nullptr;
    }

    if (m_Material) {
        m_Material->release();
        m_Material = nullptr;
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
    for (auto &body: m_Bodies) {
        if (body.actor) {
            m_Scene->removeActor(*body.actor);
            body.actor->release();
        }
    }
    m_Bodies.clear();

    // Meshes
    for (auto &mesh: scene->meshes) {
        PhysicsBodyData data;
        data.scenePosition = &mesh.position;
        data.sceneRotation = &mesh.rotation;
        data.sceneScale = &mesh.scale;
        data.radius = 0.0f;
        data.isSphere = false;
        data.mass = mesh.massKg;

        CreatePhysicsBody(data);
    }

    // Spheres
    for (auto &sphere: scene->spheres) {
        PhysicsBodyData data;
        data.scenePosition = &sphere.position;
        data.sceneRotation = &sphere.rotation;
        data.sceneScale = nullptr;
        data.radius = sphere.radius;
        data.isSphere = true;
        data.mass = sphere.massKg;

        CreatePhysicsBody(data);
    }

    spdlog::info("Loaded {} physics bodies from scene", m_Bodies.size());
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

    PxRigidBodyExt::setMassAndUpdateInertia(*body, data.mass);

    m_Scene->addActor(*body);
    data.actor = body;
    m_Bodies.push_back(data);
}


void Physics::ApplyGravitationalForces() {
    if (m_Bodies.size() < 2) return;

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

            float dist = sqrtf(distSq);
            direction = direction.getNormalized();

            // F = G * m1 * m2 / r^2
            float forceMagnitude = G * mass_i * mass_j / distSq;

            totalForce += direction * forceMagnitude;
        }

        m_Bodies[i].actor->addForce(totalForce, PxForceMode::eFORCE);
    }
}

void Physics::UpdatePhysicsBodies() {
    for (auto& body : m_Bodies) {
        if (!body.actor) continue;

        body.actor->setLinearDamping(0.1f);
        body.actor->setAngularDamping(0.1f);
    }
}
