#pragma once
#include <memory>
#include <vector>
#include <variant>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include "SceneObject.h"
#include "RenderMesh.h"
#include "../rendering/Light.h"
#include "../ecs/Registry.h"
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

// World — owns all scene state via SceneObject (hull + mesh ownership bundled).
// Physics and rendering consume non-owning flat caches (hullCache_, meshCache_)
// rebuilt eagerly after each add/remove. Hull and RenderMesh heap addresses are
// stable (unique_ptr pointees don't move), so SAP pairs, Spring endpoints,
// and ContactCache keys remain valid across SceneObject vector reallocation.
class World {
public:
    World();
    ~World();

    // Call once after GpuDevice and Renderer are ready.
    // World caches gpu + pool so factory methods need no GPU params.
    void init(GpuDevice& gpu, VkCommandPool pool);
    void cleanup();

    // ---- Scene objects ----
    // Physics-only bodies (hull-only SceneObject, no visual).
    ecs::Entity addSphere    (float mass, float radius, math::Vec3 pos = {});
    ecs::Entity addOBB       (math::Vec3 extent, float mass, math::Vec3 pos = {});
    ecs::Entity addAABB      (math::Vec3 extent, float mass, math::Vec3 pos = {});
    ecs::Entity addStaticAABB(math::Vec3 pos, math::Vec3 extent);
    SceneObject* addBarrierHull(math::Vec3 extent, math::Vec3 pos);   // deprecated — removed in Step 5
    ecs::Entity addOBBFromMesh(const LoadedMesh& mesh, float mass);

    // Bare-barrier registration (static planes, no hull).
    void addBarrier(physics::Barrier b);
    void addBarrier(physics::BoundedBarrier b);

    // Visual-only entity (mesh, no physics body).
    ecs::Entity addRenderObject(const std::vector<Vertex>&   vertices,
                                const std::vector<uint32_t>& indices);
    ecs::Entity addRenderObject(const LoadedMesh& mesh);

    // Attach a mesh to an existing entity (creates mesh, sets hull->linkedMesh).
    // Returns the new RenderMesh* for immediate property configuration.
    RenderMesh* attachMesh(ecs::Entity obj,
                           const std::vector<Vertex>&   vertices,
                           const std::vector<uint32_t>& indices);
    RenderMesh* attachMesh(ecs::Entity obj, const LoadedMesh& mesh);

    // Remove entity and associated SceneObject (springs, mesh, hull) between frames.
    void removeEntity(ecs::Entity e);
    // Legacy removal — kept for internal use; removed in Step 4.
    void removeObject(SceneObject* obj);

    // Convenience accessors: Phase D bridge until Hull* deleted in Step 5.
    physics::Hull* getHull(ecs::Entity e);
    RenderMesh*    getMesh(ecs::Entity e);

    const std::vector<SceneObject*>& getObjects() const { return objectPtrs_; }

    // ---- Flat caches (non-owning, rebuilt on add/remove) ----
    // getHulls() builds dynamically from the hullToEntity_ map (no hullCache_ member).
    std::vector<physics::Hull*>     getHulls()        const;
    const std::vector<RenderMesh*>& getRenderMeshes() const { return meshCache_; }

    // Standalone barriers (added via addBarrier()). Hull-owned barriers live in BarrierHull::getBarriers().
    const std::vector<std::variant<physics::Barrier, physics::BoundedBarrier>>& getBarriers() const { return barriers_; }

    // ---- Springs ----
    physics::Spring* addSpring(ecs::Entity a, ecs::Entity b, float k, float rest);
    physics::Spring* addSpringWithProxies(ecs::Entity a, ecs::Entity b,
                                          float k, float rest,
                                          int proxyCount, float proxyRadius);
    physics::Spring* addSpringWithMesh(ecs::Entity a, ecs::Entity b,
                                       float k, float rest, int coils,
                                       float coilRadius, float tubeRadius,
                                       int proxyCount, float proxyRadius);

    // ---- ECS registry ----
    ecs::Registry& getRegistry() { return registry_; }

    // ---- Lights ----
    void addLight(const Light& light);
    void removeLight(int index);
    int  getLightCount() const { return static_cast<int>(lightEntities_.size()); }

    // ---- Simulation ----
    void advance(float dt);
    void resetPhysics();   // Syncs GPU, destroys all objects/springs/barriers, clears caches.

    // Called by physics thread at end of each advance() to push a transform snapshot.
    void publishSnapshot();
    // Called by main thread before drawFrame() to apply the latest snapshot to RenderMesh.modelMatrix.
    void syncRenderMeshesFromFront();

    // Set by physics thread after each publishSnapshot(); cleared by main thread after consuming.
    std::atomic<bool> newSnapshotReady_{ false };

    int getIslandCount() const { return lastIslandCount_; }
    int getThreadCount() const;

    math::Vec3 gravity = {0.0f, physics::GRAVITY_Y, 0.0f};

    physics::CollisionLayers layers;

    // ---- Physics debug overlay ----
    bool debugPhysics = false;
    void rebuildDebugMeshes();
    void syncDebugMeshes();
    void destroyDebugMeshes();
    const std::vector<std::unique_ptr<RenderMesh>>& getDebugMeshes() const { return debugMeshes_; }

    void toggleProxies(bool enabled);

    World(const World&) = delete;
    World& operator=(const World&) = delete;

private:
    struct TransformSnapshot {
        math::Vec3  pos;
        math::Quat  rot;
        math::Vec3  scale;
        RenderMesh* mesh;
        ecs::Entity entity;   // Phase C: for ECS Transform sync on main thread
    };

    GpuDevice*   gpu_  = nullptr;
    VkCommandPool pool_ = VK_NULL_HANDLE;

    // Double-buffered transform snapshots. Physics thread writes snapshotBack_,
    // main thread reads snapshotFront_. Swap is guarded by snapshotMtx_.
    struct SpringSnapshot {
        math::Vec3  pos;
        math::Quat  rot;
        math::Vec3  scale;
        RenderMesh* mesh;
        ecs::Entity entity;
    };

    std::vector<TransformSnapshot> snapshotBack_, snapshotFront_;
    std::vector<SpringSnapshot>    springSnapshotBack_, springSnapshotFront_;
    std::mutex snapshotMtx_;

    // Guards hullCache_/meshCache_/springs_ against concurrent advance() and add/remove calls.
    // Recursive because addSpringWith* internally calls addSphere/addRenderObject.
    std::recursive_mutex structureMtx_;

    std::vector<std::unique_ptr<SceneObject>> objects_;

    // Non-owning flat caches — rebuilt eagerly after add/remove.
    std::vector<RenderMesh*>    meshCache_;
    std::vector<SceneObject*>   objectPtrs_;

    ecs::Registry                                                          registry_;
    std::vector<ecs::Entity>                                               lightEntities_;
    std::unordered_map<physics::Hull*, ecs::Entity>                        hullToEntity_;
    std::unordered_map<RenderMesh*, ecs::Entity>                           meshToEntity_;

    std::vector<std::variant<physics::Barrier, physics::BoundedBarrier>>   barriers_;
    std::vector<std::unique_ptr<physics::Spring>>                          springs_;
    physics::BroadphaseSAP                                                 sap_;
    std::vector<std::pair<ecs::Entity, ecs::Entity>>                       sapPairs_;
    physics::EntityContactCache                                            contactCache_;
    physics::IslandDetector                                                islandDetector_;
    std::unique_ptr<ThreadPool>                                            threadPool_;
    int                                                                    lastIslandCount_ = 0;
    std::vector<std::unique_ptr<RenderMesh>>                               debugMeshes_;

    void rebuildCaches();
    ecs::Entity findEntityForHull(const physics::Hull* h) const;

    // Creates a new SceneObject with a hull, registers it, rebuilds caches. Returns raw ptr.
    template<class HullT, class... Args>
    SceneObject* makeHullObject(Args&&... args);
};
