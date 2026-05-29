#include "World.h"
#include "../gpu/GpuDevice.h"
#include "../physics/ColliderDiscrete.h"
#include "../physics/IslandDetector.h"
#include "../physics/ThreadPool.h"
#include "../physics/PhysicsConstants.h"
#include "../ecs/Components.h"
#include "../debug/Profiler.h"
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
        // extent is half-extents, so I_x = (1/3)*m*(b²+c²), inverse = 3/(m*(b²+c²))
        hc.inverseInertia = math::Mat3::scale({
            3.0f / (mass * (ey*ey + ez*ez)),
            3.0f / (mass * (ex*ex + ez*ez)),
            3.0f / (mass * (ex*ex + ey*ey))
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
    hc.inverseInertia = math::Mat3::zero();  // AABB has no angular dynamics
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
    hc.inverseInertia = math::Mat3::zero();
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
            phc->inverseInertia = math::Mat3::zero();
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
            phc->inverseInertia = math::Mat3::zero();
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
    registry_ = ecs::Registry{};
    lightEntities_.clear();
}

// ---- Simulation step ----

void World::advance(float dt) {
    YOPE_PROF_STEP("physics");
    std::lock_guard lk(structureMtx_);

    // Per-step ECS structure snapshot — drives the archetype/migration plots
    // and lets analyze_profile.py spot archetype churn vs. step time.
    YOPE_PROF_SET_ARCHETYPE_COUNT(registry_.archetypeCount());
    YOPE_PROF_SET_ARCHETYPE_MIGRATIONS(registry_.archetypeMigrationCount());

    // Build entity list and pre-compute world-space inverse inertia tensors.
    // The *_query scope below measures pure view-walk cost (no work) so the
    // delta against entity_list_build isolates the math/lookups for the
    // AoS-vs-SoA Phase E decision (yope3d_phase2_design.md §5.9).
    {
        YOPE_PROF_SCOPE("entity_list_build_query", "physics");
        static volatile int sink = 0;
        int acc = 0;
        for (auto [e, hc] : registry_.view<ecs::Hull>()) {
            (void)e;
            acc += hc.sleepFrames;
        }
        sink = acc;
    }
    {
        YOPE_PROF_SCOPE("entity_list_build", "physics");
        advanceEntities_.clear();
        for (auto [e, hc] : registry_.view<ecs::Hull>()) {
            advanceEntities_.push_back(e);
            auto* tf = registry_.get<Transform>(e);
            if (!tf || registry_.has<ecs::Fixed>(e)) {
                hc.inertiaTensorWorld = math::Mat3::zero();
                continue;
            }
            math::Mat3 R = quatToMat3(tf->rotation);
            hc.inertiaTensorWorld = R * hc.inverseInertia * R.transpose();
        }
    }
    YOPE_PROF_SET_OBJECT_COUNT(advanceEntities_.size());

    // 0. Sync spring proxies.
    {
        YOPE_PROF_SCOPE("spring_proxy_sync", "physics");
        for (auto& s : springs_)
            s->syncProxies(registry_);
    }

    // 1. SAP broadphase.
    {
        YOPE_PROF_SCOPE("broadphase_sap", "physics");
        sap_.collectPairs(advanceEntities_, registry_, sapPairs_);
    }

    // 2. Narrow-phase detection.
    {
        YOPE_PROF_SCOPE("narrowphase_detect", "physics");
        physics::ColliderDiscrete::resetNarrowphaseTiming();
        advanceContacts_.clear();
        for (auto& [ea, eb] : sapPairs_) {
            bool aFixed = registry_.has<ecs::Fixed>(ea);
            bool bFixed = registry_.has<ecs::Fixed>(eb);
            bool aSleep = registry_.has<ecs::Sleeping>(ea);
            bool bSleep = registry_.has<ecs::Sleeping>(eb);
            if (aFixed && bFixed) continue;
            if (aSleep && bSleep) continue;
            if (aSleep && bFixed) continue;
            if (aFixed && bSleep) continue;
            physics::ColliderDiscrete::detect(ea, eb, registry_, advanceContacts_);
        }
        physics::ColliderDiscrete::emitNarrowphaseProfile();
    }
    YOPE_PROF_SET_CONTACT_COUNT(advanceContacts_.size());

    // 3. Island-partitioned PGS solve.
    lastIslandCount_ = 0;
    if (!advanceContacts_.empty()) {
        if (!threadPool_) {
            unsigned int n = std::max(1u, std::thread::hardware_concurrency() - 1u);
            threadPool_ = std::make_unique<ThreadPool>(n);
        }
        // Collect spring entity pairs so the island builder can merge contact
        // islands that are bridged only by a spring (not a direct collision).
        std::vector<std::pair<ecs::Entity, ecs::Entity>> springPairs;
        springPairs.reserve(springs_.size());
        for (const auto& s : springs_)
            springPairs.push_back({s->first_, s->second_});

        std::vector<physics::Island> islands;
        {
            YOPE_PROF_SCOPE("island_build", "physics");
            islandDetector_.build(advanceContacts_, contactCache_, islands, registry_, springPairs);
        }
        lastIslandCount_ = static_cast<int>(islands.size());
        YOPE_PROF_SET_ISLAND_COUNT(lastIslandCount_);
        {
            YOPE_PROF_SCOPE("pgs_solve", "physics");
            {
                YOPE_PROF_SCOPE("pgs_dispatch", "physics");
                for (auto& island : islands) {
                    const int islandContactN = static_cast<int>(island.contacts.size());
                    threadPool_->enqueue([&island, dt, &reg = registry_, islandContactN]() mutable {
                        // Worker-thread scope. Stamped with scope_n = contact count for this
                        // island, so analyze_profile.py can plot solve-time vs. island size
                        // and spot the single-giant-island case where parallelism collapses.
                        YOPE_PROF_SCOPE_N("pgs_island", "physics", islandContactN);
                        physics::ColliderDiscrete::solveIsland(island.contacts, dt, reg, island.localCache);
                    });
                }
            }
            {
                YOPE_PROF_SCOPE("pgs_wait", "physics");
                threadPool_->wait();
            }
        }
        physics::IslandDetector::mergeCache(islands, contactCache_);
    }

    // 4. Integration + sleep check.
    // Rewritten to iterate view<Hull, Transform> directly — the previous
    // pattern walked advanceEntities_ and did random get<Hull>/get<Transform>
    // lookups per entity, which the A1 query data showed cost ~26% of the
    // integration scope (0.18 µs/entity of pure lookup overhead). The view
    // walks archetypes contiguously, eliminating the random-access lookups.
    // Sleep tags are still applied AFTER the view loop — archetype mutation
    // during iteration is not safe.
    {
        // Query sentinel — measures pure contiguous view-walk cost. After the
        // rewrite this should drop sharply vs. the old version (which was
        // measuring random-lookup cost). Delta to `integration` is now closer
        // to "actual math" rather than "math + lookup overhead".
        YOPE_PROF_SCOPE("integration_query", "physics");
        static volatile int sink = 0;
        int acc = 0;
        for (auto [e, hc, tf] : registry_.view<ecs::Hull, Transform>()) {
            (void)e; (void)tf;
            acc += hc.sleepFrames;
        }
        sink = acc;
    }
    YOPE_PROF_SCOPE("integration", "physics");
    std::vector<ecs::Entity> toSleep;
    for (auto [e, hc, tf] : registry_.view<ecs::Hull, Transform>()) {
        if (!hc.tangible) continue;
        if (registry_.has<ecs::Fixed>(e) || registry_.has<ecs::Sleeping>(e)) {
            hc.pseudoVel   = {};
            hc.pseudoOmega = {};
            continue;
        }

        // Gravity (applied once per full step, same as Hull::advance isFirst guard)
        if (hc.gravity)
            hc.velocity += gravity * dt;

        // Damping — skip if decay would flip sign (matches Hull::advance: if linDecay > 0)
        float linDecay = 1.0f - hc.linearDamping  * dt;
        float angDecay = 1.0f - hc.angularDamping * dt;
        if (linDecay > 0.0f) hc.velocity = hc.velocity * linDecay;
        if (angDecay > 0.0f) hc.omega    = hc.omega    * angDecay;

        // Integrate position
        tf.position += hc.velocity * dt;

        // Integrate rotation — exact axis-angle left-multiply (matches Hull::advance)
        float omegaLen = hc.omega.length();
        if (omegaLen > 1e-7f) {
            float angle = omegaLen * dt;
            math::Quat dq = math::Quat::fromAxisAngle(hc.omega * (1.0f / omegaLen), angle);
            tf.rotation = dq * tf.rotation;
            tf.rotation = normalizeQuat(tf.rotation);
        }

        // Split-impulse pseudo-velocity correction (matches Hull::advance)
        tf.position += hc.pseudoVel * dt;
        constexpr float MAX_PSEUDO_OMEGA = 2.0f;
        float pOmLen = std::sqrt(hc.pseudoOmega.dot(hc.pseudoOmega));
        if (pOmLen > MAX_PSEUDO_OMEGA)
            hc.pseudoOmega = hc.pseudoOmega * (MAX_PSEUDO_OMEGA / pOmLen);
        if (pOmLen > 1e-7f) {
            float angle = pOmLen * dt;
            math::Quat dq = math::Quat::fromAxisAngle(hc.pseudoOmega * (1.0f / pOmLen), angle);
            tf.rotation = dq * tf.rotation;
            tf.rotation = normalizeQuat(tf.rotation);
        }
        hc.pseudoVel   = {};
        hc.pseudoOmega = {};

        // Sleep check
        if (hc.sleepingEnabled) {
            float linSq = hc.velocity.dot(hc.velocity);
            float angSq = hc.omega.dot(hc.omega);
            float linThresh = physics::SLEEP_LINEAR_THRESHOLD  * physics::SLEEP_LINEAR_THRESHOLD;
            float angThresh = physics::SLEEP_ANGULAR_THRESHOLD * physics::SLEEP_ANGULAR_THRESHOLD;
            if (linSq < linThresh && angSq < angThresh) {
                if (++hc.sleepFrames >= physics::SLEEP_FRAMES_REQUIRED) {
                    hc.velocity = {};
                    hc.omega    = {};
                    toSleep.push_back(e);
                }
            } else {
                hc.sleepFrames = 0;
            }
        }
    }
    // Add Sleeping tags outside view iteration (archetype mutation not safe inside).
    for (ecs::Entity e : toSleep)
        if (!registry_.has<ecs::Sleeping>(e))
            registry_.add<ecs::Sleeping>(e);

    // 5. Springs.
    {
        YOPE_PROF_SCOPE("spring_update", "physics");
        for (auto& s : springs_)
            s->update(dt, registry_);
    }

    { YOPE_PROF_SCOPE("publish_snapshot", "physics"); publishSnapshot(); }
}

// ---- Snapshot double-buffer ----

void World::publishSnapshot() {
    snapshotBack_.clear();
    for (auto [e, hc] : registry_.view<ecs::Hull>()) {
        auto* tf = registry_.get<Transform>(e);
        if (!tf) continue;
        RenderMesh* mesh = nullptr;
        if (auto* mr = registry_.get<ecs::MeshRenderer>(e)) mesh = mr->mesh;
        snapshotBack_.push_back({ tf->position, tf->rotation, {1,1,1}, mesh, e });
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
