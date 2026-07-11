#pragma once
#include "Entity.h"
#include "../math/Vec3.h"
#include "../math/Mat3.h"
#include "../physics/PhysicsConstants.h"
#include <cstdint>

class RenderMesh;
class Source;
class Script;
class Texture;
struct ResolvedMaterial;   // rendering/MaterialCache.h — runtime GPU handle for a Material
namespace physics { struct CompiledCollider; }   // physics/CompoundShape.h — baked compound body

namespace ecs {

// ---- Dynamic physics body (create-time snapshot of physics::Hull fields) ----
struct Hull {
    math::Vec3 velocity       {};
    math::Vec3 omega          {};
    float      mass           = 1.0f;
    float      inverseMass    = 1.0f;
    float      friction       = physics::PGS_DEFAULT_FRICTION;
    float      restitution    = physics::PGS_RESTITUTION;
    float      linearDamping  = physics::LINEAR_DAMPING;
    float      angularDamping = physics::ANGULAR_DAMPING;
    uint32_t   collisionLayer = 0xFFFFFFFF;
    uint32_t   collisionMask  = 0xFFFFFFFF;
    bool       gravity        = true;
    bool       tangible       = true;
    bool       sleepingEnabled = true;
    bool       isTrigger      = false;

    // Phase D fields: solve accumulators + cached tensors (populated by factory methods / publishSnapshot)
    math::Vec3 pseudoVel          {};
    math::Vec3 pseudoOmega        {};
    math::Vec3 linearImpulse      {};
    math::Vec3 angularImpulse     {};
    math::Mat3 inverseInertia     {};    // body-space inverse inertia tensor (set at factory time)
    math::Mat3 inertiaTensorWorld {};    // world-space, synced in publishSnapshot after integration
    int        sleepFrames        = 0;
};

// ---- Collider shapes ----
struct SphereForm {
    float radius = 1.0f;
};

struct AABBForm {
    math::Vec3 extent {};   // half-extents; rotation always identity
};

struct OBBForm {
    math::Vec3 extent {};   // half-extents; rotation lives in Transform
};

struct CapsuleForm {
    float radius     = 0.5f;
    float halfHeight = 1.0f;  // half the cylinder section length; axis +Y
};

struct CylinderForm {
    float radius     = 0.5f;
    float halfHeight = 1.0f;  // axis +Y
};

// ---- Compound collider (baked multi-shape body + mid-phase BVH) ----
// A single rigid body whose collision volume is many convex sub-shapes with a
// baked AABB BVH over them — used for large level geometry so the player can't
// walk through walls (static), or for a multi-part prop that tumbles/settles
// as one body under real inertia (dynamic). `assetPath` points at a cooked
// `.bcbvh` file (asset-relative); `compiled` is the shared runtime structure
// resolved lazily by World (non-owning, like Material::resolved / MeshRenderer::mesh).
// `density`/`isStatic` are bake-time choices, persisted so the editor's
// "Regenerate" action can re-bake with the same settings; they don't drive
// runtime behavior directly (World::attachCompoundCollider already applied
// them when this was baked/attached).
struct CompoundCollider {
    char                       assetPath[256] = {};
    physics::CompiledCollider* compiled       = nullptr;   // runtime-only; World-cache owned
    float                      density        = 1.0f;
    bool                       isStatic       = true;
};

// ---- Visual ----
struct MeshRenderer {
    RenderMesh* mesh = nullptr;   // non-owning; World still owns the RenderMesh
};

// ---- PBR material (metallic-roughness) ----
// Pairs with a MeshRenderer. The five *Path fields select texture maps (relative
// to YOPE_ASSETS_DIR; empty = the engine default for that slot). Factors multiply
// the sampled value. `resolved` caches the GPU descriptor set built by
// MaterialCache — runtime-only, rebuilt on load/edit, never serialized. When an
// entity has no Material, the renderer synthesises a default material from the
// RenderMesh's legacy texture/color instead.
struct Material {
    char  albedoPath[256]     = {};   // sRGB base color
    char  normalPath[256]     = {};   // tangent-space normal map (linear)
    char  metalRoughPath[256] = {};   // linear; glTF packs G=roughness, B=metallic
    char  occlusionPath[256]  = {};   // linear; R channel = ambient occlusion
    char  emissivePath[256]   = {};   // sRGB emissive
    float albedoFactor[4]     = {1, 1, 1, 1};
    float metallicFactor      = 1.0f;
    float roughnessFactor     = 1.0f;
    float emissiveFactor[3]   = {0, 0, 0};
    float normalScale         = 1.0f;
    ResolvedMaterial* resolved = nullptr;   // runtime-only; built lazily by MaterialCache
};

// ---- Lighting (flat POD superstruct — avoids std::variant, stays trivially relocatable) ----
struct LightSource {
    int   type           = 0;           // 0=Point, 1=Directional, 2=Spot, 3=Flash
    float color[3]       = {1, 1, 1};
    float intensity      = 1.0f;
    // Point / Spot
    float position[3]    = {};
    float constant       = 1.0f;
    float linear         = 0.0f;
    float quadratic      = 0.0f;
    // Directional / Spot
    float direction[3]   = {0, -1, 0};
    // Spot / Flash
    float innerConeAngle = 0.2f;
    float outerConeAngle = 0.5f;
    // "Scene Shadow Caster": single-caster enforced by World::setShadowCaster (radio
    // behavior — checking one light's flag clears the previous caster's). Point lights
    // are not a supported caster type (would need a cubemap).
    bool  castsShadow    = false;
};

// ---- Audio ----
// Source* is owned by AudioSystem; the path/gain/pitch/loop fields are the
// authoritative serializable state so the sound can be reconstructed across
// save/load and play/stop without preserving OpenAL handles.
struct AudioSource {
    Source* source = nullptr;          // non-owning; AudioSystem owns the Source
    char    path[256] = {};            // asset-relative path (e.g. "audios/hum.ogg")
    float   gain      = 1.0f;
    float   pitch     = 1.0f;
    bool    loop      = false;
    bool    autoplay  = false;
};

// ---- Spring constraint ----
struct SpringConstraint {
    Entity target     = NullEntity;
    float  k          = 1.0f;
    float  restLength = 1.0f;
};

// ---- Transform hierarchy ----
// Links an entity's Transform as LOCAL to `parent`'s frame; composed to world
// via world/TransformHierarchy.h. Absent / NullEntity parent => Transform is
// world-space (the default for all root entities). POD Entity ref survives
// Play/Stop for free (Registry snapshot preserves ids); the scene serializer
// resolves it across save/load through the fileId two-pass (see SceneSerializer).
struct Parent {
    Entity parent = NullEntity;
};

// ---- Identity / debug ----
struct Name {
    char value[64] = {};    // fixed buffer — satisfies trivially-relocatable mandate
};

// ---- Behavior script attachment ----
// scriptClass:  registered name (YOPE_REGISTER_SCRIPT). Authoritative, persists across save/load.
// paramsBlob:   raw JSON snippet for the script's per-instance params. "{}" by default.
//               (Sized for typical scripts; larger configs should reference external assets.)
// instance:     live Script*, populated only while play mode is active (editor) or while the
//               scene is loaded (runtime). null in editor edit mode. Owned externally — Engine
//               / World / SceneManager are responsible for explicit delete at well-defined points
//               (entity removal, scene swap, shutdown). Archetype migrations memcpy the pointer.
struct ScriptComponent {
    char    scriptClass[64]   = {};
    char    paramsBlob[2048]  = "{}";
    Script* instance          = nullptr;
};

// ---- Tag components (zero-content; presence encodes the condition) ----
struct Sleeping         {};   // physics: body has entered sleep state
struct Fixed            {};   // physics: body is stationary / infinite mass
struct EditorSelectable {};   // editor: show in hierarchy panel (Phase D)
struct EditorPickable   {};   // editor: render in ID buffer pass (Phase D)

// ---- 2D UI components ----
// All coordinates in [0,1] screen percentage, (0,0) top-left, Y down.
// Entities with UITransform are screen-space; they have no 3D Transform.

// Layout bounds + display properties shared by every UI element.
// anchor==0 (Free) is the legacy behavior: minX/minY/maxX/maxY are the [0,1]
// screen-fraction rect verbatim. Any other anchor (see ui::Anchor in
// UILayout.h) repositions relative to a screen corner/edge/center; sizeMode==1
// (Pixel) additionally sizes pixelWidth/pixelHeight in real screen pixels
// instead of a stretch-prone fraction. offsetXPx/offsetYPx push the rect
// inward from whichever edge(s) it's anchored to. Ignored when anchor==Free.
struct UITransform {
    float minX    = 0.0f, minY    = 0.0f;
    float maxX    = 1.0f, maxY    = 1.0f;
    int   depth   = 0;
    bool  visible = true;

    int   anchor      = 0;
    int   sizeMode    = 0;
    float pixelWidth  = 0.0f, pixelHeight = 0.0f;
    float offsetXPx   = 0.0f, offsetYPx   = 0.0f;

    // Own opacity multiplier, composed down an ecs::Parent chain by
    // ui::resolveUIRectWorld (UIHierarchy.h) — fading a parent fades every
    // descendant. Unlike `visible`, opacity 0 does not block hit-testing
    // (matches common UI-framework convention: fade vs. disable are separate).
    float opacity = 1.0f;
};

// Solid-color rectangle. Pair with UITransform.
struct UIBackground {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// Texture-modulated rectangle. Pair with UITransform.
// texture: runtime-only, loaded from path; path is the serializable state.
struct UITexturedBackground {
    char     path[256]  = {};
    Texture* texture    = nullptr;   // non-owning, runtime only
    float    tintR = 1.0f, tintG = 1.0f, tintB = 1.0f, tintA = 1.0f;
};

// Rounded-corner (bottom-arc) rectangle. curvature in [0,1]. Pair with UITransform.
struct UICurvedBackground {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
    float curvature = 0.5f;
};

// Interactive button. Pairs with UITransform. Renders as a solid-color rect
// whose color is chosen from the four states below based on World's UI input
// router (hover/press) and `enabled` — the component itself carries no click
// logic. Attach a ScriptComponent to the same entity to receive
// on_ui_press/on_ui_release/on_ui_enter/on_ui_leave (Script.h) from the router.
struct UIButton {
    float normalR = 0.20f, normalG = 0.20f, normalB = 0.20f, normalA = 1.0f;
    float hoverR = 0.30f, hoverG = 0.30f, hoverB = 0.30f, hoverA = 1.0f;
    float pressedR = 0.12f, pressedG = 0.12f, pressedB = 0.12f, pressedA = 1.0f;
    float disabledR = 0.15f, disabledG = 0.15f, disabledB = 0.15f, disabledA = 0.5f;
    // false: renders in the disabled state and is skipped by hit-testing
    // (clicks pass through to whatever's behind it).
    bool  enabled = true;
};

// Text label. fontPath selects a TextAtlas; text is the rendered string.
// displayPx: glyph height in pixels, 0 = native atlas size.
// alignment: 0 = left/DEFAULT, 1 = CENTERED.
struct UIText {
    char  fontPath[256] = {};
    char  text[1024]    = {};
    float cr = 1.0f, cg = 1.0f, cb = 1.0f, ca = 1.0f;
    int   displayPx = 0;
    int   alignment = 0;

    // When true, Renderer::buildECSUIGeometry snaps this entity's UITransform
    // to the natural (unwrapped) size of `text` at `displayPx` — anchor==Free
    // grows/shrinks maxX/maxY from the current minX/minY; any other anchor
    // sets sizeMode=Pixel + pixelWidth/pixelHeight instead, keeping the
    // authored anchor/offset. Recomputed only when `text` differs from
    // `autoSizedText` (a cache, not user-facing) — so a manual resize in the
    // editor sticks until the text content itself changes, instead of being
    // fought every frame.
    bool  autoSize      = false;
    char  autoSizedText[1024] = {};
};

// ---- 3D world-space text ----
// Pairs with a 3D Transform (the anchor). Rendered as MSDF glyph quads inside
// the main 3D pass (depth-tested, so occluded by geometry). sizeMeters is the
// world height of one em; billboard != 0 makes the text always face the camera.
struct TextLabel3D {
    char  fontPath[256] = {};
    char  text[256]     = {};
    float cr = 1.0f, cg = 1.0f, cb = 1.0f, ca = 1.0f;
    float sizeMeters    = 1.0f;
    int   billboard     = 1;
};

} // namespace ecs
