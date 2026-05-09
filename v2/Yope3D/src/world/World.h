#pragma once
#include <memory>
#include <vector>
#include <variant>
#include "RenderMesh.h"
#include "../rendering/Light.h"
#include "../assets/ObjLoader.h"
#include "../math/Vec3.h"
#include "../physics/Hull.h"
#include "../physics/CSphere.h"
#include "../physics/CAABB.h"
#include "../physics/COBB.h"
#include "../physics/Barrier.h"
#include "../physics/BoundedBarrier.h"
#include "../physics/BarrierHull.h"
#include "../physics/Spring.h"
#include "../physics/CollisionTree.h"
#include "../physics/PhysicsConstants.h"

class GpuDevice;

class World {
public:
    World() = default;
    ~World();

    void init(GpuDevice& gpu);
    void cleanup(GpuDevice& gpu);

    // ---- Renderables ----
    RenderMesh* addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                              const std::vector<Vertex>&   vertices,
                              const std::vector<uint32_t>& indices);
    RenderMesh* addRenderMesh(GpuDevice& gpu, VkCommandPool commandPool,
                              const LoadedMesh& mesh);

    const std::vector<std::unique_ptr<RenderMesh>>& getRenderMeshes() const;
    RenderMesh* getRenderMesh(size_t index);

    // ---- Lights ----
    void addLight(const Light& light);
    void removeLight(int index);
    void lightChanged();
    const std::vector<Light>& getLights() const;
    bool isLightsDirty() const { return lightsDirty; }
    void clearLightsDirty()    { lightsDirty = false; }

    // ---- Physics ----
    physics::CSphere*     addSphere(float mass, float radius, math::Vec3 pos = {});
    physics::COBB*        addOBB(math::Vec3 extent, float mass, math::Vec3 pos = {});
    physics::CAABB*       addAABB(math::Vec3 extent, float mass, math::Vec3 pos = {});
    physics::CAABB*       addStaticAABB(math::Vec3 pos, math::Vec3 extent);
    void                  addBarrier(physics::Barrier b);
    void                  addBarrier(physics::BoundedBarrier b);
    physics::BarrierHull* addBarrierHull(math::Vec3 extent, math::Vec3 pos);
    physics::Spring*      addSpring(physics::Hull* a, physics::Hull* b, float k, float rest);
    void                  initCollisionTree(math::Vec3 min, math::Vec3 max, int depth);
    void                  advance(float dt);

    const std::vector<std::unique_ptr<physics::Hull>>& getHulls() const;

    math::Vec3 gravity = {0.0f, physics::GRAVITY_Y, 0.0f};

    World(const World&) = delete;
    World& operator=(const World&) = delete;

private:
    std::vector<std::unique_ptr<RenderMesh>> renderMeshes;
    std::vector<Light>                       lights;
    bool                                     lightsDirty = false;

    std::vector<std::unique_ptr<physics::Hull>>                     hulls;
    std::vector<std::variant<physics::Barrier, physics::BoundedBarrier>> barriers;
    std::vector<std::unique_ptr<physics::Spring>>                   springs;
    std::unique_ptr<physics::CollisionTree>                         collisionTree;
};
