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
#include "../physics/BroadphaseSAP.h"
#include "../physics/IslandDetector.h"
#include "../physics/PhysicsConstants.h"
#include "../physics/ContactCache.h"
#include "../physics/CollisionLayers.h"
#include "../physics/DebugShapes.h"

class GpuDevice;
class ThreadPool;

class World {
public:
    World();
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
    // Creates a spring with kinematic proxy spheres for collision along the body.
    // No visual mesh — configure collisionLayer/collisionMask on the returned
    // spring's proxies and on the endpoint hulls to prevent self-interaction.
    physics::Spring*      addSpringWithProxies(physics::Hull* a, physics::Hull* b,
                                               float k, float rest,
                                               int proxyCount, float proxyRadius);
    // Creates a spring with a procedural helix visual mesh and optional kinematic
    // proxy spheres for collision presence along the spring body.
    // proxyCount  — number of proxy spheres (0 = none); evenly spaced between endpoints
    // proxyRadius — collision radius for each proxy (typically coilRadius + tubeRadius)
    // After creation, configure proxy/endpoint collisionLayer + collisionMask to prevent
    // the proxies from spuriously pushing the endpoint hulls apart.
    physics::Spring*      addSpringWithMesh(physics::Hull* a, physics::Hull* b,
                                            float k, float rest, int coils,
                                            float coilRadius, float tubeRadius,
                                            int proxyCount, float proxyRadius,
                                            GpuDevice& gpu, VkCommandPool commandPool);
    // Creates an OBB whose half-extents match the axis-aligned bounding box of mesh vertices.
    physics::COBB*        addOBBFromMesh(const LoadedMesh& mesh, float mass);
    void                  advance(float dt);
    void                  resetPhysics(GpuDevice& gpu);

    int getIslandCount()  const { return lastIslandCount_; }
    int getThreadCount()  const;

    const std::vector<std::unique_ptr<physics::Hull>>& getHulls() const;

    math::Vec3 gravity = {0.0f, physics::GRAVITY_Y, 0.0f};

    // Named collision layer registry — use before creating hulls that need filtering.
    physics::CollisionLayers layers;

    // ---- Physics debug rendering ----
    // When true, the Renderer overlays each hull as its actual collision shape
    // (box for CAABB/COBB/BarrierHull, sphere for CSphere) in a bright debug color.
    // Call rebuildDebugMeshes() once after scene setup, then syncDebugMeshes() each frame.
    bool debugPhysics = false;
    void rebuildDebugMeshes(GpuDevice& gpu, VkCommandPool commandPool);
    void syncDebugMeshes();
    void destroyDebugMeshes(GpuDevice& gpu);
    const std::vector<std::unique_ptr<RenderMesh>>& getDebugMeshes() const { return debugMeshes; }

    // Toggle tangibility on every spring proxy sphere (use to isolate proxy collision impact).
    void toggleProxies(bool enabled);

    World(const World&) = delete;
    World& operator=(const World&) = delete;

private:
    std::vector<std::unique_ptr<RenderMesh>> renderMeshes;
    std::vector<Light>                       lights;
    bool                                     lightsDirty = false;

    std::vector<std::unique_ptr<physics::Hull>>                     hulls;
    std::vector<std::variant<physics::Barrier, physics::BoundedBarrier>> barriers;
    std::vector<std::unique_ptr<physics::Spring>>                   springs;
    physics::BroadphaseSAP                                          sap_;
    std::vector<std::pair<physics::Hull*, physics::Hull*>>          sapPairs_;
    physics::ContactCache                                           contactCache;
    physics::IslandDetector                                         islandDetector_;
    std::unique_ptr<ThreadPool>                                     threadPool_;
    int                                                             lastIslandCount_ = 0;
    std::vector<std::unique_ptr<RenderMesh>>                        debugMeshes;
};
