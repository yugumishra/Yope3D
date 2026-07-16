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
#include "../ecs/Components.h"
#include "../assets/ObjLoader.h"
#include "../math/Vec2.h"
#include "../math/Vec3.h"
#include "../math/Vec4.h"
#include "../physics/Spring.h"
#include "../physics/Joint.h"
#include "../physics/BroadphaseSAP.h"
#include "../physics/IslandDetector.h"
#include "../physics/PhysicsConstants.h"
#include "../physics/ContactCache.h"
#include "../physics/CompoundShape.h"
#include "../physics/CollisionLayers.h"
#include "../physics/DebugShapes.h"
#include "../rendering/DebugLine.h"
#include "../ui/UIInput.h"
#include "../ui/Tween.h"

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
    // Trigger volumes: static, tangible, isTrigger=true — stay in broadphase/narrowphase
    // for enter/exit events but are never solved (no physical push-back).
    ecs::Entity addTriggerBox   (math::Vec3 pos, math::Vec3 extent);
    ecs::Entity addTriggerSphere(math::Vec3 pos, float radius);
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
    // Remove all physics components (Hull + shape + Fixed tag).
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

    // ---- Joints ----
    // Bilateral (push AND pull) constraint, solved inside the island-parallel
    // PGS loop — unlike Spring, which is a soft one-shot force applied globally
    // after the solve (see physics/Joint.h for why joints can't get away with
    // that). anchorWorld is converted to each body's local-space offset at
    // creation time; the caller (typically an editor command) computes it —
    // by convention, the geometric midpoint of a's and b's current positions.
    physics::Joint* addPointJoint(ecs::Entity a, ecs::Entity b, math::Vec3 anchorWorld);

    // Low-level: create the physics::Joint object only (no ECS component), from
    // already-local-space anchors. Used by the editor command system and scene
    // serializer so the PointJointConstraint component that lives on entity A
    // drives the physics without creating a 3rd entity (mirrors addSpringPhysics).
    physics::Joint* addPointJointPhysics(ecs::Entity a, ecs::Entity b,
                                         math::Vec3 localAnchorA, math::Vec3 localAnchorB);

    // Hinge (revolute): axisWorld is a direction (not a point), converted to
    // each body's local frame the same way anchorWorld is. limitEnabled/lower/
    // upperAngle are optional angle-limit params (radians, about the axis).
    physics::Joint* addHingeJoint(ecs::Entity a, ecs::Entity b, math::Vec3 anchorWorld, math::Vec3 axisWorld,
                                  bool limitEnabled = false, float lowerAngle = 0.0f, float upperAngle = 0.0f);
    physics::Joint* addHingeJointPhysics(ecs::Entity a, ecs::Entity b,
                                         math::Vec3 localAnchorA, math::Vec3 localAnchorB,
                                         math::Vec3 localAxisA, math::Vec3 localAxisB,
                                         bool limitEnabled, float lowerAngle, float upperAngle);

    // Cone-twist (swing-twist): twistAxisWorld is each body's "bone" direction;
    // swingLimit/twistLimit are half-angle radians.
    physics::Joint* addConeTwistJoint(ecs::Entity a, ecs::Entity b, math::Vec3 anchorWorld, math::Vec3 twistAxisWorld,
                                      float swingLimit = 0.785398f, float twistLimit = 0.785398f);
    physics::Joint* addConeTwistJointPhysics(ecs::Entity a, ecs::Entity b,
                                             math::Vec3 localAnchorA, math::Vec3 localAnchorB,
                                             math::Vec3 localTwistAxisA, math::Vec3 localTwistAxisB,
                                             float swingLimit, float twistLimit);

    // Remove the first physics::Joint whose endpoints match {a,b} (in either order).
    void removeJointBetween(ecs::Entity a, ecs::Entity b);

    // Re-syncs an existing live joint's persistent fields (anchors/axes/limits)
    // from its ECS mirror component. The mirror is what the Inspector edits —
    // without this, editing a joint's fields after creation would silently do
    // nothing, since the live physics::Joint keeps whatever values it was
    // created with. Also resets that joint's warm-start accumulators (built for
    // the old geometry/limits, now stale). No-op if no matching live joint
    // exists. Called by the editor inspectors after each committed field edit.
    void resyncPointJoint(ecs::Entity e, const ecs::PointJointConstraint& c);
    void resyncHingeJoint(ecs::Entity e, const ecs::HingeJointConstraint& c);
    void resyncConeTwistJoint(ecs::Entity e, const ecs::ConeTwistJointConstraint& c);

    // ---- Vehicles (raycast wheels — SuspensionJoint + WheelFrictionJoint) ----
    // Code-level API only (no generic "Add Joint" editor dropdown entry —
    // a vehicle is a whole rig, not a single two-body joint). All fields in
    // localPos/local space are chassis-local.
    struct WheelSpec {
        math::Vec3 localPos{};             // wheel mount point, chassis-local
        math::Vec3 localUp{0.0f, 1.0f, 0.0f};
        float      restLength = 0.4f;
        float      maxTravel  = 0.3f;
        float      stiffness  = 40000.0f;  // N/m
        float      damping    = 4000.0f;   // N*s/m
        float      radius     = 0.35f;
        float      muLong = 1.2f, muLat = 1.2f;
        bool       driven = true;          // false = free-rolling (no drive/brake row)
    };

    // Builds one Suspension+WheelFriction joint pair per wheel spec on
    // `chassis` (must already have a Hull). Returns one opaque handle per
    // wheel (the WheelFrictionJoint*, wrapped as physics::Joint* for the
    // Python binding's benefit) in the same order as `wheels`, for later
    // control via setWheelDrive/setWheelSteer.
    std::vector<physics::Joint*> addVehicle(ecs::Entity chassis, const std::vector<WheelSpec>& wheels);

    // Sets a wheel's target angular velocity (rad/s) — the longitudinal
    // friction row's slip target, i.e. throttle (positive) / brake (toward
    // zero) / reverse (negative). `wheel` is a handle returned by addVehicle.
    void setWheelDrive(physics::Joint* wheel, float angularVel);

    // Sets a wheel's steer angle (radians, about its suspension's up axis).
    void setWheelSteer(physics::Joint* wheel, float steerAngleRad);

    // ---- ECS registry ----
    ecs::Registry&       getRegistry()       { return registry_; }
    const ecs::Registry& getRegistry() const { return registry_; }

    // Give an entity a Name plus (editor builds only) EditorSelectable +
    // EditorPickable. Called at the end of every public factory method; also
    // exposed for setup/scene scripts that spawn raw entities and need them to
    // be saved by the scene serializer (its save loop iterates EditorSelectable).
    void finalizeEntity(ecs::Entity e, const char* name);

    // Returns a scoped lock on the structure mutex. Use to synchronize registry
    // iteration on the main thread against concurrent archetype migrations
    // (e.g. Fixed-tag toggles) racing World::advance().
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
    // Interactive button: UITransform + UIButton. `normalColor` seeds all four
    // visual states (hover/pressed brighten/darken it, disabled halves alpha);
    // fine-tune individual states afterward via reg_get(e, "UIButton").
    ecs::Entity addUIButton            (math::Vec2 min, math::Vec2 max,
                                        math::Vec4 normalColor, int depth = 0);
    // 3D world-space text: Transform (anchor) + ecs::TextLabel3D.
    ecs::Entity addTextLabel3D         (const char* fontPath, const char* text, math::Vec3 pos);

    // ---- UI input routing (screen-space pointer -> ECS UI entities) ----
    // Called once per frame by Engine after cursor pos + per-button edge flags
    // are known. Returns this frame's hover/press/release/click events; Engine
    // dispatches them to each target entity's ScriptComponent instance (same
    // pattern as drainCollisionEvents).
    std::vector<UIInputEvent> updateUIInput(float cx, float cy, float screenW, float screenH,
                                             const bool* pressedEdge, const bool* releasedEdge,
                                             int numButtons);
    // Re-runs the topmost hit-test only, no state change — polled API for scripts.
    ecs::Entity uiHitTest(float cx, float cy);
    // Resolve any UITexturedBackground whose `texture` is still null (freshly
    // created, or its `path` was just edited from Python) from `path` via the
    // wired AssetManager. Cheap no-op once every entry is resolved. Call once
    // per frame from the main thread (Engine::update).
    void resolvePendingUITextures();
    bool        uiConsumedClick() const { return uiInput_.consumedClick(); }
    ecs::Entity uiHovered()       const { return uiInput_.hovered(); }
    ecs::Entity uiPressed()       const { return uiInput_.pressed(); }
    ecs::Entity uiFocused()       const { return uiInput_.focused(); }
    void        setUIFocus(ecs::Entity e) { uiInput_.setFocus(e); }

    // ---- UI hierarchy / panel grouping ----
    // Groups a set of UI entities so moving/hiding/fading the root affects the
    // whole subtree (see ui::resolveUIRectWorld). Reuses ecs::Parent (already
    // the 3D transform-hierarchy link) — a child's own UITransform becomes a
    // [0,1] rect local to the parent's resolved rect rather than the screen.
    // No-ops on a cycle (parent already a descendant of child) or an invalid
    // entity. Pass NullEntity as parent to un-parent (root again).
    void setUIParent(ecs::Entity child, ecs::Entity parent);
    // Sets `visible` on root and every UI descendant (one-shot bulk write —
    // visibility also composes live via the Parent chain, so this is a
    // convenience for "toggle a whole menu" rather than required for correctness).
    void setUIGroupVisible(ecs::Entity root, bool visible);
    // Sets `opacity` on root and every UI descendant (one-shot bulk write;
    // opacity composes live too — prefer animating just the root's own
    // opacity for a fade unless you specifically want to flatten the group).
    void setUIGroupOpacity(ecs::Entity root, float opacity);

    // ---- UI tweening ----
    // Animates `root`'s own UITransform::opacity from its current value to
    // `target` over `durationSeconds` (composes down to descendants live via
    // the Parent chain — see resolveUIRectWorld). Re-targeting an entity with
    // an in-flight tween replaces it, starting from the current opacity.
    // `ease` is a ui::Ease value (see yope3d.EASE_* constants). No-op on an
    // invalid entity or one with no UITransform.
    void tweenUIOpacity(ecs::Entity root, float target, float durationSeconds, int ease = 0);
    // Advances all active tweens by dt. Called once per frame from the main
    // thread (Engine::update) — UITransform is main-thread-only state.
    void updateTweens(float dt);

    // Wire the AudioSystem so removeEntity can deallocate orphaned OpenAL sources.
    void setAudioSystem(class AudioSystem* a) { audio_ = a; }
    void setAssetManager(AssetManager* a) { assets_ = a; }

    // When set (non-null), addRenderObject/attachMesh record their GPU mesh
    // uploads into this batch instead of doing a blocking per-buffer submit. The
    // async scene-load commit pump sets it for the duration of a per-frame batch,
    // then submits once and clears it. Null everywhere else (runtime/editor mesh
    // creation stays synchronous). Not thread-safe — only set on the main thread
    // while the physics thread is stopped.
    void setUploadBatch(BufferUploadBatch* b) { activeUploadBatch_ = b; }

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

    // ---- Shadow caster (single, generic — any light type with castsShadow set) ----
    // setShadowCaster acts as a radio button: flags `e`'s LightSource.castsShadow and
    // clears it on every other light. Pass NullEntity (or call clearShadowCaster) to
    // disable shadows entirely.
    void setShadowCaster(ecs::Entity e);
    void clearShadowCaster() { setShadowCaster(ecs::NullEntity); }
    // Lazily re-validates/re-scans the registry for the flagged caster (self-heals
    // after deserialization, undo/redo, or entity deletion — no scene-load hook
    // needed). Returns NullEntity if no light is flagged.
    ecs::Entity getShadowCaster();

    // ---- Simulation ----
    void advance(float dt);
    void resetPhysics();

    // ---- Script physics helpers (lock structure internally; safe vs the physics thread) ----
    // Apply a linear impulse (kg·m/s) and wake the body. No-op on static/invalid bodies.
    void applyImpulse  (ecs::Entity e, math::Vec3 impulse);
    // Apply an impulse at a world-space point — yields both linear and angular change.
    void applyImpulseAt(ecs::Entity e, math::Vec3 impulse, math::Vec3 worldPoint);
    // Clear the Hull's asleep flag (and zero sleepFrames) so direct velocity writes take effect.
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

    // ---- Shadow tuning (World Settings) ----
    // shadowBias: NDC-space depth-compare bias, last-resort safety margin on top of
    //   the normal-offset below (see triangle.frag) and the shadow pipeline's
    //   rasterizer depth bias. shadowNormalBias: world-space offset along the
    //   surface normal before the light-space transform — the primary acne fix for
    //   grazing-angle surfaces (curved geometry, box corners/edges seen edge-on);
    //   too small and those surfaces show acne, too large and shadows detach
    //   (peter-panning). shadowPcfRadius: multiplier on the fixed-texel PCF kernel
    //   spread (1.0 = 1 texel). shadowOrthoHalfExtent/shadowOrthoFar: size of the
    //   directional-caster's camera-centered ortho box — smaller = sharper/more
    //   precise shadows but a smaller shadowed radius around the camera.
    //   shadowSpotNear/shadowSpotFar: near/far planes of the spot-caster's
    //   perspective shadow frustum. Keep the near plane as large as the scene
    //   allows and the far plane no larger than the light's actual reach —
    //   perspective depth precision is dominated by their ratio, and a too-small
    //   near (or too-large far) crushes all occluder depths toward 1.0, producing
    //   unstable, detached ("peter-panned") blob shadows.
    //   shadowPointNear/shadowPointFar: near/far planes of a point-caster's 6
    //   per-face 90-degree perspective frustums (see PointShadowMap.h /
    //   computeShadowLightViewProjPointFaces) — same precision tradeoff as
    //   shadowSpotNear/Far, just symmetric in all 6 directions instead of one cone.
    float shadowBias             = 0.0006f;
    float shadowNormalBias       = 0.035f;
    float shadowPcfRadius        = 1.0f;
    float shadowOrthoHalfExtent  = 20.0f;
    float shadowOrthoFar         = 40.0f;
    float shadowSpotNear         = 1.0f;
    float shadowSpotFar          = 30.0f;
    float shadowPointNear        = 0.05f;
    float shadowPointFar         = 25.0f;

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
    BufferUploadBatch* activeUploadBatch_ = nullptr;  // non-null during async-load commit batches
    AudioSystem*  audio_ = nullptr;   // wired by Engine; used to free Sources on removeEntity
    AssetManager* assets_ = nullptr;  // wired by Engine; used by addModel for glTF textures

    std::array<std::string, 6> skyboxFaces_{};
    bool                       skyboxDirty_ = false;
    bool                       hasSkybox_   = false;

    ecs::Entity shadowCaster_ = ecs::NullEntity;  // cache; see getShadowCaster()

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
    // unique_ptr purely for pointer stability across vector growth (same
    // rationale as springs_) — Joint is a std::variant, not polymorphic.
    std::vector<std::unique_ptr<physics::Joint>>         joints_;
    physics::BroadphaseSAP                               sap_;
    std::vector<ecs::Entity>                             advanceEntities_;    // reused each tick
    std::vector<std::pair<ecs::Entity, ecs::Entity>>     sapPairs_;
    std::vector<physics::ColliderDiscrete::ActiveContact> advanceContacts_;   // reused each tick; solver-bound
    std::vector<physics::ColliderDiscrete::ActiveContact> triggerContacts_;   // reused each tick; events only, never solved
    physics::EntityContactCache                          contactCache_;
    physics::IslandDetector                              islandDetector_;
    std::unique_ptr<ThreadPool>                          threadPool_;
    int                                                  lastIslandCount_ = 0;
    std::vector<std::unique_ptr<RenderMesh>>             debugMeshes_;
    std::vector<ecs::Entity>                             debugEntities_;
    std::unordered_map<uint32_t, math::Vec3>            debugColorOverrides_;  // entity.id -> overlay color
    std::vector<DebugLineVertex>                        debugLines_;           // GJK CSO / simplex viz

    UIInputRouter uiInput_;
    float uiScreenW_ = 0.0f, uiScreenH_ = 0.0f;  // cached from the last updateUIInput() call

    struct UIOpacityTween {
        ecs::Entity entity;
        float from, to;
        float duration, elapsed;
        ui::Ease ease;
    };
    std::vector<UIOpacityTween> uiTweens_;

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
    size_t       prePlayJointCount_   = 0;
#endif

};
