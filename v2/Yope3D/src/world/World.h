#pragma once
#include <memory>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include "RenderMesh.h"
#include "Transform.h"
#include "../rendering/Light.h"
#include "../ecs/Registry.h"
#include "../assets/ObjLoader.h"
#include "../math/Vec3.h"
#include "../physics/Spring.h"
#include "../physics/BroadphaseSAP.h"
#include "../physics/IslandDetector.h"
#include "../physics/PhysicsConstants.h"
#include "../physics/ContactCache.h"
#include "../physics/CollisionLayers.h"
#include "../physics/DebugShapes.h"

class GpuDevice;
class ThreadPool;

// World — owns all meshes in meshPool_. ECS registry owns all physics/transform data.
// Physics inner loop reads/writes ecs::Hull + Transform directly.
class World {
public:
    World();
    ~World();

    void init(GpuDevice& gpu, VkCommandPool pool);
    void cleanup();

    // ---- Scene objects ----
    ecs::Entity addSphere    (float mass, float radius, math::Vec3 pos = {});
    ecs::Entity addOBB       (math::Vec3 extent, float mass, math::Vec3 pos = {});
    ecs::Entity addAABB      (math::Vec3 extent, float mass, math::Vec3 pos = {});
    ecs::Entity addStaticAABB(math::Vec3 pos, math::Vec3 extent);
    ecs::Entity addOBBFromMesh(const LoadedMesh& mesh, float mass);

    // Visual-only entity (mesh, no physics body).
    ecs::Entity addRenderObject(const std::vector<Vertex>&   vertices,
                                const std::vector<uint32_t>& indices);
    ecs::Entity addRenderObject(const LoadedMesh& mesh);

    // Attach a mesh to an existing entity. Returns the new RenderMesh* for configuration.
    RenderMesh* attachMesh(ecs::Entity e,
                           const std::vector<Vertex>&   vertices,
                           const std::vector<uint32_t>& indices);
    RenderMesh* attachMesh(ecs::Entity e, const LoadedMesh& mesh);

    RenderMesh* getMesh(ecs::Entity e);
    void        removeEntity(ecs::Entity e);

    // Add / remove a physics body on an existing entity (editor "Add Component").
    // Silently no-ops if the entity already has a collider (or is invalid).
    void attachSphereCollider(ecs::Entity e, float mass, float radius, bool isStatic = false);
    void attachAABBCollider  (ecs::Entity e, float mass, math::Vec3 extent, bool isStatic = false);
    void attachOBBCollider   (ecs::Entity e, float mass, math::Vec3 extent, bool isStatic = false);
    // Remove all physics components (Hull + shape + Fixed/Sleeping tags).
    void detachPhysicsBody(ecs::Entity e);

    // Call once per editor tick, before the Vulkan command buffer is opened.
    // Syncs the device and destroys any RenderMeshes queued by removeEntity().
    void flushPendingGpuDestroys();

    int getHullCount();

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
    ecs::Registry&       getRegistry()       { return registry_; }
    const ecs::Registry& getRegistry() const { return registry_; }

    // ---- Lights ----
    ecs::Entity addLight(const Light& light);
    void        removeLight(int index);

    // ---- Audio ----
    // Create an audio-source entity at pos with an empty AudioSource (no Source* bound).
    // The user binds a .wav by dropping it onto the inspector's audio source drop target.
    ecs::Entity addAudioSourceEntity(math::Vec3 pos);

    // Wire the AudioSystem so removeEntity can deallocate orphaned OpenAL sources.
    void setAudioSystem(class AudioSystem* a) { audio_ = a; }
    int  getLightCount() const { return static_cast<int>(lightEntities_.size()); }

    // ---- Simulation ----
    void advance(float dt);
    void resetPhysics();

    // Edit-mode pause: physics thread's advance() is a no-op while paused.
    // Zero cost in runtime (atomic load of a false value on every tick).
    std::atomic<bool> paused_{ false };
    void setPaused(bool p) { paused_.store(p, std::memory_order_release); }

    void publishSnapshot();
    void syncRenderMeshesFromFront();

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

#ifdef YOPE_EDITOR
    // Take a snapshot of the entire scene before Play, then restore it on Stop.
    // GPU resources (vertex/index buffers) survive unchanged; only component state
    // and meshPool_/springs_ entries added during play are cleaned up on restore.
    void snapshotForPlay();
    void restoreFromPlay();
#endif

    World(const World&) = delete;
    World& operator=(const World&) = delete;

private:
    struct TransformSnapshot {
        math::Vec3  pos;
        math::Quat  rot;
        math::Vec3  scale;
        RenderMesh* mesh;
        ecs::Entity entity;
    };
    struct SpringSnapshot {
        math::Vec3  pos;
        math::Quat  rot;
        math::Vec3  scale;
        RenderMesh* mesh;
        ecs::Entity entity;
    };

    GpuDevice*    gpu_   = nullptr;
    VkCommandPool pool_  = VK_NULL_HANDLE;
    AudioSystem*  audio_ = nullptr;   // wired by Engine; used to free Sources on removeEntity

    std::vector<TransformSnapshot> snapshotBack_, snapshotFront_;
    std::vector<SpringSnapshot>    springSnapshotBack_, springSnapshotFront_;
    std::mutex snapshotMtx_;

    // Guards pools/springs_ against concurrent advance() and add/remove calls.
    // Recursive because addSpringWith* internally calls addSphere/addRenderObject.
    std::recursive_mutex structureMtx_;

    std::vector<std::unique_ptr<RenderMesh>> meshPool_;
    // Meshes removed from meshPool_ but not yet GPU-destroyed.
    // Flushed (with a device sync) at the start of each editor tick, before
    // the command buffer is opened, so VkBuffers are never destroyed while
    // they're still referenced by an in-flight or currently-recording command buffer.
    std::vector<std::unique_ptr<RenderMesh>> pendingGpuDestroy_;

    ecs::Registry                                        registry_;
    std::vector<ecs::Entity>                             lightEntities_;
    std::unordered_map<RenderMesh*, ecs::Entity>         meshToEntity_;

    std::vector<std::unique_ptr<physics::Spring>>        springs_;
    physics::BroadphaseSAP                               sap_;
    std::vector<ecs::Entity>                             advanceEntities_;    // reused each tick
    std::vector<std::pair<ecs::Entity, ecs::Entity>>     sapPairs_;
    std::vector<physics::ColliderDiscrete::ActiveContact> advanceContacts_;   // reused each tick
    physics::EntityContactCache                          contactCache_;
    physics::IslandDetector                              islandDetector_;
    std::unique_ptr<ThreadPool>                          threadPool_;
    int                                                  lastIslandCount_ = 0;
    std::vector<std::unique_ptr<RenderMesh>>             debugMeshes_;
    std::vector<ecs::Entity>                             debugEntities_;

#ifdef YOPE_EDITOR
    // Called at the end of every public factory method to stamp editor-visible tags.
    void finalizeEntity(ecs::Entity e, const char* name);

    struct PlaySnapshot {
        ecs::Registry::Snapshot registry;
        math::Vec3               gravity;
        physics::CollisionLayers layers;
    };
    PlaySnapshot playSnapshot_;
    size_t       prePlayMeshPoolSize_ = 0;
    size_t       prePlaySpringCount_  = 0;
#endif

};
