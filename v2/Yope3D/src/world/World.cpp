#include "World.h"
#include "../gpu/GpuDevice.h"
#include "../physics/ColliderDiscrete.h"
#include "../physics/IslandDetector.h"
#include "../physics/ThreadPool.h"
#include "../physics/PhysicsConstants.h"
#include "../ecs/Components.h"
#include <cmath>
#include <cfloat>
#include <thread>
#include <algorithm>
#include <mutex>
#include <variant>

// ---- Local math helpers ----

static math::Mat3 quatToMat3(const math::Quat& q) {
    float xx = q.x*q.x, yy = q.y*q.y, zz = q.z*q.z;
    float xy = q.x*q.y, xz = q.x*q.z, yz = q.y*q.z;
    float wx = q.w*q.x, wy = q.w*q.y, wz = q.w*q.z;
    math::Mat3 m;
    // column-major: m[col*3 + row]
    m.m[0] = 1-2*(yy+zz); m.m[1] = 2*(xy+wz);   m.m[2] = 2*(xz-wy);
    m.m[3] = 2*(xy-wz);   m.m[4] = 1-2*(xx+zz); m.m[5] = 2*(yz+wx);
    m.m[6] = 2*(xz+wy);   m.m[7] = 2*(yz-wx);   m.m[8] = 1-2*(xx+yy);
    return m;
}

static math::Quat normalizeQuat(math::Quat q) {
    float lenSq = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    if (lenSq > 1e-12f) {
        float inv = 1.0f / std::sqrt(lenSq);
        q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    }
    return q;
}

// ---- Primitive type detection (for raytracer parametric intersection) ----

static void setPrimitiveInfo(RenderMesh* rm, const LoadedMesh& mesh) {
    if (mesh.name == "Icosphere" || mesh.name == "Sphere") {
        rm->primitiveType = (mesh.name == "Icosphere") ? PrimitiveType::Icosphere
                                                       : PrimitiveType::Sphere;
        float maxR2 = 0.f;
        for (const auto& v : mesh.vertices)
            maxR2 = std::max(maxR2, v.position[0]*v.position[0]
                                  + v.position[1]*v.position[1]
                                  + v.position[2]*v.position[2]);
        rm->primitiveExtents = {std::sqrt(maxR2), 0.f, 0.f};
    } else if (mesh.name == "Rect" || mesh.name == "Cube") {
        rm->primitiveType = (mesh.name == "Rect") ? PrimitiveType::Rect : PrimitiveType::Cube;
        math::Vec3 e{};
        for (const auto& v : mesh.vertices) {
            e.x = std::max(e.x, std::abs(v.position[0]));
            e.y = std::max(e.y, std::abs(v.position[1]));
            e.z = std::max(e.z, std::abs(v.position[2]));
        }
        rm->primitiveExtents = e;
    } else if (mesh.name == "Plane") {
        rm->primitiveType = PrimitiveType::Plane;
        float hx = 0.f;
        for (const auto& v : mesh.vertices)
            hx = std::max(hx, std::abs(v.position[0]));
        rm->primitiveExtents = {hx, 0.f, 0.f};
    }

    if (rm->primitiveType != PrimitiveType::Custom) {
        rm->cpuVertices.clear(); rm->cpuVertices.shrink_to_fit();
        rm->cpuIndices.clear();  rm->cpuIndices.shrink_to_fit();
    }
}

// ---- World lifecycle ----

World::World()  = default;
World::~World() = default;

void World::init(GpuDevice& gpu, VkCommandPool pool) {
    gpu_  = &gpu;
    pool_ = pool;
}

int World::getThreadCount() const {
    return threadPool_ ? static_cast<int>(threadPool_->size()) : 0;
}

// ---- Cache management ----

void World::rebuildCaches() {
    meshCache_.clear();
    for (auto& m : meshPool_)
        if (m) meshCache_.push_back(m.get());
}

int World::getHullCount() {
    int count = 0;
    for (auto [e, hc] : registry_.view<ecs::Hull>())
        ++count;
    return count;
}

// ---- Physics-only factory methods ----

ecs::Entity World::addSphere(float mass, float radius, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, {radius, radius, radius}});
    ecs::Hull hc;
    hc.mass        = mass;
    hc.inverseMass = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    float invI = (mass > 0.0f && radius > 0.0f)
                 ? 1.0f / (0.4f * mass * radius * radius) : 0.0f;
    hc.inverseInertia = math::Mat3::scale({invI, invI, invI});
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::SphereForm>(e, {radius});
    return e;
}

ecs::Entity World::addOBB(math::Vec3 extent, float mass, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, extent});
    ecs::Hull hc;
    hc.mass        = mass;
    hc.inverseMass = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    if (mass > 0.0f) {
        float ex = extent.x, ey = extent.y, ez = extent.z;
        hc.inverseInertia = math::Mat3::scale({
            12.0f / (mass * (ey*ey + ez*ez)),
            12.0f / (mass * (ex*ex + ez*ez)),
            12.0f / (mass * (ex*ex + ey*ey))
        });
    }
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::OBBForm>(e, {extent});
    return e;
}

ecs::Entity World::addAABB(math::Vec3 extent, float mass, math::Vec3 pos) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, extent});
    ecs::Hull hc;
    hc.mass           = mass;
    hc.inverseMass    = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    hc.inverseInertia = math::Mat3::scale({0,0,0});  // AABB has no angular dynamics
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::AABBForm>(e, {extent});
    return e;
}

ecs::Entity World::addStaticAABB(math::Vec3 pos, math::Vec3 extent) {
    std::lock_guard lk(structureMtx_);
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, extent});
    ecs::Hull hc;
    hc.mass           = 0.0f;
    hc.inverseMass    = 0.0f;
    hc.inverseInertia = math::Mat3::scale({0,0,0});
    hc.gravity        = false;
    registry_.add<ecs::Hull>(e, hc);
    registry_.add<ecs::AABBForm>(e, {extent});
    registry_.add<ecs::Fixed>(e);
    return e;
}

ecs::Entity World::addOBBFromMesh(const LoadedMesh& loadedMesh, float mass) {
    math::Vec3 mn = { FLT_MAX,  FLT_MAX,  FLT_MAX };
    math::Vec3 mx = {-FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const auto& v : loadedMesh.vertices) {
        mn.x = std::min(mn.x, v.position[0]);
        mn.y = std::min(mn.y, v.position[1]);
        mn.z = std::min(mn.z, v.position[2]);
        mx.x = std::max(mx.x, v.position[0]);
        mx.y = std::max(mx.y, v.position[1]);
        mx.z = std::max(mx.z, v.position[2]);
    }
    math::Vec3 center     = (mn + mx) * 0.5f;
    math::Vec3 halfExtent = (mx - mn) * 0.5f;
    return addOBB(halfExtent, mass, center);
}

// ---- Visual-only factory methods ----

ecs::Entity World::addRenderObject(const std::vector<Vertex>& vertices,
                                    const std::vector<uint32_t>& indices) {
    std::lock_guard lk(structureMtx_);
    auto rm = std::make_unique<RenderMesh>(*gpu_, pool_, vertices, indices);
    rm->transformReady = true;
    RenderMesh* raw = rm.get();
    meshPool_.push_back(std::move(rm));
    rebuildCaches();
    ecs::Entity e = registry_.create();
    registry_.add<Transform>(e);
    registry_.add<ecs::MeshRenderer>(e, {raw});
    meshToEntity_[raw] = e;
    return e;
}

ecs::Entity World::addRenderObject(const LoadedMesh& mesh) {
    ecs::Entity e = addRenderObject(mesh.vertices, mesh.indices);
    if (auto* mr = registry_.get<ecs::MeshRenderer>(e))
        setPrimitiveInfo(mr->mesh, mesh);
    return e;
}

// ---- Attach mesh to existing entity ----

RenderMesh* World::attachMesh(ecs::Entity e,
                               const std::vector<Vertex>& vertices,
                               const std::vector<uint32_t>& indices) {
    std::lock_guard lk(structureMtx_);
    if (!registry_.valid(e)) return nullptr;
    auto rm = std::make_unique<RenderMesh>(*gpu_, pool_, vertices, indices);
    RenderMesh* raw = rm.get();
    // Physics entities get transformReady set by publishSnapshot (to avoid 0,0,0 flicker).
    // Render-only entities are ready immediately.
    if (!registry_.has<ecs::Hull>(e))
        raw->transformReady = true;
    meshPool_.push_back(std::move(rm));
    rebuildCaches();
    if (registry_.has<ecs::MeshRenderer>(e))
        registry_.get<ecs::MeshRenderer>(e)->mesh = raw;
    else
        registry_.add<ecs::MeshRenderer>(e, {raw});
    meshToEntity_[raw] = e;
    return raw;
}

RenderMesh* World::attachMesh(ecs::Entity e, const LoadedMesh& mesh) {
    RenderMesh* rm = attachMesh(e, mesh.vertices, mesh.indices);
    if (rm) setPrimitiveInfo(rm, mesh);
    return rm;
}

// ---- Entity/mesh accessors ----

RenderMesh* World::getMesh(ecs::Entity e) {
    auto* mr = registry_.get<ecs::MeshRenderer>(e);
    return mr ? mr->mesh : nullptr;
}

// ---- Remove entity ----

void World::removeEntity(ecs::Entity e) {
    if (!registry_.valid(e)) return;
    std::lock_guard lk(structureMtx_);

    RenderMesh* mesh = nullptr;
    if (auto* mr = registry_.get<ecs::MeshRenderer>(e)) mesh = mr->mesh;

    // Remove springs that reference this entity; recursively remove their visual/proxy entities.
    std::vector<ecs::Entity> dependent;
    springs_.erase(std::remove_if(springs_.begin(), springs_.end(),
        [&](const std::unique_ptr<physics::Spring>& s) {
            if (s->first_ == e || s->second_ == e) {
                if (registry_.valid(s->visualMeshEntity_) && s->visualMeshEntity_ != e)
                    dependent.push_back(s->visualMeshEntity_);
                for (ecs::Entity p : s->proxies_)
                    if (registry_.valid(p) && p != e) dependent.push_back(p);
                return true;
            }
            return false;
        }), springs_.end());

    for (ecs::Entity dep : dependent) removeEntity(dep);

    // Remove proxy back-references in surviving springs.
    for (auto& s : springs_)
        s->proxies_.erase(std::remove(s->proxies_.begin(), s->proxies_.end(), e), s->proxies_.end());

    // Purge contact cache entries keyed by this entity.
    std::erase_if(contactCache_,
        [e](const auto& kv) { return kv.first.a == e || kv.first.b == e; });

    if (mesh) meshToEntity_.erase(mesh);

    // GPU sync before freeing Vulkan buffers.
    if (mesh) gpu_->syncDevice();

    // Erase mesh from pool and destroy GPU resources.
    if (mesh) {
        auto it = std::find_if(meshPool_.begin(), meshPool_.end(),
            [mesh](const std::unique_ptr<RenderMesh>& m) { return m.get() == mesh; });
        if (it != meshPool_.end()) {
            (*it)->destroy(gpu_->device());
            meshPool_.erase(it);
        }
    }

    registry_.destroy(e);
    rebuildCaches();

    if (debugPhysics) {
        destroyDebugMeshes();
        rebuildDebugMeshes();
    }
}

// ---- Springs ----

physics::Spring* World::addSpring(ecs::Entity a, ecs::Entity b, float k, float rest) {
    std::lock_guard lk(structureMtx_);
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    ecs::Entity springEnt = registry_.create();
    registry_.add<ecs::SpringConstraint>(springEnt, {b, k, rest});
    return springs_.back().get();
}

physics::Spring* World::addSpringWithProxies(ecs::Entity a, ecs::Entity b,
                                              float k, float rest,
                                              int proxyCount, float proxyRadius) {
    std::lock_guard lk(structureMtx_);
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    physics::Spring* s = springs_.back().get();

    math::Vec3 posA{}, posB{};
    if (auto* tf = registry_.get<Transform>(a)) posA = tf->position;
    if (auto* tf = registry_.get<Transform>(b)) posB = tf->position;

    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        ecs::Entity proxyEnt = addSphere(1.0f, proxyRadius, posA + (posB - posA) * t);
        if (auto* phc = registry_.get<ecs::Hull>(proxyEnt)) {
            phc->inverseMass    = 0.0f;
            phc->inverseInertia = math::Mat3::scale({0,0,0});
            phc->velocity       = {};
            phc->omega          = {};
            phc->gravity        = false;
        }
        if (!registry_.has<ecs::Fixed>(proxyEnt))
            registry_.add<ecs::Fixed>(proxyEnt);
        s->proxies_.push_back(proxyEnt);
    }
    ecs::Entity springEnt = registry_.create();
    registry_.add<ecs::SpringConstraint>(springEnt, {b, k, rest});
    return s;
}

physics::Spring* World::addSpringWithMesh(ecs::Entity a, ecs::Entity b,
                                          float k, float rest, int coils,
                                          float coilRadius, float tubeRadius,
                                          int proxyCount, float proxyRadius) {
    std::lock_guard lk(structureMtx_);
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    physics::Spring* s = springs_.back().get();

    auto [verts, inds] = physics::Spring::generateCoilMesh(coils, coilRadius, tubeRadius);
    ecs::Entity coilEnt = addRenderObject(verts, inds);
    s->visualMeshEntity_ = coilEnt;
    auto [spos, srot, sscale] = s->computeSpringTransform(registry_);
    if (auto* m = getMesh(coilEnt))
        m->modelMatrix = Transform{spos, srot, sscale}.getModelMatrix();

    math::Vec3 posA{}, posB{};
    if (auto* tf = registry_.get<Transform>(a)) posA = tf->position;
    if (auto* tf = registry_.get<Transform>(b)) posB = tf->position;

    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        ecs::Entity proxyEnt = addSphere(1.0f, proxyRadius, posA + (posB - posA) * t);
        if (auto* phc = registry_.get<ecs::Hull>(proxyEnt)) {
            phc->inverseMass    = 0.0f;
            phc->inverseInertia = math::Mat3::scale({0,0,0});
            phc->velocity       = {};
            phc->omega          = {};
            phc->gravity        = false;
        }
        if (!registry_.has<ecs::Fixed>(proxyEnt))
            registry_.add<ecs::Fixed>(proxyEnt);
        s->proxies_.push_back(proxyEnt);
    }
    ecs::Entity springEnt = registry_.create();
    registry_.add<ecs::SpringConstraint>(springEnt, {b, k, rest});
    if (auto* m = getMesh(coilEnt)) registry_.add<ecs::MeshRenderer>(springEnt, {m});
    return s;
}

// ---- Lights ----

void World::addLight(const Light& light) {
    ecs::LightSource ls;
    std::visit([&](const auto& l) {
        using T = std::decay_t<decltype(l)>;
        if constexpr (std::is_same_v<T, PointLight>) {
            ls.type = 0;
            std::copy(l.color, l.color + 3, ls.color);
            ls.intensity = l.intensity;
            std::copy(l.position, l.position + 3, ls.position);
            ls.constant = l.constant; ls.linear = l.linear; ls.quadratic = l.quadratic;
        } else if constexpr (std::is_same_v<T, DirectionalLight>) {
            ls.type = 1;
            std::copy(l.color, l.color + 3, ls.color);
            ls.intensity = l.intensity;
            std::copy(l.direction, l.direction + 3, ls.direction);
        } else if constexpr (std::is_same_v<T, SpotLight>) {
            ls.type = 2;
            std::copy(l.color, l.color + 3, ls.color);
            ls.intensity = l.intensity;
            std::copy(l.position, l.position + 3, ls.position);
            std::copy(l.direction, l.direction + 3, ls.direction);
            ls.constant = l.constant; ls.linear = l.linear; ls.quadratic = l.quadratic;
            ls.innerConeAngle = l.innerConeAngle; ls.outerConeAngle = l.outerConeAngle;
        } else if constexpr (std::is_same_v<T, FlashLight>) {
            ls.type = 3;
            std::copy(l.color, l.color + 3, ls.color);
            ls.intensity = l.intensity;
            ls.constant = l.constant; ls.linear = l.linear; ls.quadratic = l.quadratic;
            ls.innerConeAngle = l.innerConeAngle; ls.outerConeAngle = l.outerConeAngle;
        }
    }, light);

    ecs::Entity e = registry_.create();
    registry_.add<ecs::LightSource>(e, ls);
    lightEntities_.push_back(e);
}

void World::removeLight(int index) {
    if (index >= 0 && index < static_cast<int>(lightEntities_.size())) {
        registry_.destroy(lightEntities_[index]);
        lightEntities_.erase(lightEntities_.begin() + index);
    }
}

// ---- resetPhysics ----

void World::resetPhysics() {
    std::lock_guard lk(structureMtx_);
    if (gpu_) gpu_->syncDevice();

    destroyDebugMeshes();

    for (auto& m : meshPool_)
        if (m) m->destroy(gpu_->device());
    meshPool_.clear();
    springs_.clear();
    contactCache_.clear();
    meshToEntity_.clear();
    rebuildCaches();

    {
        std::lock_guard slk(snapshotMtx_);
        snapshotFront_.clear();
        snapshotBack_.clear();
        springSnapshotFront_.clear();
        springSnapshotBack_.clear();
    }
    newSnapshotReady_.store(false, std::memory_order_release);

    // Preserve lights across physics resets.
    std::vector<ecs::LightSource> savedLights;
    savedLights.reserve(lightEntities_.size());
    for (auto e : lightEntities_)
        if (auto* ls = registry_.get<ecs::LightSource>(e)) savedLights.push_back(*ls);

    registry_ = ecs::Registry{};
    lightEntities_.clear();

    for (auto& ls : savedLights) {
        ecs::Entity e = registry_.create();
        registry_.add<ecs::LightSource>(e, ls);
        lightEntities_.push_back(e);
    }
}

// ---- cleanup ----

void World::cleanup() {
    std::lock_guard lk(structureMtx_);
    destroyDebugMeshes();
    for (auto& m : meshPool_)
        if (m) m->destroy(gpu_->device());
    meshPool_.clear();
    springs_.clear();
    contactCache_.clear();
    meshToEntity_.clear();
    rebuildCaches();
    registry_ = ecs::Registry{};
    lightEntities_.clear();
}

// ---- Simulation step ----

void World::advance(float dt) {
    std::lock_guard lk(structureMtx_);

    // Build entity list and pre-compute world-space inverse inertia tensors.
    std::vector<ecs::Entity> entities;
    for (auto [e, hc] : registry_.view<ecs::Hull>()) {
        entities.push_back(e);
        auto* tf = registry_.get<Transform>(e);
        if (!tf || registry_.has<ecs::Fixed>(e)) {
            hc.inertiaTensorWorld = {};
            continue;
        }
        math::Mat3 R = quatToMat3(tf->rotation);
        hc.inertiaTensorWorld = R * hc.inverseInertia * R.transpose();
    }

    // 0. Sync spring proxies.
    for (auto& s : springs_)
        s->syncProxies(registry_);

    // 1. SAP broadphase.
    sap_.collectPairs(entities, registry_, sapPairs_);

    // 2. Narrow-phase detection.
    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    for (auto& [ea, eb] : sapPairs_) {
        bool aFixed = registry_.has<ecs::Fixed>(ea);
        bool bFixed = registry_.has<ecs::Fixed>(eb);
        bool aSleep = registry_.has<ecs::Sleeping>(ea);
        bool bSleep = registry_.has<ecs::Sleeping>(eb);
        if (aFixed && bFixed) continue;
        if (aSleep && bSleep) continue;
        if (aSleep && bFixed) continue;
        if (aFixed && bSleep) continue;
        physics::ColliderDiscrete::detect(ea, eb, registry_, contacts);
    }

    // 3. Island-partitioned PGS solve.
    lastIslandCount_ = 0;
    if (!contacts.empty()) {
        if (!threadPool_) {
            unsigned int n = std::max(1u, std::thread::hardware_concurrency() - 1u);
            threadPool_ = std::make_unique<ThreadPool>(n);
        }
        std::vector<physics::Island> islands;
        islandDetector_.build(contacts, contactCache_, islands, registry_);
        lastIslandCount_ = static_cast<int>(islands.size());
        for (auto& island : islands) {
            threadPool_->enqueue([&island, dt, &reg = registry_]() mutable {
                physics::ColliderDiscrete::solveIsland(island.contacts, dt, reg, island.localCache);
            });
        }
        threadPool_->wait();
        physics::IslandDetector::mergeCache(islands, contactCache_);
    }

    // 4. Integration + sleep check.
    std::vector<ecs::Entity> toSleep;
    for (ecs::Entity e : entities) {
        auto* hc = registry_.get<ecs::Hull>(e);
        auto* tf = registry_.get<Transform>(e);
        if (!hc || !tf) continue;
        if (!hc->tangible) continue;
        if (registry_.has<ecs::Fixed>(e) || registry_.has<ecs::Sleeping>(e)) {
            hc->pseudoVel   = {};
            hc->pseudoOmega = {};
            continue;
        }

        // Gravity
        if (hc->gravity)
            hc->velocity += gravity * dt;

        // Damping
        float linDecay = 1.0f - hc->linearDamping  * dt;
        float angDecay = 1.0f - hc->angularDamping * dt;
        if (linDecay < 0.0f) linDecay = 0.0f;
        if (angDecay < 0.0f) angDecay = 0.0f;
        hc->velocity *= linDecay;
        hc->omega    *= angDecay;

        // Integrate position
        tf->position += hc->velocity * dt;

        // Integrate rotation via angular velocity
        if (hc->omega.dot(hc->omega) > 1e-12f) {
            math::Quat omegaQ{hc->omega.x, hc->omega.y, hc->omega.z, 0.0f};
            math::Quat dq = omegaQ * tf->rotation;
            tf->rotation.x += 0.5f * dt * dq.x;
            tf->rotation.y += 0.5f * dt * dq.y;
            tf->rotation.z += 0.5f * dt * dq.z;
            tf->rotation.w += 0.5f * dt * dq.w;
            tf->rotation = normalizeQuat(tf->rotation);
        }

        // Split-impulse pseudo-velocity position correction
        tf->position += hc->pseudoVel * dt;
        if (hc->pseudoOmega.dot(hc->pseudoOmega) > 1e-12f) {
            math::Quat pOmegaQ{hc->pseudoOmega.x, hc->pseudoOmega.y, hc->pseudoOmega.z, 0.0f};
            math::Quat pdq = pOmegaQ * tf->rotation;
            tf->rotation.x += 0.5f * dt * pdq.x;
            tf->rotation.y += 0.5f * dt * pdq.y;
            tf->rotation.z += 0.5f * dt * pdq.z;
            tf->rotation.w += 0.5f * dt * pdq.w;
            tf->rotation = normalizeQuat(tf->rotation);
        }
        hc->pseudoVel   = {};
        hc->pseudoOmega = {};

        // Sleep check
        if (hc->sleepingEnabled) {
            float linSq = hc->velocity.dot(hc->velocity);
            float angSq = hc->omega.dot(hc->omega);
            float linThresh = physics::SLEEP_LINEAR_THRESHOLD  * physics::SLEEP_LINEAR_THRESHOLD;
            float angThresh = physics::SLEEP_ANGULAR_THRESHOLD * physics::SLEEP_ANGULAR_THRESHOLD;
            if (linSq < linThresh && angSq < angThresh) {
                if (++hc->sleepFrames >= physics::SLEEP_FRAMES_REQUIRED) {
                    hc->velocity = {};
                    hc->omega    = {};
                    toSleep.push_back(e);
                }
            } else {
                hc->sleepFrames = 0;
            }
        }
    }
    // Add Sleeping tags outside view iteration (archetype mutation not safe inside).
    for (ecs::Entity e : toSleep)
        if (!registry_.has<ecs::Sleeping>(e))
            registry_.add<ecs::Sleeping>(e);

    // 5. Springs.
    for (auto& s : springs_)
        s->update(dt, registry_);

    publishSnapshot();
}

// ---- Snapshot double-buffer ----

void World::publishSnapshot() {
    snapshotBack_.clear();
    for (auto [e, hc] : registry_.view<ecs::Hull>()) {
        auto* tf = registry_.get<Transform>(e);
        if (!tf) continue;
        RenderMesh* mesh = nullptr;
        if (auto* mr = registry_.get<ecs::MeshRenderer>(e)) mesh = mr->mesh;
        snapshotBack_.push_back({ tf->position, tf->rotation, tf->scale, mesh, e });
    }

    springSnapshotBack_.clear();
    for (auto& s : springs_) {
        if (!registry_.valid(s->visualMeshEntity_)) continue;
        auto* mr = registry_.get<ecs::MeshRenderer>(s->visualMeshEntity_);
        if (!mr || !mr->mesh) continue;
        auto [pos, rot, scale] = s->computeSpringTransform(registry_);
        springSnapshotBack_.push_back({pos, rot, scale, mr->mesh, s->visualMeshEntity_});
    }

    if (debugPhysics)
        syncDebugMeshes();

    {
        std::lock_guard lk(snapshotMtx_);
        std::swap(snapshotFront_, snapshotBack_);
        std::swap(springSnapshotFront_, springSnapshotBack_);
    }
    newSnapshotReady_.store(true, std::memory_order_release);
}

void World::syncRenderMeshesFromFront() {
    for (auto& s : snapshotFront_) {
        if (s.mesh) {
            s.mesh->modelMatrix    = Transform{s.pos, s.rot, s.scale}.getModelMatrix();
            s.mesh->transformReady = true;
        }
    }
    for (auto& ss : springSnapshotFront_) {
        ss.mesh->modelMatrix    = Transform{ss.pos, ss.rot, ss.scale}.getModelMatrix();
        ss.mesh->transformReady = true;
    }
}

// ---- Debug overlay ----

void World::rebuildDebugMeshes() {
    destroyDebugMeshes();
    auto [boxV, boxI] = DebugShapes::makeBox();
    auto [sphV, sphI] = DebugShapes::makeSphere();
    debugEntities_.clear();

    for (auto [e, hc] : registry_.view<ecs::Hull>()) {
        debugEntities_.push_back(e);
        if (!hc.tangible) {
            debugMeshes_.push_back(nullptr);
            continue;
        }
        if (registry_.has<ecs::SphereForm>(e))
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, sphV, sphI));
        else
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, boxV, boxI));
    }
    syncDebugMeshes();
}

void World::syncDebugMeshes() {
    for (size_t i = 0; i < debugEntities_.size() && i < debugMeshes_.size(); ++i) {
        if (!debugMeshes_[i]) continue;
        ecs::Entity e = debugEntities_[i];
        auto* tf = registry_.get<Transform>(e);
        if (!tf) continue;

        math::Vec3 ext{1, 1, 1};
        if (auto* sf = registry_.get<ecs::SphereForm>(e))
            ext = {sf->radius, sf->radius, sf->radius};
        else if (auto* af = registry_.get<ecs::AABBForm>(e))
            ext = af->extent;
        else if (auto* of = registry_.get<ecs::OBBForm>(e))
            ext = of->extent;

        math::Mat4 T;
        T.m[12] = tf->position.x;
        T.m[13] = tf->position.y;
        T.m[14] = tf->position.z;
        math::Mat4 R;
        R.setRotationScale(quatToMat3(tf->rotation));
        math::Mat4 S = math::Mat4::scale(ext);
        debugMeshes_[i]->modelMatrix = T * R * S;
    }
}

void World::destroyDebugMeshes() {
    if (gpu_) {
        gpu_->syncDevice();
        for (auto& m : debugMeshes_)
            if (m) m->destroy(gpu_->device());
    }
    debugMeshes_.clear();
    debugEntities_.clear();
}

void World::toggleProxies(bool enabled) {
    for (auto& s : springs_)
        for (ecs::Entity proxyEnt : s->proxies_)
            if (auto* hc = registry_.get<ecs::Hull>(proxyEnt))
                hc->tangible = enabled;
}
