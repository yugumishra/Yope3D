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
#include "../assets/AnimationClip.h"
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
#include "../physics/ContactInfo.h"
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
    // (Query-driven character-controller body; distinct from the kinematic *rigid*
    // bodies below, which do carry a Hull and participate in the solve.)
    ecs::Entity addKinematicCapsule (float radius, float halfHeight, math::Vec3 pos = {});

    // Kinematic rigid bodies (moving platforms — limitations.md §4.3): a Hull with
    // Hull.kinematic=true (immovable by contacts, integrated by a script-set
    // velocity/omega, never sleeps). Dynamic bodies resting on one inherit its
    // motion through the normal solver. Mesh-less like the other physics factories
    // — call attach_box_mesh / attach_sphere_mesh after. See makeKinematic() to
    // convert an existing body.
    ecs::Entity addKinematicBox   (math::Vec3 pos, math::Vec3 extent);
    ecs::Entity addKinematicSphere(math::Vec3 pos, float radius);
    // Convert an existing Hull-bearing entity into a kinematic body in place
    // (zeroes inverse mass/inertia + gravity, disables sleep, removes the Fixed
    // tag if present). Takes the structure lock internally.
    void        makeKinematic(ecs::Entity e);

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

    // ---- Rigid (node-TRS) animation ----
    // Clips are registered by importModel() (glTF `animations`) and looked up by
    // name from World::animationClips_ — see AnimationClip.h for why they can't
    // live in the ecs::AnimationPlayer component itself.
    const std::unordered_map<std::string, std::unique_ptr<anim::Clip>>& animationClips() const {
        return animationClips_;
    }
    // Advances every playing ecs::AnimationPlayer by dt and samples its clip into
    // the bound entities' LOCAL Transforms. Called from World::advance() (runtime/
    // Play) and from the editor's edit-mode tick (in-place viewport preview) —
    // safe in both: writes only entities registered in animBindings_, which are
    // always render-only (never Hull roots).
    void updateAnimations(float dt);
    // Restores `root`'s bound entities to the local Transform captured at import
    // time (the glTF-authored rest pose) — used by the editor's Stop-preview
    // action so scrubbing/playing a clip is never a destructive edit. No-op if
    // `root` has no animation binding.
    void resetAnimationPose(ecs::Entity root);

    // Attach a clip-only glTF's animation(s) directly to `target`'s own
    // Transform — for reusable, single-object clips authored independently of
    // any model (e.g. an Empty keyframed in Blender and exported with
    // Include > Animations, no mesh needed), as opposed to importModel()'s
    // clips which are tied to the hierarchy imported alongside them. Every
    // channel is remapped onto `target` regardless of the source file's own
    // node indices — a clip-only file is expected to animate exactly one
    // object, so which node it names doesn't matter, only its channels do.
    // Re-attaching a clip already registered under the same key (path stem +
    // animation name) reuses it instead of re-parsing the file — the common
    // case of attaching one clip to many entities (e.g. "spin" on N coins).
    // Adds (or repoints) `target`'s ecs::AnimationPlayer to the first parsed
    // clip; other animations in the file are registered too, selectable from
    // the clip dropdown. `path` is asset-relative (mirrors addModel); returns
    // the attached clip's key, or "" on failure (invalid target / no
    // Transform / file has no animations).
    std::string attachAnimation(ecs::Entity target, const std::string& path);
    // Same as attachAnimation but takes an absolute filesystem path (editor
    // file-picker / asset-browser drag-drop, which may live outside the
    // assets tree — mirrors importModel vs. addModel).
    std::string attachAnimationAbs(ecs::Entity target, const std::string& absPath);

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
    // `contact` carries the deepest contact point/normal + the tick's accumulated
    // normal impulse on ENTER (all zero on EXIT). `layers` = the OR of both bodies'
    // Hull.observeLayers (default 0), used by the global observer dispatch to match
    // its subscribed mask without re-reading the Hulls.
    struct CollisionEvent {
        ecs::Entity a, b;
        bool enter;
        physics::ContactInfo contact;
        uint32_t layers = 0;
    };
    // Per-pair contact info accumulated (post-solve) from the solved island
    // manifolds + trigger manifolds, keyed by pairKey — the source detectCollisionEvents
    // diffs and the payload it attaches to each enter event. (Public because a
    // namespace-scope accumulation helper in World.cpp populates it.)
    struct PairContact {
        ecs::Entity a, b;
        math::Vec3  point{};
        math::Vec3  normal{};
        float       impulse   = 0.0f;   // summed normal impulse across the manifold
        float       bestDepth = -1e30f; // deepest point wins point/normal (compound: many manifolds/pair)
    };
    void setCollisionEventsEnabled(bool e) { collisionEventsEnabled_.store(e, std::memory_order_relaxed); }
    std::vector<CollisionEvent> drainCollisionEvents();

    // Global collision-observer gate (limitations.md §4.5). When non-zero, a pair
    // whose combined collisionLayer intersects this mask generates enter/exit
    // events even if neither side has a ScriptComponent — Engine feeds it from the
    // registered yope3d.observe_collisions subscriptions each frame. Zero (the
    // default, no subscribers) keeps the old script-only behavior exactly.
    void     setCollisionObserveMask(uint32_t m) { collisionObserveMask_.store(m, std::memory_order_relaxed); }
    uint32_t getCollisionObserveMask() const { return collisionObserveMask_.load(std::memory_order_relaxed); }

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

    // ---- Solver instrumentation (devlog toggles; all main-thread setters) ----

    // Warm-starting: reuse last step's converged contact impulses as this step's
    // initial guess. Turning it off is a demo, not an optimization — see
    // physics::ColliderDiscrete::setWarmStartEnabled for what it costs.
    void setWarmStart(bool on) { physics::ColliderDiscrete::setWarmStartEnabled(on); }
    bool getWarmStart() const  { return physics::ColliderDiscrete::warmStartEnabled(); }

    // Scales the WALL-CLOCK dt fed into the physics accumulator (Engine's physics
    // thread), NOT the step size — advance() still runs at a fixed PHYSICS_DT, so
    // slow motion is a slowed-down replay of the same deterministic sim rather
    // than a different (softer / stiffer) one. 0 freezes the sim; values above 1
    // are bounded by MAX_CATCHUP_STEPS (substeps per iteration) and
    // MAX_PHYSICS_ACCUMULATOR (retained-backlog ceiling), so a big number just
    // saturates the catch-up rate instead of exploding.
    void  setTimeScale(float s) { timeScale_.store(std::max(0.0f, s), std::memory_order_relaxed); }
    float getTimeScale() const  { return timeScale_.load(std::memory_order_relaxed); }

    // Last tick's solver-load counters. pairCount = broadphase candidate pairs;
    // contactCount = manifolds surviving narrowphase (matches the profiler CSV's
    // contact_count); contactPointCount = individual points inside those
    // manifolds, which is what the PGS loop actually iterates (a box-box pair is
    // one manifold but up to four points).
    int getPairCount()         const { return lastPairCount_; }
    int getContactCount()      const { return lastContactCount_; }
    int getContactPointCount() const { return lastContactPointCount_; }

    // ---- Contact-point overlay ----
    // With debugContacts on, the physics thread copies each solved manifold point
    // (world position, normal, penetration, converged normal impulse) out at the
    // end of advance(); the main thread turns them into debug lines via
    // emitContactDebugLines(). Off by default and the copy is skipped entirely
    // when off, so this costs nothing in a normal run.
    bool debugContacts = false;
    struct ContactDebugPoint {
        math::Vec3 point;
        math::Vec3 normal;
        float      depth   = 0.0f;   // penetration (m)
        float      impulse = 0.0f;   // converged normal lambda (N·s)
    };
    // Main thread. Appends a cross at each contact point plus its normal into
    // debugLines_, coloring by impulse magnitude relative to the largest impulse
    // in the same snapshot (blue = barely loaded, red = carrying the most force).
    // The scale is per-frame and relative — it shows the load *path* through a
    // stack, not absolute newtons.
    void emitContactDebugLines(float crossSize = 0.05f, float normalLen = 0.25f);

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

    // Rigid-animation clip store (keyed by "<modelStem>:<animName>", unique per
    // importModel() call) and the per-root node->entity binding table built
    // alongside it. `restLocal` is each bound entity's LOCAL Transform as
    // authored (captured once, at import) — the rest pose resetAnimationPose
    // restores to. Keyed by root.id (like debugColorOverrides_ above) since
    // ecs::Entity has no std::hash specialization.
    // `additive`: importModel() bindings are false — glTF channel semantics
    // make the sampled value the AUTHORITATIVE absolute local Transform for
    // whichever paths the clip animates, and it's authored together with the
    // same hierarchy it's bound to. attachAnimation() bindings are true — the
    // clip comes from an unrelated file with no relationship to the target's
    // placement in this scene, so the sampled value must be treated as a
    // delta from the clip's OWN frame-0 pose and composed onto `restLocal`
    // (the target's actual pose at attach time) rather than assigned outright
    // — otherwise attaching a clip would teleport the target to wherever the
    // clip's raw keyframe values happen to sit in its source file's space.
    struct AnimBinding {
        std::vector<ecs::Entity> nodeEntities;
        std::vector<Transform>   restLocal;
        bool                      additive = false;
    };
    std::unordered_map<std::string, std::unique_ptr<anim::Clip>> animationClips_;
    std::unordered_map<uint32_t, AnimBinding>                     animBindings_;
    // (clip key, time) last actually sampled+written for a given root (keyed by
    // root.id). updateAnimations skips the write entirely when !playing and
    // neither has changed since — otherwise a paused/stopped clip would
    // re-stamp its pose every tick and silently undo any gizmo/inspector
    // Transform edit made on the bound entities in between. Tracking the clip
    // key too (not just time) forces a resample when the Inspector switches
    // clips while paused at the same time value in both.
    struct AnimSampleKey { std::string clip; float time = 0.0f; };
    std::unordered_map<uint32_t, AnimSampleKey>                   animLastSampled_;

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
    int                                                  lastPairCount_ = 0;
    int                                                  lastContactCount_ = 0;
    int                                                  lastContactPointCount_ = 0;
    std::atomic<float>                                   timeScale_{ 1.0f };
    // Written by the physics thread at the end of advance(), drained by the main
    // thread in emitContactDebugLines(). Its own mutex rather than structureMtx_:
    // the main thread would otherwise block on a whole physics step just to draw.
    std::vector<ContactDebugPoint>                       contactDebug_;
    std::mutex                                           contactDebugMtx_;
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
    // Runs after the solve so `impulse` reflects this tick's converged lambdas.
    void detectCollisionEvents(const std::unordered_map<uint64_t, PairContact>& pairContacts);
    std::atomic<bool>           collisionEventsEnabled_{ false };
    std::atomic<uint32_t>       collisionObserveMask_{ 0 };
    std::mutex                  collisionEventMtx_;
    std::vector<CollisionEvent> collisionEvents_;
    // Keyed by pairKey; value keeps a/b + layers so an EXIT (pair gone this tick)
    // can still be emitted with the right entities and observer-layer mask.
    std::unordered_map<uint64_t, CollisionEvent> prevContactEvents_;

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
