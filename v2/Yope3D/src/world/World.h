#pragma once
#include <memory>
#include <vector>
#include <array>
#include <string>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include "RenderMesh.h"
#include "Transform.h"
#include "../rendering/Light.h"
#include "../ecs/Registry.h"
#include "../assets/ObjLoader.h"
#include "../math/Vec2.h"
#include "../math/Vec3.h"
#include "../math/Vec4.h"
#include "../physics/Spring.h"
#include "../physics/BroadphaseSAP.h"
#include "../physics/IslandDetector.h"
#include "../physics/PhysicsConstants.h"
#include "../physics/ContactCache.h"
#include "../physics/CompoundShape.h"
#include "../physics/CollisionLayers.h"
#include "../physics/DebugShapes.h"
#include "../rendering/DebugLine.h"

class GpuDevice;
class ThreadPool;
class AssetManager;

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
    // GJK-only primitives (axis +Y; dims baked into mesh, Transform.scale stays {1,1,1})
    ecs::Entity addCapsule          (float radius, float halfHeight, float mass, math::Vec3 pos = {});
    ecs::Entity addCylinder         (float radius, float halfHeight, float mass, math::Vec3 pos = {});
    // Kinematic capsule: Transform + CapsuleForm only — no Hull, physics sim ignores it.
    ecs::Entity addKinematicCapsule (float radius, float halfHeight, math::Vec3 pos = {});

    // Visual-only entity (mesh, no physics body).
    ecs::Entity addRenderObject(const std::vector<Vertex>&   vertices,
                                const std::vector<uint32_t>& indices);
    ecs::Entity addRenderObject(const LoadedMesh& mesh);

    // Load a model (.obj = single mesh; .gltf/.glb = one entity per primitive)
    // from a path relative to YOPE_ASSETS_DIR. Attaches an ecs::Material when the
    // source defines one. Uses the Engine-wired AssetManager for glTF textures.
    std::vector<ecs::Entity> addModel(const std::string& path);

    // Same as addModel but takes an absolute filesystem path (e.g. from the
    // editor's native file picker / asset drag-drop, which may live outside the
    // assets tree). Embedded glTF textures load; external-URI textures resolve
    // relative to YOPE_ASSETS_DIR (loader limitation).
    std::vector<ecs::Entity> importModel(const std::string& absPath);

    // Re-decode and re-register a glTF's embedded images (under their "<glb>#imgN"
    // keys) without creating any entities. Scene load calls this so materials that
    // reference embedded textures resolve after a fresh load (the pixel data isn't
    // stored in the scene JSON — only the synthetic key is). No-op on failure.
    void reregisterEmbeddedTextures(const std::string& glbAbsPath);

    // Attach a mesh to an existing entity. Returns the new RenderMesh* for configuration.
    RenderMesh* attachMesh(ecs::Entity e,
                           const std::vector<Vertex>&   vertices,
                           const std::vector<uint32_t>& indices);
    RenderMesh* attachMesh(ecs::Entity e, const LoadedMesh& mesh);

    RenderMesh* getMesh(ecs::Entity e);
    void        removeEntity(ecs::Entity e);
    // Show/hide an entity's render mesh without destroying it (blinking pickups, etc.).
    void        setMeshVisible(ecs::Entity e, bool visible);

    // Add / remove a physics body on an existing entity (editor "Add Component").
    // Silently no-ops if the entity already has a collider (or is invalid).
    void attachSphereCollider  (ecs::Entity e, float mass, float radius, bool isStatic = false);
    void attachAABBCollider    (ecs::Entity e, float mass, math::Vec3 extent, bool isStatic = false);
    void attachOBBCollider     (ecs::Entity e, float mass, math::Vec3 extent, bool isStatic = false);
    void attachCapsuleCollider (ecs::Entity e, float mass, float radius, float halfHeight, bool isStatic = false);
    void attachCylinderCollider(ecs::Entity e, float mass, float radius, float halfHeight, bool isStatic = false);

    // ---- Static compound collider (baked level geometry) ----
    // Owns a per-asset-path cache of CompiledCollider (shared across instances).
    // buildCompoundCollider caches an in-memory build (used by the baker / tests);
    // loadCompoundCollider loads a cooked .bcbvh from an asset-relative path.
    // Both return a non-owning pointer stable for World's lifetime.
    physics::CompiledCollider* buildCompoundCollider(const std::string& key,
                                                     std::vector<physics::SubShape> shapes,
                                                     int leafSize = 4);
    // forceReload re-reads the file even if `assetRelPath` is already cached — used
    // by the editor's "Regenerate" action. Mutates the cached CompiledCollider's
    // contents in place (same address) rather than swapping the cache slot, so any
    // ecs::CompoundCollider::compiled pointers already handed out stay valid.
    physics::CompiledCollider* loadCompoundCollider(const std::string& assetRelPath,
                                                    bool forceReload = false);
    // Adds a body carrying `compiled`; `assetPath` is stored on the component for
    // serialization ("" for a pure in-memory build). isStatic=true (default)
    // preserves prior behavior: Fixed tag, mass 0, zero inertia. isStatic=false
    // makes it a dynamic rigid body: mass defaults to the baked compiled->totalMass
    // when `mass` <= 0, inverseInertia comes from compiled->inverseInertiaLocal, no
    // Fixed tag, gravity enabled. `density` is stored on the component purely as
    // bake-time bookkeeping (what to prefill next time this gets re-baked) — it
    // does not affect this call's own mass/inertia (those come from `compiled`).
    ecs::Entity attachCompoundCollider(ecs::Entity e, physics::CompiledCollider* compiled,
                                       const std::string& assetPath,
                                       float mass = 0.0f, bool isStatic = true,
                                       float density = 1.0f);
    // Remove all physics components (Hull + shape + Fixed/Sleeping tags).
    void detachPhysicsBody(ecs::Entity e);

    // Call once per editor tick, before the Vulkan command buffer is opened.
    // Syncs the device and destroys any RenderMeshes queued by removeEntity().
    void flushPendingGpuDestroys();

    // Swap the capsule entity's render mesh for a freshly baked one at its current
    // CapsuleForm dims. Old GPU buffers are freed on the next flushPendingGpuDestroys().
    // Only needed for capsules (baked+identity-scale approach); cylinders use unit+scale.
    void rebuildCapsuleMesh(ecs::Entity e);


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

    // Low-level: create the physics::Spring object only (no ECS entity, no component).
    // Used by the editor command system and scene serializer so the SpringConstraint
    // component that lives on entity A drives the physics without creating a 3rd entity.
    void addSpringPhysics(ecs::Entity a, ecs::Entity b, float k, float rest);

    // Remove the first physics::Spring whose endpoints match {a,b} (in either order).
    // Used by AddSpringConstraintCommand::undo().
    void removeSpringBetween(ecs::Entity a, ecs::Entity b);

    // ---- ECS registry ----
    ecs::Registry&       getRegistry()       { return registry_; }
    const ecs::Registry& getRegistry() const { return registry_; }

    // Returns a scoped lock on the structure mutex. Use to synchronize registry
    // iteration on the main thread against concurrent archetype migrations
    // (e.g. Sleeping-tag additions) in World::advance().
    std::unique_lock<std::recursive_mutex> lockStructure() {
        return std::unique_lock<std::recursive_mutex>(structureMtx_);
    }

    // ---- Lights ----
    ecs::Entity addLight(const Light& light);
    void        removeLight(int index);
    void        removeLight(ecs::Entity e);   // by entity (what scripts hold from add_point_light)

    // ---- Audio ----
    // Create an audio-source entity at pos with an empty AudioSource (no Source* bound).
    // The user binds a .wav by dropping it onto the inspector's audio source drop target.
    ecs::Entity addAudioSourceEntity(math::Vec3 pos);

    // ---- UI entities (screen-space, no physics) ----
    // Coordinates are in [0,1] screen percentage, top-left origin.
    ecs::Entity addUIBackground        (math::Vec2 min, math::Vec2 max,
                                        math::Vec4 color, int depth = 0);
    ecs::Entity addUITexturedBackground(math::Vec2 min, math::Vec2 max,
                                        math::Vec4 tint, const char* texPath, int depth = 0);
    ecs::Entity addUICurvedBackground  (math::Vec2 min, math::Vec2 max,
                                        math::Vec4 color, float curvature = 0.5f, int depth = 0);
    ecs::Entity addUIText              (const char* fontPath, const char* text,
                                        math::Vec2 min, math::Vec2 max, int depth = 0);
    // 3D world-space text: Transform (anchor) + ecs::TextLabel3D.
    ecs::Entity addTextLabel3D         (const char* fontPath, const char* text, math::Vec3 pos);

    // Wire the AudioSystem so removeEntity can deallocate orphaned OpenAL sources.
    void setAudioSystem(class AudioSystem* a) { audio_ = a; }
    void setAssetManager(AssetManager* a) { assets_ = a; }

    // Cubemap skybox (producer = scripts/editor, consumer = Renderer). Faces in
    // order +X,-X,+Y,-Y,+Z,-Z, asset-relative. The Renderer (re)loads it when dirty.
    void setSkybox(const std::array<std::string, 6>& faces) {
        skyboxFaces_ = faces; skyboxDirty_ = true; hasSkybox_ = true;
    }
    bool hasSkybox()    const { return hasSkybox_; }
    bool skyboxDirty()  const { return skyboxDirty_; }
    void clearSkyboxDirty()   { skyboxDirty_ = false; }
    const std::array<std::string, 6>& skyboxFaces() const { return skyboxFaces_; }
    int  getLightCount() const { return static_cast<int>(lightEntities_.size()); }

    // ---- Simulation ----
    void advance(float dt);
    void resetPhysics();

    // ---- Script physics helpers (lock structure internally; safe vs the physics thread) ----
    // Apply a linear impulse (kg·m/s) and wake the body. No-op on static/invalid bodies.
    void applyImpulse  (ecs::Entity e, math::Vec3 impulse);
    // Apply an impulse at a world-space point — yields both linear and angular change.
    void applyImpulseAt(ecs::Entity e, math::Vec3 impulse, math::Vec3 worldPoint);
    // Remove the Sleeping tag (and zero sleepFrames) so direct velocity writes take effect.
    void wake          (ecs::Entity e);
    // Monotonic physics-tick counter (incremented once per advance()).
    uint64_t getTickCount() const { return tickCount_.load(std::memory_order_relaxed); }

    // ---- Collision events (enter/exit) ----
    // The physics thread diffs the contact-pair set each tick and enqueues
    // transitions; the main thread drains them once per frame and dispatches to
    // behaviors. Enabled lazily by Engine only when behaviors exist (zero cost
    // for script-less / stress scenes — the whole diff is skipped).
    struct CollisionEvent { ecs::Entity a, b; bool enter; };
    void setCollisionEventsEnabled(bool e) { collisionEventsEnabled_.store(e, std::memory_order_relaxed); }
    std::vector<CollisionEvent> drainCollisionEvents();

    // Edit-mode pause: physics thread's advance() is a no-op while paused.
    // Zero cost in runtime (atomic load of a false value on every tick).
    std::atomic<bool> paused_{ false };
    void setPaused(bool p) { paused_.store(p, std::memory_order_release); }

    void publishSnapshot();
    void syncRenderMeshesFromFront();

    std::atomic<bool> newSnapshotReady_{ false };
    std::atomic<uint64_t> tickCount_{ 0 };   // bumped once per advance(); read by scripts

    int getIslandCount() const { return lastIslandCount_; }
    int getThreadCount() const;

    math::Vec3 gravity = {0.0f, physics::GRAVITY_Y, 0.0f};

    // Global scene exposure applied pre-tonemap in the PBR shader (World Settings).
    float exposure = 1.0f;

    physics::CollisionLayers layers;

    // ---- Physics debug overlay ----
    bool debugPhysics = false;
    void rebuildDebugMeshes();
    void syncDebugMeshes();
    void destroyDebugMeshes();
    const std::vector<std::unique_ptr<RenderMesh>>& getDebugMeshes() const { return debugMeshes_; }

    // Per-entity debug-overlay color override (used by the GJK oracle to paint
    // collision verdicts). syncDebugMeshes() applies these on top of the default
    // green; entities with no override stay green. Keyed by entity id.
    void setDebugColor(ecs::Entity e, math::Vec3 color);
    void clearDebugColors();
    size_t debugMeshCount() const { return debugMeshes_.size(); }

    // ---- Debug line overlay (GJK CSO / simplex visualizer) ----
    // Producer (the editor GJK stepper) sets a world-space LINE_LIST; the Renderer
    // uploads + draws it inside the 3D pass, independent of debugPhysics.
    void setDebugLines(std::vector<DebugLineVertex> lines) { debugLines_ = std::move(lines); }
    void clearDebugLines() { debugLines_.clear(); }
    const std::vector<DebugLineVertex>& getDebugLines() const { return debugLines_; }
    // Append one world-space segment (a→b) in `color`. Used by script yope3d.draw_line;
    // Engine clears debugLines_ each frame before scripts run, so segments are per-frame.
    void addDebugLine(math::Vec3 a, math::Vec3 b, math::Vec3 color);

    void toggleProxies(bool enabled);

#ifdef YOPE_EDITOR
    // Take a snapshot of the entire scene before Play, then restore it on Stop.
    // GPU resources (vertex/index buffers) survive unchanged; only component state
    // and meshPool_/springs_ entries added during play are cleaned up on restore.
    void snapshotForPlay();
    void restoreFromPlay();

    // Scene Script panel: same mesh-pool + registry capture as snapshotForPlay
    // but does NOT change the physics-paused state (physics stays paused in edit mode).
    void takeScriptSnapshot();
    void restoreScriptSnapshot();
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
    AssetManager* assets_ = nullptr;  // wired by Engine; used by addModel for glTF textures

    std::array<std::string, 6> skyboxFaces_{};
    bool                       skyboxDirty_ = false;
    bool                       hasSkybox_   = false;

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

    // Baked compound colliders, keyed by asset-relative path (or in-memory key).
    // Shared/immutable; ecs::CompoundCollider::compiled points into these.
    std::unordered_map<std::string, std::unique_ptr<physics::CompiledCollider>> compoundColliderCache_;

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
    std::unordered_map<uint32_t, math::Vec3>            debugColorOverrides_;  // entity.id -> overlay color
    std::vector<DebugLineVertex>                        debugLines_;           // GJK CSO / simplex viz

    // Called at the end of every public factory method.
    // Always adds ecs::Name; adds EditorSelectable/EditorPickable only in editor builds.
    void finalizeEntity(ecs::Entity e, const char* name);

    // Physics-thread: diff this tick's contact pairs vs. last tick's, enqueue enter/exit.
    void detectCollisionEvents();
    std::atomic<bool>           collisionEventsEnabled_{ false };
    std::mutex                  collisionEventMtx_;
    std::vector<CollisionEvent> collisionEvents_;
    std::unordered_map<uint64_t, std::pair<ecs::Entity, ecs::Entity>> prevContactPairs_;

#ifdef YOPE_EDITOR
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
