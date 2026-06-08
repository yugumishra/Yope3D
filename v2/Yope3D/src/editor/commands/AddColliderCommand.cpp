#include "AddColliderCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "world/World.h"
#include "ecs/Registry.h"

void AddColliderCommand::redo(EditorContext& ctx) {
    if (!ctx.registry->valid(entity_)) return;

    // Capture the current Transform so undo can restore it exactly.
    if (!capturedTransform_) {
        if (auto* tf = ctx.registry->get<Transform>(entity_))
            prevTransform_ = *tf;
        capturedTransform_ = true;
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
    // Restore the Transform scale that attach* overwrote.
    if (capturedTransform_) {
        if (auto* tf = ctx.registry->get<Transform>(entity_))
            tf->scale = prevTransform_.scale;
    }
}
#endif
