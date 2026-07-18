#include "editor/commands/ReparentCommand.h"
#ifdef YOPE_EDITOR
#include "editor/EditorContext.h"
#include "ecs/Registry.h"
#include "ecs/Components.h"
#include "world/Transform.h"
#include "world/TransformHierarchy.h"

namespace {
// Set (or clear, if NullEntity) the Parent component on e.
void applyParent(ecs::Registry& reg, ecs::Entity e, ecs::Entity parent) {
    if (parent == ecs::NullEntity) {
        if (reg.has<ecs::Parent>(e)) reg.remove<ecs::Parent>(e);
    } else if (auto* p = reg.get<ecs::Parent>(e)) {
        p->parent = parent;
    } else {
        reg.add<ecs::Parent>(e, ecs::Parent{parent});
    }
}
} // namespace

bool ReparentCommand::canReparent(ecs::Registry& reg, ecs::Entity entity, ecs::Entity newParent) {
    if (!reg.valid(entity)) return false;
    // Physics bodies are hierarchy roots (v1 invariant).
    if (reg.has<ecs::Hull>(entity)) return false;
    // UI entities use their own depth-ordered layout, not the 3D hierarchy.
    if (reg.has<ecs::UITransform>(entity)) return false;
    if (newParent == ecs::NullEntity) return true;
    if (!reg.valid(newParent)) return false;
    if (newParent == entity) return false;
    // No cycles: newParent may not be entity or one of its descendants.
    if (hierarchy::isDescendantOf(reg, newParent, entity)) return false;
    return true;
}

void ReparentCommand::redo(EditorContext& ctx) {
    ecs::Registry& reg = *ctx.registry;
    if (!canReparent(reg, entity_, newParent_)) return;

    auto* tf = reg.get<Transform>(entity_);
    if (!tf) return;

    if (!captured_) {
        oldParent_ = reg.has<ecs::Parent>(entity_) ? reg.get<ecs::Parent>(entity_)->parent
                                                   : ecs::NullEntity;
        localBefore_ = *tf;
        // Preserve world pose: recompute local against the new parent's world frame.
        Transform world = hierarchy::worldTransform(reg, entity_);
        localAfter_ = (newParent_ == ecs::NullEntity)
                        ? world
                        : hierarchy::toLocal(hierarchy::worldTransform(reg, newParent_), world);
        captured_ = true;
    }

    applyParent(reg, entity_, newParent_);
    *tf = localAfter_;
}

void ReparentCommand::undo(EditorContext& ctx) {
    ecs::Registry& reg = *ctx.registry;
    if (!reg.valid(entity_)) return;
    applyParent(reg, entity_, oldParent_);
    if (auto* tf = reg.get<Transform>(entity_)) *tf = localBefore_;
}
#endif
