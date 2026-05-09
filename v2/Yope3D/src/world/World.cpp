#include "World.h"
#include "../gpu/GpuDevice.h"
#include "../physics/ColliderCCD.h"
#include "../physics/ColliderDiscrete.h"
#include <variant>
#include "../physics/PhysicsConstants.h"
World::~World() {}

void World::init(GpuDevice& /*gpu*/) {}

void World::cleanup(GpuDevice& gpu) {
    for (auto& mesh : renderMeshes) {
        if (mesh) mesh->destroy(gpu.device());
    }
    renderMeshes.clear();
    lights.clear();
    hulls.clear();
    springs.clear();
    barriers.clear();
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
    // 0. Gravity — applied before CCD so the barrier response reacts to it in the same frame.
    //    Hull::advance is then called with zero gravity to avoid double-application.
    for (auto& h : hulls)
        if (!h->isFixed() && h->isTangible() && h->gravityEnabled())
            h->addVelocity(gravity * dt);

    // 1. CCD barrier collision
    for (auto& h : hulls) {
        if (h->isFixed() || !h->isTangible()) continue;
        // Standalone infinite-plane barriers
        for (auto& bv : barriers) {
            std::visit([&](auto& b){ physics::ColliderCCD::collideBarrier(*h, b, dt); }, bv);
        }
        // BarrierHull internal barriers (box rooms, etc.)
        for (auto& other : hulls) {
            if (auto* bh = dynamic_cast<physics::BarrierHull*>(other.get())) {
                for (auto& bv : bh->getBarriers()) {
                    std::visit([&](auto& b){ physics::ColliderCCD::collideBarrier(*h, b, dt); }, bv);
                }
            }
        }
    }

    // 2. Hull-hull discrete collision — brute-force O(n²) for diagnostics
    for (size_t i = 0; i < hulls.size(); i++) {
        if (!hulls[i]->isTangible()) continue;
        for (size_t j = i + 1; j < hulls.size(); j++) {
            if (!hulls[j]->isTangible()) continue;
            if (hulls[i]->isFixed() && hulls[j]->isFixed()) continue;
            physics::ColliderDiscrete::collide(*hulls[i], *hulls[j], dt);
        }
    }

    // 3. Integration — gravity already applied in step 0, pass zero to avoid double-application
    const math::Vec3 zeroGravity = {};
    for (auto& h : hulls) {
        if (h->isTangible())
            h->advance(dt, zeroGravity);
    }

    // 4. Springs
    for (auto& s : springs)
        s->update(dt);
}
