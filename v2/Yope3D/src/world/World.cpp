#include "World.h"
#include "../gpu/GpuDevice.h"
#include <cmath>
#include "../physics/ColliderCCD.h"
#include "../physics/ColliderDiscrete.h"
#include "../physics/IslandDetector.h"
#include "../physics/ThreadPool.h"
#include "../physics/CSphere.h"
#include "../physics/CAABB.h"
#include "../physics/COBB.h"
#include <variant>
#include <cfloat>
#include <thread>
#include <algorithm>
#include <mutex>
#include "../physics/PhysicsConstants.h"
#include "../ecs/Components.h"

static ecs::Hull buildHullComp(const physics::Hull* h) {
    ecs::Hull c;
    c.velocity       = h->getVelocity();
    c.omega          = h->getOmega();
    c.mass           = h->getMass();
    c.inverseMass    = h->getInverseMass();
    c.friction       = h->friction;
    c.restitution    = h->restitution;
    c.linearDamping  = h->linearDamping;
    c.angularDamping = h->angularDamping;
    c.collisionLayer = h->collisionLayer;
    c.collisionMask  = h->collisionMask;
    c.gravity        = h->gravityEnabled();
    c.tangible       = h->isTangible();
    c.sleepingEnabled = h->isSleepingEnabled();
    return c;
}

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
    objectPtrs_.clear();
    for (auto& obj : objects_) {
        objectPtrs_.push_back(obj.get());
        if (obj->mesh) meshCache_.push_back(obj->mesh.get());
    }
}

std::vector<physics::Hull*> World::getHulls() const {
    std::vector<physics::Hull*> v;
    v.reserve(hullToEntity_.size());
    for (const auto& [h, e] : hullToEntity_) v.push_back(h);
    return v;
}

ecs::Entity World::findEntityForHull(const physics::Hull* h) const {
    for (const auto& obj : objects_)
        if (obj->hull.get() == h) return obj->entity;
    return ecs::NullEntity;
}

// ---- Hull factory helper ----

template<class HullT, class... Args>
SceneObject* World::makeHullObject(Args&&... args) {
    std::lock_guard lk(structureMtx_);
    auto obj = std::make_unique<SceneObject>();
    obj->hull = std::make_unique<HullT>(std::forward<Args>(args)...);
    objects_.push_back(std::move(obj));
    rebuildCaches();
    return objects_.back().get();
}

// ---- Physics-only factory methods ----

ecs::Entity World::addSphere(float mass, float radius, math::Vec3 pos) {
    auto* obj = makeHullObject<physics::CSphere>(mass, radius, pos);
    ecs::Entity e = registry_.create();
    obj->entity = e;
    auto* h = obj->getHull();
    registry_.add<Transform>(e, Transform{h->getPosition(), h->getRotation(), {radius, radius, radius}});
    registry_.add<ecs::Hull>(e, buildHullComp(h));
    if (auto* hc = registry_.get<ecs::Hull>(e)) {
        float invI = (mass > 0.0f && radius > 0.0f) ? 1.0f / (0.4f * mass * radius * radius) : 0.0f;
        hc->inverseInertia = math::Mat3::scale({invI, invI, invI});
    }
    registry_.add<ecs::SphereForm>(e, {radius});
    registry_.add<ecs::LegacyHullRef>(e, {h});
    hullToEntity_[h] = e;
    return e;
}

ecs::Entity World::addOBB(math::Vec3 extent, float mass, math::Vec3 pos) {
    auto* obj = makeHullObject<physics::COBB>(extent, mass, pos);
    ecs::Entity e = registry_.create();
    obj->entity = e;
    auto* h = obj->getHull();
    registry_.add<Transform>(e, Transform{h->getPosition(), h->getRotation(), extent});
    registry_.add<ecs::Hull>(e, buildHullComp(h));
    if (auto* hc = registry_.get<ecs::Hull>(e); hc && mass > 0.0f) {
        float ex = extent.x, ey = extent.y, ez = extent.z;
        hc->inverseInertia = math::Mat3::scale({
            12.0f / (mass * (ey*ey + ez*ez)),
            12.0f / (mass * (ex*ex + ez*ez)),
            12.0f / (mass * (ex*ex + ey*ey))
        });
    }
    registry_.add<ecs::OBBForm>(e, {extent});
    registry_.add<ecs::LegacyHullRef>(e, {h});
    hullToEntity_[h] = e;
    return e;
}

ecs::Entity World::addAABB(math::Vec3 extent, float mass, math::Vec3 pos) {
    auto* obj = makeHullObject<physics::CAABB>(extent, mass, pos);
    ecs::Entity e = registry_.create();
    obj->entity = e;
    auto* h = obj->getHull();
    registry_.add<Transform>(e, Transform{h->getPosition(), h->getRotation(), extent});
    registry_.add<ecs::Hull>(e, buildHullComp(h));
    if (auto* hc = registry_.get<ecs::Hull>(e); hc && mass > 0.0f) {
        float ex = extent.x, ey = extent.y, ez = extent.z;
        hc->inverseInertia = math::Mat3::scale({
            12.0f / (mass * (ey*ey + ez*ez)),
            12.0f / (mass * (ex*ex + ez*ez)),
            12.0f / (mass * (ex*ex + ey*ey))
        });
    }
    registry_.add<ecs::AABBForm>(e, {extent});
    registry_.add<ecs::LegacyHullRef>(e, {h});
    hullToEntity_[h] = e;
    return e;
}

ecs::Entity World::addStaticAABB(math::Vec3 pos, math::Vec3 extent) {
    auto* obj = makeHullObject<physics::CAABB>(pos, extent);
    ecs::Entity e = registry_.create();
    obj->entity = e;
    auto* h = obj->getHull();
    registry_.add<Transform>(e, Transform{pos, {0, 0, 0, 1}, extent});
    registry_.add<ecs::Hull>(e, buildHullComp(h));
    registry_.add<ecs::AABBForm>(e, {extent});
    registry_.add<ecs::Fixed>(e);
    registry_.add<ecs::LegacyHullRef>(e, {h});
    hullToEntity_[h] = e;
    return e;
}

SceneObject* World::addBarrierHull(math::Vec3 extent, math::Vec3 pos) {
    return makeHullObject<physics::BarrierHull>(
        physics::BarrierHull::genRectangularBarriers(extent, pos)
    );
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

// ---- Bare barriers ----

void World::addBarrier(physics::Barrier b) {
    std::lock_guard lk(structureMtx_);
    barriers_.emplace_back(std::move(b));
}
void World::addBarrier(physics::BoundedBarrier b) {
    std::lock_guard lk(structureMtx_);
    barriers_.emplace_back(std::move(b));
}

// ---- Primitive-type detection ----
// Called after constructing a RenderMesh from a named Primitive to let the
// raytracer choose parametric intersection instead of triangle soup.
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
    // else: stays Custom

    if (rm->primitiveType != PrimitiveType::Custom) {
        rm->cpuVertices.clear(); rm->cpuVertices.shrink_to_fit();
        rm->cpuIndices.clear();  rm->cpuIndices.shrink_to_fit();
    }
}

// ---- Visual-only factory methods ----

ecs::Entity World::addRenderObject(const std::vector<Vertex>& vertices,
                                    const std::vector<uint32_t>& indices) {
    std::lock_guard lk(structureMtx_);
    auto obj = std::make_unique<SceneObject>();
    obj->mesh = std::make_unique<RenderMesh>(*gpu_, pool_, vertices, indices);
    obj->mesh->transformReady = true;  // static mesh: transform is known at creation
    objects_.push_back(std::move(obj));
    rebuildCaches();
    SceneObject* raw = objects_.back().get();
    ecs::Entity e = registry_.create();
    raw->entity = e;
    registry_.add<Transform>(e);
    registry_.add<ecs::MeshRenderer>(e, {raw->getMesh()});
    meshToEntity_[raw->getMesh()] = e;
    return e;
}

ecs::Entity World::addRenderObject(const LoadedMesh& mesh) {
    ecs::Entity e = addRenderObject(mesh.vertices, mesh.indices);
    // find the SceneObject we just created to set primitive info
    for (auto& obj : objects_) {
        if (obj->entity == e && obj->mesh) {
            setPrimitiveInfo(obj->mesh.get(), mesh);
            break;
        }
    }
    return e;
}

// ---- Attach mesh to existing entity ----

RenderMesh* World::attachMesh(ecs::Entity e,
                               const std::vector<Vertex>& vertices,
                               const std::vector<uint32_t>& indices) {
    std::lock_guard lk(structureMtx_);
    SceneObject* obj = nullptr;
    for (auto& o : objects_)
        if (o->entity == e) { obj = o.get(); break; }
    if (!obj) return nullptr;
    obj->mesh = std::make_unique<RenderMesh>(*gpu_, pool_, vertices, indices);
    if (obj->hull) {
        obj->hull->linkedMesh = obj->mesh.get();
    } else {
        obj->mesh->transformReady = true;
    }
    rebuildCaches();
    registry_.add<ecs::MeshRenderer>(e, {obj->mesh.get()});
    return obj->mesh.get();
}

RenderMesh* World::attachMesh(ecs::Entity e, const LoadedMesh& mesh) {
    RenderMesh* rm = attachMesh(e, mesh.vertices, mesh.indices);
    if (rm) setPrimitiveInfo(rm, mesh);
    return rm;
}

// ---- Entity/mesh accessors ----

physics::Hull* World::getHull(ecs::Entity e) {
    auto* ref = registry_.get<ecs::LegacyHullRef>(e);
    return ref ? ref->ptr : nullptr;
}

RenderMesh* World::getMesh(ecs::Entity e) {
    auto* mr = registry_.get<ecs::MeshRenderer>(e);
    return mr ? mr->mesh : nullptr;
}

void World::removeEntity(ecs::Entity e) {
    for (const auto& obj : objects_) {
        if (obj->entity == e) { removeObject(obj.get()); return; }
    }
    if (registry_.valid(e)) registry_.destroy(e);
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
    if (auto* ha = getHull(a)) posA = ha->getPosition();
    if (auto* hb = getHull(b)) posB = hb->getPosition();
    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        ecs::Entity proxyEnt = addSphere(1.0f, proxyRadius, posA + (posB - posA) * t);
        if (auto* proxy = dynamic_cast<physics::CSphere*>(getHull(proxyEnt))) {
            proxy->fix(); proxy->disableGravity();
        }
        if (!registry_.has<ecs::Fixed>(proxyEnt)) registry_.add<ecs::Fixed>(proxyEnt);
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
    if (auto* ha = getHull(a)) posA = ha->getPosition();
    if (auto* hb = getHull(b)) posB = hb->getPosition();
    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        ecs::Entity proxyEnt = addSphere(1.0f, proxyRadius, posA + (posB - posA) * t);
        if (auto* proxy = dynamic_cast<physics::CSphere*>(getHull(proxyEnt))) {
            proxy->fix(); proxy->disableGravity();
        }
        if (!registry_.has<ecs::Fixed>(proxyEnt)) registry_.add<ecs::Fixed>(proxyEnt);
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

// ---- removeObject ----

void World::removeObject(SceneObject* obj) {
    if (!obj) return;
    std::lock_guard lk(structureMtx_);
    ecs::Entity removedEnt = obj->entity;
    if (registry_.valid(removedEnt))
        registry_.destroy(removedEnt);
    physics::Hull* h = obj->hull.get();

    if (h) {
        // 1a. Remove springs that reference this entity; collect coil/proxy SceneObjects.
        std::vector<SceneObject*> toRemove;
        springs_.erase(std::remove_if(springs_.begin(), springs_.end(),
            [&](const std::unique_ptr<physics::Spring>& s) {
                if (s->first_ == removedEnt || s->second_ == removedEnt) {
                    // Collect the coil visual SceneObject by entity.
                    for (auto& o : objects_) {
                        if (o.get() != obj && o->entity == s->visualMeshEntity_)
                            toRemove.push_back(o.get());
                    }
                    // Collect proxy SceneObjects by entity.
                    for (ecs::Entity proxyEnt : s->proxies_) {
                        for (auto& o : objects_) {
                            if (o.get() != obj && o->entity == proxyEnt)
                                toRemove.push_back(o.get());
                        }
                    }
                    return true;
                }
                return false;
            }), springs_.end());

        // Remove collected coil/proxy objects (they don't trigger spring cleanup themselves).
        for (auto* dead : toRemove) {
            if (dead->mesh) { dead->mesh->destroy(gpu_->device()); dead->mesh.reset(); }
            objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                [dead](const std::unique_ptr<SceneObject>& o) { return o.get() == dead; }),
                objects_.end());
        }

        // 1b. Remove proxy back-references in surviving springs.
        for (auto& s : springs_) {
            s->proxies_.erase(std::remove(s->proxies_.begin(), s->proxies_.end(), removedEnt),
                              s->proxies_.end());
        }

        // 1c. Purge EntityContactCache entries keyed by this entity.
        std::erase_if(contactCache_,
            [removedEnt](const auto& kv) {
                return kv.first.a == removedEnt || kv.first.b == removedEnt;
            });

        hullToEntity_.erase(h);
    }

    // 2. GPU teardown — sync device before freeing Vulkan buffers.
    if (obj->mesh) meshToEntity_.erase(obj->mesh.get());
    gpu_->syncDevice();
    if (obj->mesh) { obj->mesh->destroy(gpu_->device()); obj->mesh.reset(); }

    // 3. Erase from objects (frees the unique_ptrs — hull pointer invalidated here).
    objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
        [obj](const std::unique_ptr<SceneObject>& o) { return o.get() == obj; }),
        objects_.end());

    // 4. Rebuild caches; refresh debug overlay if active.
    rebuildCaches();
    if (debugPhysics) {
        destroyDebugMeshes();
        rebuildDebugMeshes();
    }
}

// ---- resetPhysics ----

void World::resetPhysics() {
    std::lock_guard lk(structureMtx_);
    // Sync GPU before destroying Vulkan buffers.
    if (gpu_) gpu_->syncDevice();

    destroyDebugMeshes();

    for (auto& obj : objects_)
        if (obj->mesh) obj->mesh->destroy(gpu_->device());
    objects_.clear();

    springs_.clear();
    barriers_.clear();
    contactCache_.clear();
    hullToEntity_.clear();
    meshToEntity_.clear();
    rebuildCaches();

    // Preserve lights across physics resets — they're rendering state, not simulation state.
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
    for (auto& obj : objects_)
        if (obj->mesh) obj->mesh->destroy(gpu_->device());
    objects_.clear();
    springs_.clear();
    barriers_.clear();
    contactCache_.clear();
    hullToEntity_.clear();
    meshToEntity_.clear();
    rebuildCaches();

    registry_ = ecs::Registry{};
    lightEntities_.clear();
}

// ---- Simulation step ----

void World::advance(float dt) {
    std::lock_guard lk(structureMtx_);

    // Build entity and hull lists from ECS.
    std::vector<ecs::Entity>    entities;
    std::vector<physics::Hull*> hulls;
    for (auto [e, ref] : registry_.view<ecs::LegacyHullRef>()) {
        if (ref.ptr) {
            entities.push_back(e);
            hulls.push_back(ref.ptr);
        }
    }

    // 0. Sync spring proxies.
    for (auto& s : springs_)
        s->syncProxies(registry_);

    // 1. SAP broadphase — Entity-keyed pairs.
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

    // 4. Integration (Hull*-based; Hull deleted in Step 5).
    for (auto* h : hulls) {
        if (h->isTangible()) h->advance(dt, gravity);
    }

    // 5. Sleeping check.
    for (auto* h : hulls) {
        if (!h->isFixed() && h->isTangible() && !h->isSleeping()) {
            math::Vec3 v = h->getVelocity();
            math::Vec3 w = h->getOmega();
            h->tickSleep(v.dot(v), w.dot(w));
        }
    }

    // 6. Springs.
    for (auto& s : springs_)
        s->update(dt, registry_);

    publishSnapshot();
}

// ---- Snapshot double-buffer ----

void World::publishSnapshot() {
    // Called at end of advance(), still holding structureMtx_.
    snapshotBack_.clear();
    for (auto [e, ref] : registry_.view<ecs::LegacyHullRef>()) {
        auto* h = ref.ptr;
        if (!h) continue;
        snapshotBack_.push_back({ h->getPosition(), h->getRotation(), h->getScale(), h->linkedMesh, e });

        if (auto* hc = registry_.get<ecs::Hull>(e)) {
            hc->velocity           = h->getVelocity();
            hc->omega              = h->getOmega();
            hc->inertiaTensorWorld = h->getInverseInertiaTensorWorld();
        }
        bool sleeping = h->isSleeping();
        if (sleeping && !registry_.has<ecs::Sleeping>(e))
            registry_.add<ecs::Sleeping>(e);
        else if (!sleeping && registry_.has<ecs::Sleeping>(e))
            registry_.remove<ecs::Sleeping>(e);
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
    // Called on the main thread after newSnapshotReady_ is observed; no lock needed
    // since snapshotFront_ is only swapped under snapshotMtx_ before this call.
    for (auto& s : snapshotFront_) {
        if (s.mesh) {
            s.mesh->modelMatrix    = Transform{s.pos, s.rot, s.scale}.getModelMatrix();
            s.mesh->transformReady = true;
        }
        if (registry_.valid(s.entity)) {
            if (auto* t = registry_.get<Transform>(s.entity)) {
                t->position = s.pos;
                t->rotation = s.rot;
                t->scale    = s.scale;
            }
        }
    }
    for (auto& ss : springSnapshotFront_) {
        ss.mesh->modelMatrix    = Transform{ss.pos, ss.rot, ss.scale}.getModelMatrix();
        ss.mesh->transformReady = true;
        if (registry_.valid(ss.entity)) {
            if (auto* t = registry_.get<Transform>(ss.entity)) {
                t->position = ss.pos;
                t->rotation = ss.rot;
                t->scale    = ss.scale;
            }
        }
    }
}

// ---- Debug overlay ----

void World::rebuildDebugMeshes() {
    destroyDebugMeshes();
    auto [boxV, boxI] = DebugShapes::makeBox();
    auto [sphV, sphI] = DebugShapes::makeSphere();

    for (const auto& obj : objects_) {
        auto* h = obj->hull.get();
        if (!h || !h->isTangible()) {
            debugMeshes_.push_back(nullptr);
            continue;
        }
        if (dynamic_cast<physics::CSphere*>(h))
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, sphV, sphI));
        else
            debugMeshes_.push_back(std::make_unique<RenderMesh>(*gpu_, pool_, boxV, boxI));
    }
    syncDebugMeshes();
}

void World::syncDebugMeshes() {
    size_t dmIdx = 0;
    for (const auto& obj : objects_) {
        if (dmIdx >= debugMeshes_.size()) break;
        if (!debugMeshes_[dmIdx]) { ++dmIdx; continue; }
        auto* h  = obj->hull.get();
        if (!h)  { ++dmIdx; continue; }
        auto& dm = *debugMeshes_[dmIdx];
        ++dmIdx;

        math::Vec3 ext = h->getBroadExtent();
        math::Mat4 T;
        T.m[12] = h->getPosition().x;
        T.m[13] = h->getPosition().y;
        T.m[14] = h->getPosition().z;
        math::Mat4 R;
        R.setRotationScale(h->getRotTransform());
        math::Mat4 S = math::Mat4::scale(ext);
        dm.modelMatrix = T * R * S;
    }
}

void World::destroyDebugMeshes() {
    if (gpu_) {
        gpu_->syncDevice();
        for (auto& m : debugMeshes_)
            if (m) m->destroy(gpu_->device());
    }
    debugMeshes_.clear();
}

void World::toggleProxies(bool enabled) {
    for (auto& s : springs_)
        for (ecs::Entity proxyEnt : s->proxies_) {
            if (auto* hc = registry_.get<ecs::Hull>(proxyEnt))
                hc->tangible = enabled;
            if (auto* ref = registry_.get<ecs::LegacyHullRef>(proxyEnt))
                if (ref->ptr) ref->ptr->setTangible(enabled);
        }
}
