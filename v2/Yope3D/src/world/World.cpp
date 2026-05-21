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
#include "../physics/PhysicsConstants.h"

World::World()  = default;
World::~World() = default;

void World::init(GpuDevice& /*gpu*/) {}

int World::getThreadCount() const {
    return threadPool_ ? static_cast<int>(threadPool_->size()) : 0;
}

void World::resetPhysics(GpuDevice& gpu) {
    for (auto& mesh : renderMeshes)
        if (mesh) mesh->destroy(gpu.device());
    renderMeshes.clear();
    destroyDebugMeshes(gpu);
    hulls.clear();
    springs.clear();
    barriers.clear();
    contactCache.clear();
}

void World::cleanup(GpuDevice& gpu) {
    for (auto& mesh : renderMeshes)
        if (mesh) mesh->destroy(gpu.device());
    renderMeshes.clear();
    destroyDebugMeshes(gpu);
    lights.clear();
    hulls.clear();
    springs.clear();
    barriers.clear();
    contactCache.clear();
}

// ---- Renderables ----

RenderMesh* World::addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                                  const std::vector<Vertex>&   vertices,
                                  const std::vector<uint32_t>& indices)
{
    renderMeshes.push_back(
        std::make_unique<RenderMesh>(gpu, commandPool, vertices, indices)
    );
    return renderMeshes.back().get();
}

RenderMesh* World::addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                                  const LoadedMesh& mesh)
{
    return addRenderMesh(gpu, commandPool, mesh.vertices, mesh.indices);
}

const std::vector<std::unique_ptr<RenderMesh>>& World::getRenderMeshes() const {
    return renderMeshes;
}

RenderMesh* World::getRenderMesh(size_t index) {
    if (index < renderMeshes.size()) return renderMeshes[index].get();
    return nullptr;
}

// ---- Lights ----

void World::addLight(const Light& light) { lights.push_back(light); lightsDirty = true; }

void World::removeLight(int index) {
    if (index >= 0 && index < static_cast<int>(lights.size())) {
        lights.erase(lights.begin() + index);
        lightsDirty = true;
    }
}

void World::lightChanged() { lightsDirty = true; }

const std::vector<Light>& World::getLights() const { return lights; }

// ---- Physics factories ----

physics::CSphere* World::addSphere(float mass, float radius, math::Vec3 pos) {
    hulls.push_back(std::make_unique<physics::CSphere>(mass, radius, pos));
    return static_cast<physics::CSphere*>(hulls.back().get());
}

physics::COBB* World::addOBB(math::Vec3 extent, float mass, math::Vec3 pos) {
    hulls.push_back(std::make_unique<physics::COBB>(extent, mass, pos));
    return static_cast<physics::COBB*>(hulls.back().get());
}

physics::CAABB* World::addAABB(math::Vec3 extent, float mass, math::Vec3 pos) {
    hulls.push_back(std::make_unique<physics::CAABB>(extent, mass, pos));
    return static_cast<physics::CAABB*>(hulls.back().get());
}

physics::CAABB* World::addStaticAABB(math::Vec3 pos, math::Vec3 extent) {
    hulls.push_back(std::make_unique<physics::CAABB>(pos, extent));
    return static_cast<physics::CAABB*>(hulls.back().get());
}

void World::addBarrier(physics::Barrier b) {
    barriers.emplace_back(std::move(b));
}

void World::addBarrier(physics::BoundedBarrier b) {
    barriers.emplace_back(std::move(b));
}

physics::BarrierHull* World::addBarrierHull(math::Vec3 extent, math::Vec3 pos) {
    hulls.push_back(std::make_unique<physics::BarrierHull>(
        physics::BarrierHull::genRectangularBarriers(extent, pos)
    ));
    return static_cast<physics::BarrierHull*>(hulls.back().get());
}

physics::Spring* World::addSpring(physics::Hull* a, physics::Hull* b, float k, float rest) {
    springs.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    return springs.back().get();
}

physics::Spring* World::addSpringWithProxies(physics::Hull* a, physics::Hull* b,
                                              float k, float rest,
                                              int proxyCount, float proxyRadius) {
    springs.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    physics::Spring* s = springs.back().get();
    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        math::Vec3 pos = a->getPosition() + (b->getPosition() - a->getPosition()) * t;
        physics::CSphere* proxy = addSphere(1.0f, proxyRadius, pos);
        proxy->fix();
        proxy->disableGravity();
        s->proxies.push_back(proxy);
    }
    return s;
}

physics::Spring* World::addSpringWithMesh(physics::Hull* a, physics::Hull* b,
                                          float k, float rest, int coils,
                                          float coilRadius, float tubeRadius,
                                          int proxyCount, float proxyRadius,
                                          GpuDevice& gpu, VkCommandPool pool) {
    springs.push_back(std::make_unique<physics::Spring>(a, b, k, rest));
    physics::Spring* s = springs.back().get();

    auto [verts, inds] = physics::Spring::generateCoilMesh(coils, coilRadius, tubeRadius);
    s->visualMesh = addRenderMesh(gpu, pool, verts, inds);
    s->visualMesh->modelMatrix = s->computeModelMatrix();

    for (int i = 0; i < proxyCount; ++i) {
        float t = (float)(i + 1) / (float)(proxyCount + 1);
        math::Vec3 pos = a->getPosition() + (b->getPosition() - a->getPosition()) * t;
        physics::CSphere* proxy = addSphere(1.0f, proxyRadius, pos);
        proxy->fix();
        proxy->disableGravity();
        s->proxies.push_back(proxy);
    }

    return s;
}

physics::COBB* World::addOBBFromMesh(const LoadedMesh& mesh, float mass) {
    math::Vec3 mn = { FLT_MAX,  FLT_MAX,  FLT_MAX };
    math::Vec3 mx = {-FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const auto& v : mesh.vertices) {
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

const std::vector<std::unique_ptr<physics::Hull>>& World::getHulls() const {
    return hulls;
}

// ---- Simulation step ----

void World::advance(float dt) {
    // 0. Sync kinematic proxy spheres to current spring endpoint positions
    //    (before broadphase so they participate at the right locations)
    for (auto& s : springs)
        s->syncProxies();

    // 1. CCD barrier collision — skip sleeping hulls
    for (auto& h : hulls) {
        if (h->isFixed() || !h->isTangible() || h->isSleeping()) continue;
        for (auto& bv : barriers) {
            std::visit([&](auto& b){ physics::ColliderCCD::collideBarrier(*h, b, dt, gravity); }, bv);
        }
        for (auto& other : hulls) {
            if (auto* bh = dynamic_cast<physics::BarrierHull*>(other.get())) {
                for (auto& bv : bh->getBarriers()) {
                    std::visit([&](auto& b){ physics::ColliderCCD::collideBarrier(*h, b, dt, gravity); }, bv);
                }
            }
        }
    }

    // 2. Hull-hull discrete collision — SAP broadphase + island-partitioned PGS.
    // Phase 1: SAP collects AABB-overlapping pairs (sorted sweep, no per-frame allocation).
    // Phase 2: narrow-phase detect into flat contact list.
    // Phase 3: island detection partitions contacts into connected components.
    // Phase 4: each island solved independently; disjoint hull sets allow parallel execution.
    sap_.collectPairs(hulls, sapPairs_);

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
        // Lazy thread pool init (first advance call).
        if (!threadPool_) {
            unsigned int n = std::max(1u, std::thread::hardware_concurrency() - 1u);
            threadPool_ = std::make_unique<ThreadPool>(n);
        }

        std::vector<physics::Island> islands;
        islandDetector_.build(contacts, contactCache, islands);
        lastIslandCount_ = static_cast<int>(islands.size());

        for (auto& island : islands) {
            threadPool_->enqueue([&island, dt] {
                physics::ColliderDiscrete::solveIsland(island.contacts, dt, island.localCache);
            });
        }
        threadPool_->wait();

        physics::IslandDetector::mergeCache(islands, contactCache);
    }

    // 2b. Barrier pseudo-position clamp.
    // The split-impulse position pass gives the lower body a downward pseudo-velocity to
    // separate it from the upper body.  That pseudo-velocity is integrated into position
    // in step 3, sinking floor-resting bodies through the barrier — CCD (velocity-only)
    // cannot compensate.  Project out any pseudo-velocity component pointing into a barrier
    // when the hull surface is at or near the barrier plane.
    for (auto& h : hulls) {
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
                        if (pvn < 0.0f)
                            h->addPseudoLinear(n * (-pvn));
                        // Zero angular pseudo-velocity: off-center contact torques rotate
                        // floor-resting boxes, shifting contact geometry and causing horizontal drift.
                        math::Vec3 pOmega = h->getPseudoOmega();
                        if (pOmega.dot(pOmega) > 1e-10f)
                            h->addPseudoAngular(-pOmega);
                    }
                }
            }, bv);
        };
        for (auto& bv : barriers) clampAgainstBarriers(bv);
        for (auto& other : hulls)
            if (auto* bh = dynamic_cast<physics::BarrierHull*>(other.get()))
                for (auto& bv : bh->getBarriers()) clampAgainstBarriers(bv);
    }

    // 3. Integration
    for (auto& h : hulls) {
        if (h->isTangible())
            h->advance(dt, gravity);
    }

    // 4. Sleeping check — skip bodies already asleep (they stay asleep until woken by PGS contact)
    for (auto& h : hulls) {
        if (!h->isFixed() && h->isTangible() && !h->isSleeping()) {
            math::Vec3 v = h->getVelocity();
            math::Vec3 w = h->getOmega();
            h->tickSleep(v.dot(v), w.dot(w));
        }
    }

    // 5. Springs
    for (auto& s : springs) {
        s->update(dt);
        if (s->visualMesh)
            s->visualMesh->modelMatrix = s->computeModelMatrix();
    }
}

// ---- Physics debug meshes ----

void World::rebuildDebugMeshes(GpuDevice& gpu, VkCommandPool pool) {
    destroyDebugMeshes(gpu);
    auto [boxV, boxI] = DebugShapes::makeBox();
    auto [sphV, sphI] = DebugShapes::makeSphere();

    for (auto& h : hulls) {
        // nullptr placeholder for intangible hulls (e.g. player sphere) —
        // keeps debugMeshes[i] aligned with hulls[i] for syncDebugMeshes.
        if (!h->isTangible()) {
            debugMeshes.push_back(nullptr);
            continue;
        }
        if (dynamic_cast<physics::CSphere*>(h.get()))
            debugMeshes.push_back(std::make_unique<RenderMesh>(gpu, pool, sphV, sphI));
        else
            debugMeshes.push_back(std::make_unique<RenderMesh>(gpu, pool, boxV, boxI));
    }
    syncDebugMeshes();
}

void World::syncDebugMeshes() {
    // Debug meshes are built in the same order as hulls — keep indices in sync.
    size_t n = std::min(hulls.size(), debugMeshes.size());
    for (size_t i = 0; i < n; ++i) {
        if (!debugMeshes[i]) continue; // intangible hull placeholder
        auto& h  = *hulls[i];
        auto& dm = *debugMeshes[i];

        math::Vec3 ext = h.getBroadExtent(); // half-extents (or radius for sphere)
        math::Mat4 T;
        T.m[12] = h.getPosition().x;
        T.m[13] = h.getPosition().y;
        T.m[14] = h.getPosition().z;
        math::Mat4 R;
        R.setRotationScale(h.getRotTransform());
        // Box unit mesh has half-extent=1 → scale by half-extents directly.
        // Sphere unit mesh has radius=1   → scale by radius directly (getBroadExtent = {r,r,r}).
        math::Mat4 S = math::Mat4::scale(ext);
        dm.modelMatrix = T * R * S;
    }
}

void World::toggleProxies(bool enabled) {
    for (auto& s : springs)
        for (auto* p : s->proxies)
            p->setTangible(enabled);
}

void World::destroyDebugMeshes(GpuDevice& gpu) {
    for (auto& m : debugMeshes)
        if (m) m->destroy(gpu.device());
    debugMeshes.clear();
}
