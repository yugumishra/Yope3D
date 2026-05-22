#pragma once
#include <memory>
#include <vector>
#include <variant>
#include <atomic>
#include <mutex>
#include "SceneObject.h"
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
    SceneObject* addSphere    (float mass, float radius, math::Vec3 pos = {});
    SceneObject* addOBB       (math::Vec3 extent, float mass, math::Vec3 pos = {});
    SceneObject* addAABB      (math::Vec3 extent, float mass, math::Vec3 pos = {});
    SceneObject* addStaticAABB(math::Vec3 pos, math::Vec3 extent);
    SceneObject* addBarrierHull(math::Vec3 extent, math::Vec3 pos);
    SceneObject* addOBBFromMesh(const LoadedMesh& mesh, float mass);

    // Bare-barrier registration (static planes, no hull).
    void addBarrier(physics::Barrier b);
    void addBarrier(physics::BoundedBarrier b);

    // Visual-only SceneObject (mesh, no physics body).
    SceneObject* addRenderObject(const std::vector<Vertex>&   vertices,
                                 const std::vector<uint32_t>& indices);
    SceneObject* addRenderObject(const LoadedMesh& mesh);

    // Attach a mesh to an existing SceneObject (creates mesh, sets hull->linkedMesh).
    // Returns the new RenderMesh* for immediate property configuration.
    RenderMesh* attachMesh(SceneObject* obj,
                           const std::vector<Vertex>&   vertices,
                           const std::vector<uint32_t>& indices);
    RenderMesh* attachMesh(SceneObject* obj, const LoadedMesh& mesh);

    // Remove a SceneObject: purges springs/cache referencing its hull, frees GPU mesh,
    // erases from objects, rebuilds caches. Call only between frames (not during advance).
    void removeObject(SceneObject* obj);

    const std::vector<SceneObject*>& getObjects() const { return objectPtrs_; }

    // ---- Flat caches (non-owning, rebuilt on add/remove) ----
    const std::vector<physics::Hull*>& getHulls()        const { return hullCache_; }
    const std::vector<RenderMesh*>&    getRenderMeshes()  const { return meshCache_; }

    // ---- Springs ----
    physics::Spring* addSpring(physics::Hull* a, physics::Hull* b, float k, float rest);
    physics::Spring* addSpringWithProxies(physics::Hull* a, physics::Hull* b,
                                          float k, float rest,
                                          int proxyCount, float proxyRadius);
    physics::Spring* addSpringWithMesh(physics::Hull* a, physics::Hull* b,
                                       float k, float rest, int coils,
                                       float coilRadius, float tubeRadius,
                                       int proxyCount, float proxyRadius);

    // ---- Lights ----
    void addLight(const Light& light);
    void removeLight(int index);
    void lightChanged();
    const std::vector<Light>& getLights() const;
    bool isLightsDirty()   const { return lightsDirty; }
    void clearLightsDirty()      { lightsDirty = false; }

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
    };

    GpuDevice*   gpu_  = nullptr;
    VkCommandPool pool_ = VK_NULL_HANDLE;

    // Double-buffered transform snapshots. Physics thread writes snapshotBack_,
    // main thread reads snapshotFront_. Swap is guarded by snapshotMtx_.
    std::vector<TransformSnapshot>                 snapshotBack_, snapshotFront_;
    // Spring visual mesh matrices (full Mat4 since computeModelMatrix() returns one directly).
    std::vector<std::pair<RenderMesh*, math::Mat4>> springSnapshotBack_, springSnapshotFront_;
    std::mutex snapshotMtx_;

    // Guards hullCache_/meshCache_/springs_ against concurrent advance() and add/remove calls.
    // Recursive because addSpringWith* internally calls addSphere/addRenderObject.
    std::recursive_mutex structureMtx_;

    std::vector<std::unique_ptr<SceneObject>> objects_;

    // Non-owning flat caches — rebuilt eagerly after add/remove.
    std::vector<physics::Hull*> hullCache_;
    std::vector<RenderMesh*>    meshCache_;
    std::vector<SceneObject*>   objectPtrs_;

    std::vector<Light>                                                     lights_;
    bool                                                                   lightsDirty = false;
    std::vector<std::variant<physics::Barrier, physics::BoundedBarrier>>   barriers_;
    std::vector<std::unique_ptr<physics::Spring>>                          springs_;
    physics::BroadphaseSAP                                                 sap_;
    std::vector<std::pair<physics::Hull*, physics::Hull*>>                 sapPairs_;
    physics::ContactCache                                                  contactCache_;
    physics::IslandDetector                                                islandDetector_;
    std::unique_ptr<ThreadPool>                                            threadPool_;
    int                                                                    lastIslandCount_ = 0;
    std::vector<std::unique_ptr<RenderMesh>>                               debugMeshes_;

    void rebuildCaches();

    // Creates a new SceneObject with a hull, registers it, rebuilds caches. Returns raw ptr.
    template<class HullT, class... Args>
    SceneObject* makeHullObject(Args&&... args);
};
