#pragma once
#ifdef YOPE_EDITOR
#include "editor/CommandHistory.h"
#include "editor/EditorContext.h"
#include "editor/commands/SetComponentCommand.h"
#include "editor/commands/CompoundCommand.h"
#include "world/Transform.h"
#include "world/World.h"
#include "ecs/Entity.h"
#include "ecs/Components.h"
#include "ecs/Registry.h"
#include <cmath>
#include <memory>

// Shared anchor + helpers used by every code path that mutates Transform
// during a drag-style edit (inspector DragFloat3, viewport ImGuizmo, future
// script listeners). Centralising this means:
//   1. The "anchor pre-edit values" → "apply scale ratio" → "commit compound
//      command" pipeline is identical for inspector and gizmo, so editing
//      Scale via the gizmo preserves a snapped collider exactly the same way
//      editing it via the inspector does.
//   2. Undo/redo always restores Transform + any present Form atomically.
//   3. Adding a listener hook later only needs to touch one place.

struct TransformEditAnchor {
    bool             active = false;
    ecs::Entity      entity = ecs::NullEntity;
    Transform        tfBefore{};
    bool             hasSphere  = false;  ecs::SphereForm  sphereBefore{};
    bool             hasAABB    = false;  ecs::AABBForm    aabbBefore{};
    bool             hasOBB     = false;  ecs::OBBForm     obbBefore{};
    bool             hasCapsule = false;  ecs::CapsuleForm capsuleBefore{};
    bool             hasCylinder= false;  ecs::CylinderForm cylinderBefore{};
};

namespace transform_edit {

inline float safeRatio(float numer, float denom) {
    return (std::abs(denom) > 1e-6f) ? (numer / denom) : 1.0f;
}

// Capture pre-edit state. Call on the first frame the drag becomes active
// (DragFloat IsItemActivated, gizmo IsUsing transition false→true).
inline void begin(TransformEditAnchor& anchor, ecs::Entity e, ecs::Registry& reg) {
    anchor.active     = true;
    anchor.entity     = e;
    anchor.hasSphere  = false;
    anchor.hasAABB    = false;
    anchor.hasOBB     = false;
    anchor.hasCapsule = false;
    anchor.hasCylinder= false;
    if (auto* tf = reg.get<Transform>(e))        anchor.tfBefore       = *tf;
    if (auto* sf = reg.get<ecs::SphereForm>(e))  { anchor.hasSphere   = true; anchor.sphereBefore   = *sf; }
    if (auto* a  = reg.get<ecs::AABBForm>(e))    { anchor.hasAABB     = true; anchor.aabbBefore     = *a;  }
    if (auto* o  = reg.get<ecs::OBBForm>(e))     { anchor.hasOBB      = true; anchor.obbBefore      = *o;  }
    if (auto* c  = reg.get<ecs::CapsuleForm>(e)) { anchor.hasCapsule  = true; anchor.capsuleBefore  = *c;  }
    if (auto* c  = reg.get<ecs::CylinderForm>(e)){ anchor.hasCylinder = true; anchor.cylinderBefore = *c;  }
}

// Scale any present Form proportionally to the current tf.scale vs anchor.
// Call every frame during the drag *after* writing tf->scale. Safe no-op
// when the entity has no collider, or the anchor isn't active.
// Capsule / Cylinder convention: scale.x (and .z, kept equal) → radius;
//                                scale.y → halfHeight.
inline void applyScaleRatio(const TransformEditAnchor& anchor,
                            ecs::Entity e, ecs::Registry& reg) {
    if (!anchor.active || anchor.entity != e) return;
    auto* tf = reg.get<Transform>(e);
    if (!tf) return;
    float rx = safeRatio(tf->scale.x, anchor.tfBefore.scale.x);
    float ry = safeRatio(tf->scale.y, anchor.tfBefore.scale.y);
    float rz = safeRatio(tf->scale.z, anchor.tfBefore.scale.z);
    if (anchor.hasSphere) {
        if (auto* sf = reg.get<ecs::SphereForm>(e))
            sf->radius = anchor.sphereBefore.radius * rx;
    }
    if (anchor.hasAABB) {
        if (auto* a = reg.get<ecs::AABBForm>(e))
            a->extent = { anchor.aabbBefore.extent.x * rx,
                          anchor.aabbBefore.extent.y * ry,
                          anchor.aabbBefore.extent.z * rz };
    }
    if (anchor.hasOBB) {
        if (auto* o = reg.get<ecs::OBBForm>(e))
            o->extent = { anchor.obbBefore.extent.x * rx,
                          anchor.obbBefore.extent.y * ry,
                          anchor.obbBefore.extent.z * rz };
    }
    if (anchor.hasCapsule) {
        if (auto* c = reg.get<ecs::CapsuleForm>(e)) {
            // Capsule uses baked mesh + identity scale; scale is used transiently by
            // the gizmo as a ratio input. Update the form then reset scale to {1,1,1}
            // so the baked mesh isn't double-scaled.
            float rr = (std::abs(rx - 1.f) >= std::abs(rz - 1.f)) ? rx : rz;
            c->radius     = std::max(0.01f, anchor.capsuleBefore.radius     * rr);
            c->halfHeight = std::max(0.01f, anchor.capsuleBefore.halfHeight * ry);
            tf->scale = {1.f, 1.f, 1.f};
        }
    }
    if (anchor.hasCylinder) {
        if (auto* c = reg.get<ecs::CylinderForm>(e)) {
            float rr = (std::abs(rx - 1.f) >= std::abs(rz - 1.f)) ? rx : rz;
            c->radius     = std::max(0.01f, anchor.cylinderBefore.radius     * rr);
            c->halfHeight = std::max(0.01f, anchor.cylinderBefore.halfHeight * ry);
            tf->scale.x = c->radius;
            tf->scale.z = c->radius;
            tf->scale.y = c->halfHeight;
        }
    }
}

// Push a single undoable CompoundCommand that restores Transform plus every
// Form that was captured. Clears the anchor. Call on edit release
// (IsItemDeactivatedAfterEdit, gizmo IsUsing transition true→false).
inline void commit(TransformEditAnchor& anchor, ecs::Entity e,
                   EditorContext& ctx, const char* label) {
    if (!anchor.active || anchor.entity != e || !ctx.history || !ctx.registry) {
        anchor.active = false;
        return;
    }
    auto group = std::make_unique<CompoundCommand>(label);
    if (auto* tf = ctx.registry->get<Transform>(e))
        group->add(std::make_unique<SetComponentCommand<Transform>>(
            e, anchor.tfBefore, *tf, label));
    if (anchor.hasSphere) {
        if (auto* sf = ctx.registry->get<ecs::SphereForm>(e))
            group->add(std::make_unique<SetComponentCommand<ecs::SphereForm>>(
                e, anchor.sphereBefore, *sf, "Resize Sphere"));
    }
    if (anchor.hasAABB) {
        if (auto* a = ctx.registry->get<ecs::AABBForm>(e))
            group->add(std::make_unique<SetComponentCommand<ecs::AABBForm>>(
                e, anchor.aabbBefore, *a, "Resize AABB"));
    }
    if (anchor.hasOBB) {
        if (auto* o = ctx.registry->get<ecs::OBBForm>(e))
            group->add(std::make_unique<SetComponentCommand<ecs::OBBForm>>(
                e, anchor.obbBefore, *o, "Resize OBB"));
    }
    if (anchor.hasCapsule) {
        if (auto* c = ctx.registry->get<ecs::CapsuleForm>(e))
            group->add(std::make_unique<SetComponentCommand<ecs::CapsuleForm>>(
                e, anchor.capsuleBefore, *c, "Resize Capsule"));
    }
    if (anchor.hasCylinder) {
        if (auto* c = ctx.registry->get<ecs::CylinderForm>(e))
            group->add(std::make_unique<SetComponentCommand<ecs::CylinderForm>>(
                e, anchor.cylinderBefore, *c, "Resize Cylinder"));
    }
    ctx.history->execute(ctx, std::move(group));
    // Capsule: rebuild the baked render mesh so caps are correct after resize.
    if (anchor.hasCapsule && ctx.world) ctx.world->rebuildCapsuleMesh(e);
    anchor.active = false;
}

} // namespace transform_edit
#endif
