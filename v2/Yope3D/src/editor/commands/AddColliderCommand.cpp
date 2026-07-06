#include "AddColliderCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "world/World.h"
#include "world/TransformHierarchy.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"

void AddColliderCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(entity_)) return;

    // Capture the current (local) Transform + parent so undo can restore them exactly.
    if (!capturedTransform_) {
        if (auto* tf = ctx.registry->get<Transform>(entity_))
            prevTransform_ = *tf;
        oldParent_ = ctx.registry->has<ecs::Parent>(entity_)
                       ? ctx.registry->get<ecs::Parent>(entity_)->parent : ecs::NullEntity;
        capturedTransform_ = true;
    }

    // Auto-unparent: a physics body must be a hierarchy root. Bake the current
    // world pose into the Transform and drop the Parent so nothing moves.
    if (ctx.registry->has<ecs::Parent>(entity_)) {
        Transform w = hierarchy::worldTransform(*ctx.registry, entity_);
        if (auto* tf = ctx.registry->get<Transform>(entity_)) *tf = w;
        ctx.registry->remove<ecs::Parent>(entity_);
    }

    switch (shape_) {
        case Shape::Sphere:
            ctx.world->attachSphereCollider(entity_, mass_, extent_.x, isStatic_);
            break;
        case Shape::AABB:
            ctx.world->attachAABBCollider(entity_, mass_, extent_, isStatic_);
            break;
        case Shape::OBB:
            ctx.world->attachOBBCollider(entity_, mass_, extent_, isStatic_);
            break;
        case Shape::Capsule:
            // extent_.x = radius, extent_.y = halfHeight
            ctx.world->attachCapsuleCollider(entity_, mass_, extent_.x, extent_.y, isStatic_);
            break;
        case Shape::Cylinder:
            ctx.world->attachCylinderCollider(entity_, mass_, extent_.x, extent_.y, isStatic_);
            break;
    }
}

void AddColliderCommand::undo(EditorContext& ctx) {
    if (!ctx.registry->valid(entity_)) return;
    ctx.world->detachPhysicsBody(entity_);
    if (capturedTransform_) {
        // Re-parent (if the entity was a child before) and restore the exact
        // pre-attach local Transform — this also rolls back the scale attach wrote
        // and the local→world bake done in redo.
        if (oldParent_ != ecs::NullEntity && ctx.registry->valid(oldParent_)
            && !ctx.registry->has<ecs::Parent>(entity_))
            ctx.registry->add<ecs::Parent>(entity_, ecs::Parent{oldParent_});
        if (auto* tf = ctx.registry->get<Transform>(entity_))
            *tf = prevTransform_;
    }
}
#endif
