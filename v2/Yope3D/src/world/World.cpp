#include "World.h"
#include "../gpu/GpuDevice.h"
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
#include "../physics/PhysicsConstants.h"

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
    hullCache_.clear();
    meshCache_.clear();
    objectPtrs_.clear();
    for (auto& obj : objects_) {
        objectPtrs_.push_back(obj.get());
        if (obj->hull) hullCache_.push_back(obj->hull.get());
        if (obj->mesh) meshCache_.push_back(obj->mesh.get());
    }
}

// ---- Hull factory helper ----

template<class HullT, class... Args>
SceneObject* World::makeHullObject(Args&&... args) {
    auto obj = std::make_unique<SceneObject>();
    obj->hull = std::make_unique<HullT>(std::forward<Args>(args)...);
    objects_.push_back(std::move(obj));
    rebuildCaches();
    return objects_.back().get();
}

// ---- Physics-only factory methods ----

SceneObject* World::addSphere(float mass, float radius, math::Vec3 pos) {
    return makeHullObject<physics::CSphere>(mass, radius, pos);
}

SceneObject* World::addOBB(math::Vec3 extent, float mass, math::Vec3 pos) {
    return makeHullObject<physics::COBB>(extent, mass, pos);
}

SceneObject* World::addAABB(math::Vec3 extent, float mass, math::Vec3 pos) {
    return makeHullObject<physics::CAABB>(extent, mass, pos);
}

SceneObject* World::addStaticAABB(math::Vec3 pos, math::Vec3 extent) {
    return makeHullObject<physics::CAABB>(pos, extent);
}

SceneObject* World::addBarrierHull(math::Vec3 extent, math::Vec3 pos) {
    return makeHullObject<physics::BarrierHull>(
        physics::BarrierHull::genRectangularBarriers(extent, pos)
    );
}

SceneObject* World::addOBBFromMesh(const LoadedMesh& loadedMesh, float mass) {
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

void World::addBarrier(physics::Barrier b)        { barriers_.emplace_back(std::move(b)); }
void World::addBarrier(physics::BoundedBarrier b)  { barriers_.emplace_back(std::move(b)); }

// ---- Visual-only factory methods ----

SceneObject* World::addRenderObject(const std::vector<Vertex>& vertices,
                                     const std::vector<uint32_t>& indices) {
    auto obj = std::make_unique<SceneObject>();
    obj->mesh = std::make_unique<RenderMesh>(*gpu_, pool_, vertices, indices);
    objects_.push_back(std::move(obj));
    rebuildCaches();
    return objects_.back().get();
}

SceneObject* World::addRenderObject(const LoadedMesh& mesh) {
    return addRenderObject(mesh.vertices, mesh.indices);
}

// ---- Attach mesh to existing object ----

RenderMesh* World::attachMesh(SceneObject* obj,
                               const std::vector<Vertex>& vertices,
                               const std::vector<uint32_t>& indices) {
    obj->mesh = std::make_unique<RenderMesh>(*gpu_, pool_, vertices, indices);
    if (obj->hull) obj->hull->linkedMesh = obj->mesh.get();
    rebuildCaches();
    return obj->mesh.get();
}

RenderMesh* World::attachMesh(SceneObject* obj, const LoadedMesh& mesh) {
    return attachMesh(obj, mesh.vertices, mesh.indices);
}

// ---- Springs ----

physics::Spring* World::addSpring(physics::Hull* a, physics::Hull* b, float k, float rest) {
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    return springs_.back().get();
}

physics::Spring* World::addSpringWithProxies(physics::Hull* a, physics::Hull* b,
                                              float k, float rest,
                                              int proxyCount, float proxyRadius) {
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    physics::Spring* s = springs_.back().get();
    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        math::Vec3 pos = a->getPosition() + (b->getPosition() - a->getPosition()) * t;
        auto* proxyObj = addSphere(1.0f, proxyRadius, pos);
        auto* proxy = proxyObj->hullAs<physics::CSphere>();
        proxy->fix();
        proxy->disableGravity();
        s->proxies.push_back(proxy);
    }
    return s;
}

physics::Spring* World::addSpringWithMesh(physics::Hull* a, physics::Hull* b,
                                          float k, float rest, int coils,
                                          float coilRadius, float tubeRadius,
                                          int proxyCount, float proxyRadius) {
    springs_.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    physics::Spring* s = springs_.back().get();

    auto [verts, inds] = physics::Spring::generateCoilMesh(coils, coilRadius, tubeRadius);
    auto* coilObj = addRenderObject(verts, inds);
    s->visualMesh = coilObj->getMesh();
    s->visualMesh->modelMatrix = s->computeModelMatrix();

    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        math::Vec3 pos = a->getPosition() + (b->getPosition() - a->getPosition()) * t;
        auto* proxyObj = addSphere(1.0f, proxyRadius, pos);
        auto* proxy = proxyObj->hullAs<physics::CSphere>();
        proxy->fix();
        proxy->disableGravity();
        s->proxies.push_back(proxy);
    }
    return s;
}

// ---- Lights ----

void World::addLight(const Light& light) { lights_.push_back(light); lightsDirty = true; }

void World::removeLight(int index) {
    if (index >= 0 && index < static_cast<int>(lights_.size())) {
        lights_.erase(lights_.begin() + index);
        lightsDirty = true;
    }
}

void World::lightChanged() { lightsDirty = true; }
const std::vector<Light>& World::getLights() const { return lights_; }

// ---- removeObject ----

void World::removeObject(SceneObject* obj) {
    if (!obj) return;
    physics::Hull* h = obj->hull.get();

    if (h) {
        // 1a. Remove springs that reference this hull; also collect coil/proxy SceneObjects to remove.
        std::vector<SceneObject*> toRemove;
        springs_.erase(std::remove_if(springs_.begin(), springs_.end(),
            [&](const std::unique_ptr<physics::Spring>& s) {
                if (s->getFirst() == h || s->getSecond() == h) {
                    // Collect the coil visual SceneObject.
                    if (s->visualMesh) {
                        for (auto& o : objects_) {
                            if (o.get() != obj && o->mesh.get() == s->visualMesh)
                                toRemove.push_back(o.get());
                        }
                    }
                    // Collect proxy SceneObjects.
                    for (auto* proxy : s->proxies) {
                        for (auto& o : objects_) {
                            if (o.get() != obj && o->hull.get() == proxy)
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
            s->proxies.erase(std::remove(s->proxies.begin(), s->proxies.end(),
                static_cast<physics::CSphere*>(h)), s->proxies.end());
        }

        // 1c. Purge ContactCache entries keyed by this hull.
        std::erase_if(contactCache_,
            [h](const auto& kv) { return kv.first.a == h || kv.first.b == h; });
    }

    // 2. GPU teardown — sync device before freeing Vulkan buffers.
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
    // Sync GPU before destroying Vulkan buffers.
    if (gpu_) gpu_->syncDevice();

    destroyDebugMeshes();

    for (auto& obj : objects_)
        if (obj->mesh) obj->mesh->destroy(gpu_->device());
    objects_.clear();

    springs_.clear();
    barriers_.clear();
    contactCache_.clear();
    rebuildCaches();
}

// ---- cleanup ----

void World::cleanup() {
    destroyDebugMeshes();
    for (auto& obj : objects_)
        if (obj->mesh) obj->mesh->destroy(gpu_->device());
    objects_.clear();
    lights_.clear();
    springs_.clear();
    barriers_.clear();
    contactCache_.clear();
    rebuildCaches();
}

// ---- Simulation step ----

void World::advance(float dt) {
    // 0. Sync spring proxies.
    for (auto& s : springs_)
        s->syncProxies();

    // 1. CCD barrier collision.
    /*
    for (auto* h : hullCache_) {
        if (h->isFixed() || !h->isTangible() || h->isSleeping()) continue;
        for (auto& bv : barriers_) {
            std::visit([&](auto& b){ physics::ColliderCCD::collideBarrier(*h, b, dt, gravity); }, bv);
        }
    }*/

    // 2. SAP broadphase + island-partitioned PGS.
    sap_.collectPairs(hullCache_, sapPairs_);

    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    for (auto& [ha, hb] : sapPairs_) {
        if (ha->isFixed() && hb->isFixed()) continue;
        if (ha->isSleeping() && hb->isSleeping()) continue;
        if (ha->isSleeping() && hb->isFixed()) continue;
        if (ha->isFixed() && hb->isSleeping()) continue;
        physics::ColliderDiscrete::detect(*ha, *hb, contacts);
    }

    lastIslandCount_ = 0;
    if (!contacts.empty()) {
        if (!threadPool_) {
            unsigned int n = std::max(1u, std::thread::hardware_concurrency() - 1u);
            threadPool_ = std::make_unique<ThreadPool>(n);
        }
        std::vector<physics::Island> islands;
        islandDetector_.build(contacts, contactCache_, islands);
        lastIslandCount_ = static_cast<int>(islands.size());
        for (auto& island : islands) {
            threadPool_->enqueue([&island, dt] {
                physics::ColliderDiscrete::solveIsland(island.contacts, dt, island.localCache);
            });
        }
        threadPool_->wait();
        physics::IslandDetector::mergeCache(islands, contactCache_);
    }

    // 2b. Barrier pseudo-position clamp.
    /*
    for (auto* h : hullCache_) {
        if (!h->isTangible() || h->isFixed()) continue;
        auto clampAgainstBarriers = [&](const auto& bv) {
            std::visit([&](const auto& b) {
                using T = std::decay_t<decltype(b)>;
                if constexpr (std::is_same_v<T, physics::Barrier>) {
                    math::Vec3 n   = b.normal;
                    math::Vec3 ext = h->getBroadExtent();
                    float r   = std::abs(n.x)*ext.x + std::abs(n.y)*ext.y + std::abs(n.z)*ext.z;
                    float dist = (h->getPosition() - b.position).dot(n) - r;
                    if (dist < physics::SPLIT_SLOP) {
                        math::Vec3 pv  = h->getPseudoVel();
                        float      pvn = pv.dot(n);
                        if (pvn < 0.0f) h->addPseudoLinear(n * (-pvn));
                        math::Vec3 pOmega = h->getPseudoOmega();
                        if (pOmega.dot(pOmega) > 1e-10f) h->addPseudoAngular(-pOmega);
                    }
                }
            }, bv);
        };
        for (auto& bv : barriers_) clampAgainstBarriers(bv);
    }*/

    // 3. Integration.
    for (auto* h : hullCache_) {
        if (h->isTangible()) h->advance(dt, gravity);
    }

    // 4. Sleeping check.
    for (auto* h : hullCache_) {
        if (!h->isFixed() && h->isTangible() && !h->isSleeping()) {
            math::Vec3 v = h->getVelocity();
            math::Vec3 w = h->getOmega();
            h->tickSleep(v.dot(v), w.dot(w));
        }
    }

    // 5. Springs.
    for (auto& s : springs_) {
        s->update(dt);
        if (s->visualMesh)
            s->visualMesh->modelMatrix = s->computeModelMatrix();
    }
}

// ---- Debug overlay ----

void World::rebuildDebugMeshes() {
    destroyDebugMeshes();
    auto [boxV, boxI] = DebugShapes::makeBox();
    auto [sphV, sphI] = DebugShapes::makeSphere();

    for (auto* h : hullCache_) {
        if (!h->isTangible()) {
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
    size_t n = std::min(hullCache_.size(), debugMeshes_.size());
    for (size_t i = 0; i < n; ++i) {
        if (!debugMeshes_[i]) continue;
        auto* h  = hullCache_[i];
        auto& dm = *debugMeshes_[i];

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
        for (auto* p : s->proxies)
            p->setTangible(enabled);
}
