#pragma once
#include "Entity.h"
#include "../math/Vec3.h"
#include "../physics/PhysicsConstants.h"
#include <cstdint>

class RenderMesh;
class Source;
namespace physics { class Hull; }

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

// ---- Visual ----
struct MeshRenderer {
    RenderMesh* mesh = nullptr;   // non-owning; World still owns the RenderMesh
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
struct AudioSource {
    Source* source = nullptr;   // non-owning; AudioSystem owns the Source
};

// ---- Spring constraint ----
struct SpringConstraint {
    Entity target     = NullEntity;
    float  k          = 1.0f;
    float  restLength = 1.0f;
};

// ---- Identity / debug ----
struct Name {
    char value[64] = {};    // fixed buffer — satisfies trivially-relocatable mandate
};

// ---- Phase C bridge — removed in Phase D when physics::Hull is deleted ----
struct LegacyHullRef {
    physics::Hull* ptr = nullptr;
};

// ---- Tag components (zero-content; presence encodes the condition) ----
struct Sleeping         {};   // physics: body has entered sleep state
struct Fixed            {};   // physics: body is stationary / infinite mass
struct EditorSelectable {};   // editor: show in hierarchy panel (Phase D)
struct EditorPickable   {};   // editor: render in ID buffer pass (Phase D)

} // namespace ecs
