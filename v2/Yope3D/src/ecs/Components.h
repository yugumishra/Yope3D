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
struct UITransform {
    float minX    = 0.0f, minY    = 0.0f;
    float maxX    = 1.0f, maxY    = 1.0f;
    int   depth   = 0;
    bool  visible = true;
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

// Text label. fontPath selects a TextAtlas; text is the rendered string.
// displayPx: glyph height in pixels, 0 = native atlas size.
// alignment: 0 = left/DEFAULT, 1 = CENTERED.
struct UIText {
    char  fontPath[256] = {};
    char  text[1024]    = {};
    float cr = 1.0f, cg = 1.0f, cb = 1.0f, ca = 1.0f;
    int   displayPx = 0;
    int   alignment = 0;
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
