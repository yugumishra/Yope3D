#include "World.h"
#include "../gpu/GpuDevice.h"
#include "../physics/ColliderCCD.h"
#include "../physics/ColliderDiscrete.h"
#include <variant>
#include "../physics/PhysicsConstants.h"
World::~World() {}

void World::init(GpuDevice& /*gpu*/) {}

void World::resetPhysics(GpuDevice& gpu) {
    for (auto& mesh : renderMeshes)
        if (mesh) mesh->destroy(gpu.device());
    renderMeshes.clear();
    hulls.clear();
    springs.clear();
    barriers.clear();
    contactCache.clear();
}

void World::cleanup(GpuDevice& gpu) {
    for (auto& mesh : renderMeshes) {
        if (mesh) mesh->destroy(gpu.device());
    }
    renderMeshes.clear();
    lights.clear();
    hulls.clear();
    springs.clear();
    barriers.clear();
    contactCache.clear();
    collisionTree.reset();
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

void World::initCollisionTree(math::Vec3 mn, math::Vec3 mx, int depth) {
    collisionTree = std::make_unique<physics::CollisionTree>(mn, mx, depth);
}

const std::vector<std::unique_ptr<physics::Hull>>& World::getHulls() const {
    return hulls;
}

// ---- Simulation step ----

void World::advance(float dt) {
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

    // 2. Hull-hull discrete collision — global PGS.
    // Phase 1: detect all contacts into a flat list.
    // Phase 2: solve all contacts globally (each pass propagates support one layer up the stack).
    std::vector<physics::ColliderDiscrete::ActiveContact> contacts;
    for (size_t i = 0; i < hulls.size(); i++) {
        if (!hulls[i]->isTangible()) continue;
        for (size_t j = i + 1; j < hulls.size(); j++) {
            if (!hulls[j]->isTangible()) continue;
            if (hulls[i]->isFixed() && hulls[j]->isFixed()) continue;
            if (hulls[i]->isSleeping() && hulls[j]->isSleeping()) continue;
            // A sleeping body resting on a fixed hull is stable — skip to avoid wakeup churn.
            if (hulls[i]->isSleeping() && hulls[j]->isFixed()) continue;
            if (hulls[i]->isFixed() && hulls[j]->isSleeping()) continue;
            physics::ColliderDiscrete::detect(*hulls[i], *hulls[j], contacts);
        }
    }
    physics::ColliderDiscrete::solveAll(contacts, dt, contactCache);

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
    for (auto& s : springs)
        s->update(dt);
}
